#pragma once

#include <cstdint>

// Frame block shared between driver_asvr and its consumers (asvr_monitor,
// asvr_ctl): a named file mapping on Windows, a POSIX shm object on Linux, the
// same layout on both. The driver's direct-mode component blits both eyes
// side-by-side into an output image, reads it back and publishes the pixels
// here (4 bytes per pixel, tightly packed), so consumers need no GPU access at
// all - and no adapter matching, which is what made the old Windows shared
// texture handle fragile over RDP.
//
// Two pixel slots so a reader never observes a half-written frame: the driver
// alternates slots, sets `latestSlot`, issues a release fence, then bumps
// `frameCounter`. `generation` follows the same discipline for the geometry
// and format fields. A reader copies the latest slot and re-checks
// frameCounter afterwards: an unchanged counter guarantees an untouched slot
// (the driver bumps it before it can start on that slot again), so any change
// means copy again.
struct SharedFramePixelsInfo
{
    uint32_t magic;
    volatile uint32_t generation;  // bumped when the fields below change
    uint32_t width;                // both eyes, side by side (left eye = left half)
    uint32_t height;
    uint32_t pixelFormat;          // g_sharedPixelFormat_* below (VkFormat numbering on every platform)
    uint32_t rowPitch;             // bytes per row, tightly packed: width * 4
    uint32_t slotOffset[2];        // byte offsets of the pixel slots from the block start
    uint32_t slotSize;             // rowPitch * height
    volatile uint32_t latestSlot;  // slot holding the newest complete frame
    volatile uint64_t frameCounter;
};

constexpr uint32_t g_sharedFramePixelsMagic = 0x53494D50U; // 'SIMP'
#ifdef _WIN32
constexpr char g_sharedFramePixelsName[] = "Local\\driver_asvr_frame_v2";
#else
constexpr char g_sharedFramePixelsName[] = "/driver_asvr_frame_v2";
#endif

// Published pixel formats; the bytes are display-ready (sRGB-encoded), the
// name only says which channel order they come in
constexpr uint32_t g_sharedPixelFormat_R8G8B8A8_Unorm = 37U; // VK_FORMAT_R8G8B8A8_UNORM
constexpr uint32_t g_sharedPixelFormat_R8G8B8A8_Srgb = 43U;  // VK_FORMAT_R8G8B8A8_SRGB
constexpr uint32_t g_sharedPixelFormat_B8G8R8A8_Unorm = 44U; // VK_FORMAT_B8G8R8A8_UNORM
constexpr uint32_t g_sharedPixelFormat_B8G8R8A8_Srgb = 50U;  // VK_FORMAT_B8G8R8A8_SRGB

inline bool SharedPixelFormatIsBgra(uint32_t f_format)
{
    return (f_format == g_sharedPixelFormat_B8G8R8A8_Unorm) || (f_format == g_sharedPixelFormat_B8G8R8A8_Srgb);
}

inline bool SharedPixelFormatIsKnown(uint32_t f_format)
{
    return (f_format == g_sharedPixelFormat_R8G8B8A8_Unorm) || (f_format == g_sharedPixelFormat_R8G8B8A8_Srgb) || SharedPixelFormatIsBgra(f_format);
}
