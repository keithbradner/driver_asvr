#pragma once

#include "win/CFramePublisher.h"

// Windows direct-mode output: the same IVRDriverDirectModeComponent the Linux
// build serves, backed by D3D11 instead of Vulkan. vrcompositor drives the
// virtual headset through this component (served from vrserver over IPC):
// swap textures are D3D11 shared textures allocated here and opened by the
// compositor and the app, every presented layer is drawn side-by-side into
// an output texture, read back through a staging copy and published to the
// file mapping consumers read.
//
// Threading: all interface methods are called serialized on a vrserver IPC
// thread (a different thread from RunFrame); m_stateLock guards the swap-set
// tables against Shutdown() from the vrserver thread and ResolveAdapterLuid()
// from the HMD's Activate.
class CDirectModeOutput final : public vr::IVRDriverDirectModeComponent
{
public:
    CDirectModeOutput(uint32_t f_width, uint32_t f_height, float f_fps);
    ~CDirectModeOutput();

    void Shutdown();

    // LUID of the adapter the frame pipeline will use, 0 when DXGI has none.
    // DXGI enumeration only, never a D3D device (see EnsureResources); cached.
    uint64_t ResolveAdapterLuid();

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
        ID3D11Texture2D *m_texture = nullptr;
        ID3D11ShaderResourceView *m_view = nullptr; // null when the format cannot be sampled; the set still works for the compositor
    };
    struct SwapSet
    {
        uint32_t m_pid = 0U;
        uint32_t m_width = 0U;
        uint32_t m_height = 0U;
        DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
        bool m_encodeSrgb = false; // the view hands the shader linear values; re-encode on output
        SwapSlot m_slots[3];
        uint32_t m_nextIndex = 0U;
    };
    struct LayerEye
    {
        vr::SharedTextureHandle_t m_handle = 0U;
        vr::VRTextureBounds_t m_bounds = { 0.f, 0.f, 1.f, 1.f };
    };
    struct Layer
    {
        LayerEye m_eye[2];
    };
    static const uint32_t g_maxLayers = 8U;

    bool EnsureResources(); // m_stateLock held
    bool CreateResources(); // m_stateLock held; false after logging the step that failed
    bool CreateShaders(); // m_stateLock held
    void ReleaseResources(); // m_stateLock held
    void ReleaseSet(SwapSet *f_set); // m_stateLock held
    void ReleaseSyncTexture(); // m_stateLock held
    bool FindSlot(vr::SharedTextureHandle_t f_handle, SwapSet *&f_set, uint32_t &f_slot); // m_stateLock held
    IDXGIAdapter1* PickAdapter(); // best hardware adapter (caller releases), records m_adapterLuid
    IDXGIKeyedMutex* ResolveSyncMutex(vr::SharedTextureHandle_t f_handle); // m_stateLock held
    static DXGI_FORMAT MakeTypedFormat(DXGI_FORMAT f_format);
    static DXGI_FORMAT MakeViewFormat(DXGI_FORMAT f_format, bool &f_encodeSrgb);
    double GetSeconds() const;
    void SleepToNextVsync();

    // config
    uint32_t m_width;
    uint32_t m_height;
    float m_fps;

    std::mutex m_stateLock;
    bool m_shuttingDown = false;
    bool m_resourcesReady = false;
    bool m_resourcesFailed = false;
    uint64_t m_adapterLuid = 0U;

    // D3D11 pipeline, created lazily by EnsureResources
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_context = nullptr;
    ID3D11Texture2D *m_output = nullptr;
    ID3D11RenderTargetView *m_outputView = nullptr;
    ID3D11Texture2D *m_staging[2] = { nullptr, nullptr }; // ping-pong by frame parity
    ID3D11VertexShader *m_vertexShader = nullptr;
    ID3D11PixelShader *m_pixelShader = nullptr;
    ID3D11Buffer *m_constants = nullptr;
    ID3D11SamplerState *m_sampler = nullptr;
    ID3D11RasterizerState *m_rasterizer = nullptr;
    ID3D11BlendState *m_blendOpaque = nullptr;
    ID3D11BlendState *m_blendAlpha = nullptr;
    std::vector<uint8_t> m_scratch; // tightly packed copy of the mapped staging rows
    CFramePublisher m_publisher;

    // compositor-owned sync texture, opened once per handle
    vr::SharedTextureHandle_t m_syncHandle = 0U;
    ID3D11Texture2D *m_syncTexture = nullptr;
    IDXGIKeyedMutex *m_syncMutex = nullptr;
    bool m_syncFailureLogged = false;

    std::vector<std::unique_ptr<SwapSet>> m_sets;

    // per-frame
    Layer m_layers[g_maxLayers];
    uint32_t m_layerCount = 0U;
    bool m_framePending = false;
    uint32_t m_frameParity = 0U;

    // vsync simulation + stats
    double m_qpcToSeconds = 0.0;
    double m_lastVsync = 0.0;
    uint32_t m_droppedLastFrame = 0U;
    uint64_t m_presentCount = 0U;
    bool m_layerOverflowLogged = false;
    bool m_viewFailureLogged = false;
};
