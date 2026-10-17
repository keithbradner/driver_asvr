#pragma once

#include "linux/CVulkanContext.h"
#include "linux/CFramePublisher.h"

// Linux replacement for the Windows IVRVirtualDisplay path: SteamVR's Linux
// compositor drives direct-mode drivers through this component (served from
// vrserver over IPC). Swap textures are allocated in SteamVR's IPC resource
// manager, imported here by fd, and every presented frame is blitted
// side-by-side into an output image, copied to a host-visible buffer and
// published to the shm block consumers read.
//
// Threading: all methods are called serialized on a vrserver IPC thread (a
// different thread from RunFrame); m_stateLock guards the swap-set tables
// against Shutdown() from the vrserver thread.
class CDirectModeOutput final : public vr::IVRDriverDirectModeComponent
{
public:
    CDirectModeOutput(uint32_t f_width, uint32_t f_height, float f_fps);
    ~CDirectModeOutput();

    void Shutdown();

    // vr::IVRDriverDirectModeComponent
    void CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t *pSwapTextureSetDesc, SwapTextureSet_t *pOutSwapTextureSet) override;
    void DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle) override;
    void DestroyAllSwapTextureSets(uint32_t unPid) override;
    void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2]) override;
    void SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2]) override;
    void Present(vr::SharedTextureHandle_t syncTexture) override;
    void PostPresent(const Throttling_t *pThrottling) override;
    void GetFrameTiming(vr::DriverDirectMode_FrameTiming *pFrameTiming) override;

private:
    struct SwapSlot
    {
        vr::SharedTextureHandle_t m_handle = 0U;
        VkImage m_image = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        int m_syncFd = -1; // retained dup of the shared fd, polled for implicit-sync
    };
    struct SwapSet
    {
        uint32_t m_pid = 0U;
        uint32_t m_width = 0U;
        uint32_t m_height = 0U;
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        SwapSlot m_slots[3];
        uint32_t m_nextIndex = 0U;
    };
    struct LayerEye
    {
        vr::SharedTextureHandle_t m_handle = 0U;
        vr::VRTextureBounds_t m_bounds = { 0.f, 0.f, 1.f, 1.f };
    };

    bool EnsureResources(); // m_stateLock held
    void ReleaseSet(SwapSet *f_set); // m_stateLock held
    bool FindSlot(vr::SharedTextureHandle_t f_handle, SwapSet *&f_set, uint32_t &f_slot); // m_stateLock held
    static VkFormat TranslateSwapFormat(uint32_t f_raw);
    static double GetSeconds();
    void SleepToNextVsync();

    // config
    uint32_t m_width;
    uint32_t m_height;
    float m_fps;

    std::mutex m_stateLock;
    bool m_shuttingDown = false;
    bool m_resourcesReady = false;
    bool m_resourceFailureLogged = false;

    CVulkanContext m_vk;
    CFramePublisher m_publisher;
    VkImage m_outputImage = VK_NULL_HANDLE;
    VkDeviceMemory m_outputMemory = VK_NULL_HANDLE;
    VkBuffer m_staging = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    void *m_stagingMapped = nullptr;

    std::vector<std::unique_ptr<SwapSet>> m_sets;

    // per-frame
    LayerEye m_scene[2];
    uint32_t m_layersThisFrame = 0U;
    bool m_framePending = false;
    uint32_t m_frameParity = 0U;

    // vsync simulation + stats
    double m_lastVsync = 0.0;
    uint32_t m_droppedLastFrame = 0U;
    uint64_t m_presentCount = 0U;
    bool m_overlayLayersLogged = false;
};
