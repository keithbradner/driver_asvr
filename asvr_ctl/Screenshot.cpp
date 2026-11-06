#include "stdafx.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#endif

// stb's own _CRT_SECURE_NO_WARNINGS comes too late (the PCH already pulled in
// stdio.h) and its HDR writer uses plain sprintf, which SDLCheck makes an error
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Screenshot.h"
#include "../shared/SharedFrameInfo.h"

// Saves the frame the driver publishes in the shared pixel block as PNG. The
// block is a plain CPU copy of the composited output (both eyes side by side,
// 4 bytes per pixel) on both platforms, so no GPU access is needed here; the
// only platform-specific pieces are opening the block, the clock and a nap.

#ifdef _WIN32
typedef HANDLE FrameBlockHandle;
static const FrameBlockHandle g_invalidFrameBlock = NULL;

static FrameBlockHandle OpenFrameBlock()
{
    return OpenFileMappingA(FILE_MAP_READ, FALSE, g_sharedFramePixelsName);
}

static const void* MapFrameBlock(FrameBlockHandle f_handle, size_t f_size)
{
    return MapViewOfFile(f_handle, FILE_MAP_READ, 0U, 0U, f_size);
}

static void UnmapFrameBlock(const void *f_view, size_t)
{
    UnmapViewOfFile(f_view);
}

static void CloseFrameBlock(FrameBlockHandle f_handle)
{
    CloseHandle(f_handle);
}

static uint64_t GetMilliseconds()
{
    return GetTickCount64();
}

static void SleepBriefly()
{
    Sleep(2U);
}
#else
typedef int FrameBlockHandle;
static const FrameBlockHandle g_invalidFrameBlock = -1;

static FrameBlockHandle OpenFrameBlock()
{
    return shm_open(g_sharedFramePixelsName, O_RDONLY, 0);
}

static const void* MapFrameBlock(FrameBlockHandle f_fd, size_t f_size)
{
    const void *l_view = mmap(nullptr, f_size, PROT_READ, MAP_SHARED, f_fd, 0);
    return (l_view == MAP_FAILED) ? nullptr : l_view;
}

static void UnmapFrameBlock(const void *f_view, size_t f_size)
{
    munmap(const_cast<void*>(f_view), f_size);
}

static void CloseFrameBlock(FrameBlockHandle f_fd)
{
    close(f_fd);
}

static uint64_t GetMilliseconds()
{
    timespec l_time;
    clock_gettime(CLOCK_MONOTONIC, &l_time);
    return (static_cast<uint64_t>(l_time.tv_sec) * 1000ULL) + (static_cast<uint64_t>(l_time.tv_nsec) / 1000000ULL);
}

static void SleepBriefly()
{
    usleep(2000U);
}
#endif

// Maps the whole block read-only: the header first to learn the size, then a
// full-size view (the block only grows with the headset resolution). Prints
// the failure reason; nullptr on failure.
static const SharedFramePixelsInfo* MapWholeFrameBlock(size_t &f_mappingSize)
{
    const FrameBlockHandle l_handle = OpenFrameBlock();
    if(l_handle == g_invalidFrameBlock)
    {
        fputs("err: frame block not found (is SteamVR running with the ASVR driver?)\n", stderr);
        return nullptr;
    }

    SharedFramePixelsInfo l_header = {};
    {
        const void *l_view = MapFrameBlock(l_handle, sizeof(SharedFramePixelsInfo));
        if(!l_view)
        {
            CloseFrameBlock(l_handle);
            fputs("err: failed to map the frame info\n", stderr);
            return nullptr;
        }
        memcpy(&l_header, l_view, sizeof(l_header));
        UnmapFrameBlock(l_view, sizeof(SharedFramePixelsInfo));
    }
    if((l_header.magic != g_sharedFramePixelsMagic) || (l_header.slotSize == 0U))
    {
        CloseFrameBlock(l_handle);
        fputs("err: no frame published yet (compositor not presenting?)\n", stderr);
        return nullptr;
    }

    f_mappingSize = static_cast<size_t>(l_header.slotOffset[1]) + l_header.slotSize;
    const void *l_view = MapFrameBlock(l_handle, f_mappingSize);
    CloseFrameBlock(l_handle); // the view keeps the block alive on both platforms
    if(!l_view) fputs("err: failed to map the frame block\n", stderr);
    return static_cast<const SharedFramePixelsInfo*>(l_view);
}

