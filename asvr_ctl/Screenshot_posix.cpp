#include "stdafx.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "Screenshot.h"
#include "../shared/SharedFrameInfo.h"

// VkFormat values the driver can publish (see CDirectModeOutput)
static constexpr uint32_t g_vkFormatR8G8B8A8Unorm = 37U;
static constexpr uint32_t g_vkFormatR8G8B8A8Srgb = 43U;
static constexpr uint32_t g_vkFormatB8G8R8A8Unorm = 44U;
static constexpr uint32_t g_vkFormatB8G8R8A8Srgb = 50U;

static uint64_t GetMilliseconds()
{
    timespec l_time;
    clock_gettime(CLOCK_MONOTONIC, &l_time);
    return (static_cast<uint64_t>(l_time.tv_sec) * 1000ULL) + (static_cast<uint64_t>(l_time.tv_nsec) / 1000000ULL);
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
            i++; // GPU selection is a Windows concern; accepted so scripts stay portable
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

    const int l_fd = shm_open(g_sharedFramePixelsName, O_RDONLY, 0);
    if(l_fd < 0)
    {
        fputs("err: frame block not found (is SteamVR running with the ASVR driver?)\n", stderr);
        return 2;
    }

    // Map the header first to learn the full block size
    SharedFramePixelsInfo l_header = {};
    {
        const void *l_view = mmap(nullptr, sizeof(SharedFramePixelsInfo), PROT_READ, MAP_SHARED, l_fd, 0);
        if(l_view == MAP_FAILED)
        {
            close(l_fd);
            fputs("err: failed to map the frame info\n", stderr);
            return 2;
        }
        memcpy(&l_header, l_view, sizeof(l_header));
        munmap(const_cast<void*>(l_view), sizeof(SharedFramePixelsInfo));
    }
    if((l_header.magic != g_sharedFramePixelsMagic) || (l_header.slotSize == 0U))
    {
        close(l_fd);
        fputs("err: no frame published yet (compositor not presenting?)\n", stderr);
        return 2;
    }

    const size_t l_mappingSize = static_cast<size_t>(l_header.slotOffset[1]) + l_header.slotSize;
    const SharedFramePixelsInfo *l_info = static_cast<const SharedFramePixelsInfo*>(
        mmap(nullptr, l_mappingSize, PROT_READ, MAP_SHARED, l_fd, 0));
    close(l_fd);
    if(l_info == MAP_FAILED)
    {
        fputs("err: failed to map the frame block\n", stderr);
        return 2;
    }
    const char *l_base = reinterpret_cast<const char*>(l_info);

    int l_result = 2;
    std::vector<unsigned char> l_pixels(l_header.slotSize);
    do
    {
        if(l_waitNewFrame)
        {
            const uint64_t l_baseline = l_info->frameCounter;
            const uint64_t l_deadline = GetMilliseconds() + 5000ULL;
            while(l_info->frameCounter == l_baseline)
            {
                if(GetMilliseconds() >= l_deadline) break;
                usleep(2000U);
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

        // Copy the newest complete slot; re-check the counter afterwards to
        // detect the driver lapping us mid-copy (double-buffered, so one lap
        // is needed to tear)
        bool l_stable = false;
        for(int l_attempt = 0; (l_attempt < 4) && !l_stable; l_attempt++)
        {
            const uint64_t l_before = l_info->frameCounter;
            const uint32_t l_slot = l_info->latestSlot;
            memcpy(l_pixels.data(), l_base + l_info->slotOffset[l_slot & 1U], l_header.slotSize);
            const uint64_t l_after = l_info->frameCounter;
            l_stable = ((l_after == l_before) || (l_after == (l_before + 1U)));
        }
        if(!l_stable)
        {
            fputs("err: frame kept changing during the copy\n", stderr);
            break;
        }

        // Normalize to RGBA for the PNG: swap if the payload is BGRA, force alpha opaque
        const bool l_swapChannels = (l_header.pixelFormat == g_vkFormatB8G8R8A8Unorm) || (l_header.pixelFormat == g_vkFormatB8G8R8A8Srgb);
        if((l_header.pixelFormat != g_vkFormatR8G8B8A8Unorm) && (l_header.pixelFormat != g_vkFormatR8G8B8A8Srgb) && !l_swapChannels)
        {
            fprintf(stderr, "err: unsupported pixel format %u\n", l_header.pixelFormat);
            break;
        }
        for(size_t i = 0U; i < l_pixels.size(); i += 4U)
        {
            if(l_swapChannels) std::swap(l_pixels[i], l_pixels[i + 2U]);
            l_pixels[i + 3U] = 255U;
        }

        const uint32_t l_outputWidth = l_bothEyes ? l_header.width : (l_header.width / 2U);
        if(!stbi_write_png(l_outputPath, static_cast<int>(l_outputWidth), static_cast<int>(l_header.height), 4,
            l_pixels.data(), static_cast<int>(l_header.rowPitch)))
        {
            fprintf(stderr, "err: failed to write '%s'\n", l_outputPath);
            break;
        }
        printf("ok %ux%u %s\n", l_outputWidth, l_header.height, l_outputPath);
        l_result = 0;
    } while(false);

    munmap(const_cast<SharedFramePixelsInfo*>(const_cast<const SharedFramePixelsInfo*>(l_info)), l_mappingSize);
    return l_result;
}
