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