int RunScreenshot(int argc, char *argv[])
{
    const char *l_outputPath = nullptr;
    bool l_bothEyes = false;
    bool l_waitNewFrame = false;
    for(int i = 0; i < argc; i++)
    {
        const std::string l_argument(argv[i]);
        if(!l_argument.compare("--both")) l_bothEyes = true;
        else if(!l_argument.compare("--wait-new-frame")) l_waitNewFrame = true;
        else if(!l_argument.compare("--adapter"))
        {
            i++; // obsolete (the pixels no longer come from a GPU); accepted so old scripts keep working
        }
        else if(!l_outputPath) l_outputPath = argv[i];
        else
        {
            fprintf(stderr, "err: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if(!l_outputPath)
    {
        fputs("err: missing output path\n", stderr);
        return 2;
    }

    size_t l_mappingSize = 0U;
    const SharedFramePixelsInfo *l_info = MapWholeFrameBlock(l_mappingSize);
    if(!l_info) return 2;
    const char *l_base = reinterpret_cast<const char*>(l_info);

    // Geometry is read once; the driver only changes it on a mode switch, and
    // frameCounter covers that for the copy below
    const uint32_t l_width = l_info->width;
    const uint32_t l_height = l_info->height;
    const uint32_t l_rowPitch = l_info->rowPitch;
    const uint32_t l_slotSize = l_info->slotSize;
    const uint32_t l_pixelFormat = l_info->pixelFormat;

    int l_result = 2;
    std::vector<unsigned char> l_pixels(l_slotSize);
    do
    {
        if(l_waitNewFrame)
        {
            const uint64_t l_baseline = l_info->frameCounter;
            const uint64_t l_deadline = GetMilliseconds() + 5000ULL;
            while(l_info->frameCounter == l_baseline)
            {
                if(GetMilliseconds() >= l_deadline) break;
                SleepBriefly();
            }
            if(l_info->frameCounter == l_baseline)
            {
                fputs("err: timed out waiting for a new frame\n", stderr);
                break;
            }
        }
        else if(l_info->frameCounter == 0U)
        {
            fputs("err: no frame published yet (compositor not presenting?)\n", stderr);
            break;
        }

        // Copy the newest complete slot; re-check the counter afterwards. The
        // driver bumps the counter before it can come back to this slot, so an
        // unchanged counter guarantees an untouched copy; anything else is a
        // retry (a bump during the copy may already be the next write into
        // this very slot)
        bool l_stable = false;
        for(int l_attempt = 0; (l_attempt < 6) && !l_stable; l_attempt++)
        {
            const uint64_t l_before = l_info->frameCounter;
            const uint32_t l_slot = l_info->latestSlot;
            memcpy(l_pixels.data(), l_base + l_info->slotOffset[l_slot & 1U], l_slotSize);
            const uint64_t l_after = l_info->frameCounter;
            l_stable = (l_after == l_before);
        }
        if(!l_stable)
        {
            fputs("err: frame kept changing during the copy\n", stderr);
            break;
        }

        // Normalize to RGBA for the PNG: swap if the payload is BGRA, force alpha opaque
        if(!SharedPixelFormatIsKnown(l_pixelFormat))
        {
            fprintf(stderr, "err: unsupported pixel format %u\n", l_pixelFormat);
            break;
        }
        const bool l_swapChannels = SharedPixelFormatIsBgra(l_pixelFormat);
        for(size_t i = 0U; i < l_pixels.size(); i += 4U)
        {
            if(l_swapChannels) std::swap(l_pixels[i], l_pixels[i + 2U]);
            l_pixels[i + 3U] = 255U;
        }

        // Left eye = left half of every row; the stride stays the full row
        const uint32_t l_outputWidth = l_bothEyes ? l_width : (l_width / 2U);
        if(!stbi_write_png(l_outputPath, static_cast<int>(l_outputWidth), static_cast<int>(l_height), 4,
            l_pixels.data(), static_cast<int>(l_rowPitch)))
        {
            fprintf(stderr, "err: failed to write '%s'\n", l_outputPath);
            break;
        }
        printf("ok %ux%u %s\n", l_outputWidth, l_height, l_outputPath);
        l_result = 0;
    } while(false);

    UnmapFrameBlock(l_info, l_mappingSize);
    return l_result;
}
