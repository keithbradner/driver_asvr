#pragma once

#include <cstdint>

// Frame block shared between driver_asvr and asvr_monitor through a
// named file mapping. The driver owns the shared texture and publishes its handle
// here; the monitor opens the texture whenever `generation` changes.
// Publish order on the driver side: width/height/sharedHandle/magic first, then a
// memory barrier, then the `generation` bump - so the monitor can never observe a
// new generation with stale fields.
struct SharedFrameInfo
{
    uint32_t magic;
    volatile uint32_t generation; // bumped when the texture below is recreated
    uint32_t width;
    uint32_t height;
    uint64_t sharedHandle;
    volatile uint64_t frameCounter;
};

constexpr uint32_t g_sharedFrameMagic = 0x53494D46U; // 'SIMF'
constexpr char g_sharedFrameMappingName[] = "Local\\driver_asvr_frame_v1";

#ifndef _WIN32
// Linux carries the pixels themselves in the shared block (a POSIX shm object)
// instead of a GPU handle: at the driver's default resolution the copy is
// negligible and consumers need no GPU context at all. Two slots so a reader
// never observes a half-written frame: the driver alternates slots, sets
// `latestSlot`, issues a release fence, then bumps `frameCounter`. `generation`
// follows the same discipline for the geometry/format fields.
// (A dmabuf-passing zero-copy variant can be added later as a v3 alongside this;
// fds cannot ride in shm, so it would need an SCM_RIGHTS handshake instead.)
struct SharedFramePixelsInfo
{
    uint32_t magic;
    volatile uint32_t generation;  // bumped when the fields below change
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;          // VkFormat of the payload (43 = R8G8B8A8_SRGB)
    uint32_t rowPitch;             // bytes per row, tightly packed: width * 4
    uint32_t slotOffset[2];        // byte offsets of the pixel slots from shm start
    uint32_t slotSize;             // rowPitch * height
    volatile uint32_t latestSlot;  // slot holding the newest complete frame
    volatile uint64_t frameCounter;
};

constexpr uint32_t g_sharedFramePixelsMagic = 0x53494D50U; // 'SIMP'
constexpr char g_sharedFramePixelsName[] = "/driver_asvr_frame_v2";
#endif
