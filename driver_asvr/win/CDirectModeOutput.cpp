#include "stdafx.h"

#include <cstdarg>

#include "win/CDirectModeOutput.h"
#include "../shared/SharedFrameInfo.h"

static void LogLine(const char *f_format, ...)
{
    char l_line[256];
    va_list l_args;
    va_start(l_args, f_format);
    vsnprintf(l_line, sizeof(l_line), f_format, l_args);
    va_end(l_args);
    vr::IVRDriverLog *l_log = vr::VRDriverLog();
    if(l_log) l_log->Log(l_line);
}

template<class T> static void SafeRelease(T *&f_object)
{
    if(f_object)
    {
        f_object->Release();
        f_object = nullptr;
    }
}

// One oversized triangle per eye; the layer's VRTextureBounds_t rectangle is
// mapped onto the viewport by remapping the triangle's UVs, so vMin > vMax
// (vertically flipped sources) simply samples the other way round. Sampling
// goes through UNORM views so the bytes the app wrote come out unchanged;
// g_params.x asks for an sRGB encode when the view could only be linear
// (typed sRGB and float formats).
static const char g_shaderSource[] = R"(
cbuffer Constants : register(b0)
{
    float4 g_bounds; // uMin, vMin, uMax, vMax
    float4 g_params; // x: 1 = encode the sampled linear colour as sRGB
};
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    // (0,0) (2,0) (0,2) in viewport space; the parts outside are clipped
    float2 unit = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(unit * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = lerp(g_bounds.xy, g_bounds.zw, unit);
    return o;
}

float3 EncodeSrgb(float3 c)
{
    c = saturate(c);
    float3 low = c * 12.92;
    float3 high = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(c, 0.0031308));
}

float4 PSMain(VSOut i) : SV_Target
{
    float4 c = g_texture.Sample(g_sampler, i.uv);
    if(g_params.x > 0.5) c.rgb = EncodeSrgb(c.rgb);
    return c;
}
)";

struct ShaderConstants
{
    float m_bounds[4];
    float m_params[4];
};

CDirectModeOutput::CDirectModeOutput(uint32_t f_width, uint32_t f_height, float f_fps)
{
    m_width = f_width;
    m_height = f_height;
    m_fps = (f_fps > 1.f) ? f_fps : 60.f;

    LARGE_INTEGER l_frequency;
    QueryPerformanceFrequency(&l_frequency);
    m_qpcToSeconds = 1.0 / static_cast<double>(l_frequency.QuadPart);

    // SleepToNextVsync sleeps in 1 ms slices; the default 15.6 ms timer
    // granularity would make them land anywhere inside a frame
    timeBeginPeriod(1U);
}

CDirectModeOutput::~CDirectModeOutput()
{
    Shutdown();
    timeEndPeriod(1U);
}

void CDirectModeOutput::Shutdown()
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    if(m_shuttingDown) return;
    m_shuttingDown = true;

    for(auto &l_set : m_sets) ReleaseSet(l_set.get());
    m_sets.clear();
    m_layerCount = 0U;
    m_framePending = false;

    ReleaseSyncTexture();
    if(m_context)
    {
        // Unbind everything so the resources below really go away with their last Release
        m_context->ClearState();
        m_context->Flush();
    }
    ReleaseResources();
}

double CDirectModeOutput::GetSeconds() const
{
    LARGE_INTEGER l_counter;
    QueryPerformanceCounter(&l_counter);
    return static_cast<double>(l_counter.QuadPart) * m_qpcToSeconds;
}

// The UNORM (or float) equivalent of a typeless format; views need a typed one
DXGI_FORMAT CDirectModeOutput::MakeTypedFormat(DXGI_FORMAT f_format)
{
    DXGI_FORMAT l_result = f_format;
    switch(f_format)
    {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            l_result = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            l_result = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            l_result = DXGI_FORMAT_B8G8R8X8_UNORM;
            break;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            l_result = DXGI_FORMAT_R10G10B10A2_UNORM;
            break;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            l_result = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            l_result = DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
    }
    return l_result;
}

// The format the swap texture is sampled through. UNORM wherever the texture
// allows it: the bytes the app wrote are display-ready for every normal app
// and the SteamVR dashboard, and must not be gamma-converted on the way out.
// A typed sRGB texture only accepts an sRGB view (D3D11 casts from TYPELESS
// only), which decodes to linear on sampling, and float textures hold linear
// light by definition - both get re-encoded in the shader so the published
// bytes still match what the app wrote.
DXGI_FORMAT CDirectModeOutput::MakeViewFormat(DXGI_FORMAT f_format, bool &f_encodeSrgb)
{
    f_encodeSrgb = false;
    switch(f_format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R11G11B10_FLOAT:
            f_encodeSrgb = true;
            break;
    }
    return MakeTypedFormat(f_format);
}

// The compositor renders on the discrete GPU; shared textures only open on the
// same adapter, so skip software/Microsoft adapters (RDP remote display) and
// take the biggest hardware GPU. Null when DXGI has no usable adapter.
IDXGIAdapter1* CDirectModeOutput::PickAdapter()
{
    IDXGIFactory1 *l_factory = nullptr;
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&l_factory))))
    {
        LogLine("CDirectModeOutput: CreateDXGIFactory1 failed");
        return nullptr;
    }

    IDXGIAdapter1 *l_adapter = nullptr;
    SIZE_T l_bestMemory = 0U;
    for(UINT i = 0U; ; i++)
    {
        IDXGIAdapter1 *l_candidate = nullptr;
        if(l_factory->EnumAdapters1(i, &l_candidate) != S_OK) break;

        DXGI_ADAPTER_DESC1 l_desc;
        l_candidate->GetDesc1(&l_desc);
        const bool l_softwareLike = ((l_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U) || (l_desc.VendorId == 0x1414U);
        if(!l_softwareLike && (l_desc.DedicatedVideoMemory > l_bestMemory))
        {
            if(l_adapter) l_adapter->Release();
            l_adapter = l_candidate;
            l_bestMemory = l_desc.DedicatedVideoMemory;
        }
        else l_candidate->Release();
    }
    l_factory->Release();

    if(l_adapter)
    {
        DXGI_ADAPTER_DESC1 l_desc;
        l_adapter->GetDesc1(&l_desc);
        m_adapterLuid = (static_cast<uint64_t>(static_cast<uint32_t>(l_desc.AdapterLuid.HighPart)) << 32) | l_desc.AdapterLuid.LowPart;
    }
    return l_adapter;
}

uint64_t CDirectModeOutput::ResolveAdapterLuid()
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    if(m_adapterLuid == 0U)
    {
        IDXGIAdapter1 *l_adapter = PickAdapter();
        if(l_adapter) l_adapter->Release();
    }
    return m_adapterLuid;
}

// The device is created here, on the compositor's first CreateSwapTextureSet,
// and never at construction or HMD activation: D3D11CreateDevice pulls the
// GPU's user-mode driver into vrserver, a large module load that took part in
// a start-up crash when it overlapped vrserver's own loading (see the comment
// above CServerDriver::RegisterDevices). By the time the compositor asks for
// textures that start-up is long over.
bool CDirectModeOutput::EnsureResources()
{
    if(m_resourcesReady) return true;
    if(m_resourcesFailed) return false; // logged once; the headset runs headless from here on

    if(!CreateResources())
    {
        LogLine("CDirectModeOutput: frame pipeline unavailable, running headless (tracking only)");
        ReleaseResources();
        m_resourcesFailed = true;
        return false;
    }
    m_resourcesReady = true;
    return true;
}

bool CDirectModeOutput::CreateResources()
{
    IDXGIAdapter1 *l_adapter = PickAdapter();
    HRESULT l_result = D3D11CreateDevice(l_adapter, l_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        NULL, 0U, NULL, 0U, D3D11_SDK_VERSION, &m_device, NULL, &m_context);
    if(l_adapter) l_adapter->Release();
    if(FAILED(l_result))
    {
        LogLine("CDirectModeOutput: D3D11CreateDevice failed, hr=0x%08lX", static_cast<unsigned long>(l_result));
        return false;
    }

    D3D11_TEXTURE2D_DESC l_outputDesc = {};
    l_outputDesc.Width = m_width;
    l_outputDesc.Height = m_height;
    l_outputDesc.MipLevels = 1U;
    l_outputDesc.ArraySize = 1U;
    l_outputDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    l_outputDesc.SampleDesc.Count = 1U;
    l_outputDesc.Usage = D3D11_USAGE_DEFAULT;
    l_outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    l_result = m_device->CreateTexture2D(&l_outputDesc, NULL, &m_output);
    if(SUCCEEDED(l_result)) l_result = m_device->CreateRenderTargetView(m_output, NULL, &m_outputView);
    if(FAILED(l_result))
    {
        LogLine("CDirectModeOutput: output texture creation failed, hr=0x%08lX", static_cast<unsigned long>(l_result));
        return false;
    }

    D3D11_TEXTURE2D_DESC l_stagingDesc = l_outputDesc;
    l_stagingDesc.Usage = D3D11_USAGE_STAGING;
    l_stagingDesc.BindFlags = 0U;
    l_stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for(int i = 0; i < 2; i++)
    {
        l_result = m_device->CreateTexture2D(&l_stagingDesc, NULL, &m_staging[i]);
        if(FAILED(l_result))
        {
            LogLine("CDirectModeOutput: staging texture creation failed, hr=0x%08lX", static_cast<unsigned long>(l_result));
            return false;
        }
    }

    if(!CreateShaders()) return false;

    D3D11_BUFFER_DESC l_constantsDesc = {};
    l_constantsDesc.ByteWidth = sizeof(ShaderConstants);
    l_constantsDesc.Usage = D3D11_USAGE_DYNAMIC;
    l_constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    l_constantsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    l_result = m_device->CreateBuffer(&l_constantsDesc, NULL, &m_constants);
    if(FAILED(l_result))
    {
        LogLine("CDirectModeOutput: constant buffer creation failed, hr=0x%08lX", static_cast<unsigned long>(l_result));
        return false;
    }

    D3D11_SAMPLER_DESC l_samplerDesc = {};
    l_samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    l_samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    l_samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    l_samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    l_samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    l_result = m_device->CreateSamplerState(&l_samplerDesc, &m_sampler);

    D3D11_RASTERIZER_DESC l_rasterizerDesc = {};
    l_rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    l_rasterizerDesc.CullMode = D3D11_CULL_NONE;
    l_rasterizerDesc.DepthClipEnable = TRUE;
    if(SUCCEEDED(l_result)) l_result = m_device->CreateRasterizerState(&l_rasterizerDesc, &m_rasterizer);

    // Colour only: apps routinely leave alpha at 0 in their render targets and
    // consumers draw the published pixels with blending on (the monitor goes
    // through ImGui), so the output keeps the opaque alpha it was cleared to
    D3D11_BLEND_DESC l_blendDesc = {};
    l_blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    if(SUCCEEDED(l_result)) l_result = m_device->CreateBlendState(&l_blendDesc, &m_blendOpaque);
    // Straight (non-premultiplied) alpha: overlay layers over the scene; the
    // source alpha still drives the blend even though it is never stored
    l_blendDesc.RenderTarget[0].BlendEnable = TRUE;
    l_blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    l_blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    l_blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    l_blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    l_blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    l_blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    if(SUCCEEDED(l_result)) l_result = m_device->CreateBlendState(&l_blendDesc, &m_blendAlpha);
    if(FAILED(l_result))
    {
        LogLine("CDirectModeOutput: pipeline state creation failed, hr=0x%08lX", static_cast<unsigned long>(l_result));
        return false;
    }

    m_scratch.resize(static_cast<size_t>(m_width) * m_height * 4U);
    if(!m_publisher.Create(m_width, m_height, g_sharedPixelFormat_R8G8B8A8_Srgb))
    {
        LogLine("CDirectModeOutput: shared frame mapping creation failed, error=%lu", GetLastError());
        return false;
    }

    LogLine("CDirectModeOutput: frame pipeline ready (%ux%u @ %.1f fps, adapter LUID 0x%llX, pid=%lu)",
        m_width, m_height, m_fps, static_cast<unsigned long long>(m_adapterLuid), GetCurrentProcessId());
    return true;
}

// Compiled at runtime: d3dcompiler_47.dll ships with Windows 10/11, which
// keeps the shaders readable next to the code that sets their constants
bool CDirectModeOutput::CreateShaders()
{
    const UINT l_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const struct
    {
        const char *m_entry;
        const char *m_target;
    } l_stages[2] = { { "VSMain", "vs_4_0" }, { "PSMain", "ps_4_0" } };

    for(int i = 0; i < 2; i++)
    {
        ID3DBlob *l_code = nullptr;
        ID3DBlob *l_errors = nullptr;
        HRESULT l_result = D3DCompile(g_shaderSource, sizeof(g_shaderSource) - 1U, "asvr_blit", NULL, NULL,
            l_stages[i].m_entry, l_stages[i].m_target, l_flags, 0U, &l_code, &l_errors);
        if(FAILED(l_result))
        {
            LogLine("CDirectModeOutput: %s compile failed, hr=0x%08lX: %s", l_stages[i].m_entry, static_cast<unsigned long>(l_result),
                l_errors ? static_cast<const char*>(l_errors->GetBufferPointer()) : "(no diagnostics)");
            SafeRelease(l_errors);
            SafeRelease(l_code);
            return false;
        }
        SafeRelease(l_errors);

        if(i == 0) l_result = m_device->CreateVertexShader(l_code->GetBufferPointer(), l_code->GetBufferSize(), NULL, &m_vertexShader);
        else l_result = m_device->CreatePixelShader(l_code->GetBufferPointer(), l_code->GetBufferSize(), NULL, &m_pixelShader);
        l_code->Release();
        if(FAILED(l_result))
        {
            LogLine("CDirectModeOutput: %s creation failed, hr=0x%08lX", l_stages[i].m_entry, static_cast<unsigned long>(l_result));
            return false;
        }
    }
    return true;
}

void CDirectModeOutput::ReleaseResources()
{
    m_publisher.Destroy();
    SafeRelease(m_blendAlpha);
    SafeRelease(m_blendOpaque);
    SafeRelease(m_rasterizer);
    SafeRelease(m_sampler);
    SafeRelease(m_constants);
    SafeRelease(m_pixelShader);
    SafeRelease(m_vertexShader);
    for(int i = 0; i < 2; i++) SafeRelease(m_staging[i]);
    SafeRelease(m_outputView);
    SafeRelease(m_output);
    SafeRelease(m_context);
    SafeRelease(m_device);
    m_resourcesReady = false;
}

void CDirectModeOutput::CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t *pSwapTextureSetDesc, SwapTextureSet_t *pOutSwapTextureSet)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);

    memset(pOutSwapTextureSet, 0, sizeof(SwapTextureSet_t));
    LogLine("CDirectModeOutput: CreateSwapTextureSet pid=%u %ux%u format=%u samples=%u",
        unPid, pSwapTextureSetDesc->nWidth, pSwapTextureSetDesc->nHeight, pSwapTextureSetDesc->nFormat, pSwapTextureSetDesc->nSampleCount);
    if(m_shuttingDown || !EnsureResources()) return; // headless: tracking keeps working without frames

    auto l_set = std::make_unique<SwapSet>();
    l_set->m_pid = unPid;
    l_set->m_width = pSwapTextureSetDesc->nWidth;
    l_set->m_height = pSwapTextureSetDesc->nHeight;
    l_set->m_format = static_cast<DXGI_FORMAT>(pSwapTextureSetDesc->nFormat); // a DXGI_FORMAT on Windows
    if(pSwapTextureSetDesc->nSampleCount > 1U) LogLine("CDirectModeOutput: multisampled swap set requested (%u), allocating single-sampled", pSwapTextureSetDesc->nSampleCount);

    // Legacy shared handles: vrcompositor and the app open these with OpenSharedResource
    D3D11_TEXTURE2D_DESC l_desc = {};
    l_desc.Width = l_set->m_width;
    l_desc.Height = l_set->m_height;
    l_desc.MipLevels = 1U;
    l_desc.ArraySize = 1U;
    l_desc.Format = l_set->m_format;
    l_desc.SampleDesc.Count = 1U;
    l_desc.Usage = D3D11_USAGE_DEFAULT;
    l_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    l_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    const DXGI_FORMAT l_viewFormat = MakeViewFormat(l_set->m_format, l_set->m_encodeSrgb);
    D3D11_SHADER_RESOURCE_VIEW_DESC l_viewDesc = {};
    l_viewDesc.Format = l_viewFormat;
    l_viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    l_viewDesc.Texture2D.MipLevels = 1U;

    for(int i = 0; i < 3; i++)
    {
        SwapSlot &l_slot = l_set->m_slots[i];
        HRESULT l_result = m_device->CreateTexture2D(&l_desc, NULL, &l_slot.m_texture);
        if(FAILED(l_result))
        {
            LogLine("CDirectModeOutput: swap texture creation failed (slot %d, format %u), hr=0x%08lX", i, l_desc.Format, static_cast<unsigned long>(l_result));
            ReleaseSet(l_set.get());
            return;
        }

        HANDLE l_handle = NULL;
        IDXGIResource *l_resource = nullptr;
        l_result = l_slot.m_texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&l_resource));
        if(SUCCEEDED(l_result))
        {
            l_result = l_resource->GetSharedHandle(&l_handle);
            l_resource->Release();
        }
        if(FAILED(l_result) || (l_handle == NULL))
        {
            LogLine("CDirectModeOutput: shared handle unavailable (slot %d), hr=0x%08lX", i, static_cast<unsigned long>(l_result));
            ReleaseSet(l_set.get());
            return;
        }
        l_slot.m_handle = reinterpret_cast<uint64_t>(l_handle);

        // A typed format the view cannot cast from inherits the texture's own
        // format. A failure here is not fatal: the compositor can still render
        // into the set, this eye just stays black in the mirror
        const bool l_inherit = (l_viewFormat == l_set->m_format);
        l_result = m_device->CreateShaderResourceView(l_slot.m_texture, l_inherit ? NULL : &l_viewDesc, &l_slot.m_view);
        if(FAILED(l_result))
        {
            l_slot.m_view = nullptr;
            if(!m_viewFailureLogged)
            {
                LogLine("CDirectModeOutput: swap format %u cannot be sampled (view format %u), hr=0x%08lX; frames from this set are not mirrored",
                    l_set->m_format, l_viewFormat, static_cast<unsigned long>(l_result));
                m_viewFailureLogged = true;
            }
        }
    }

    for(int i = 0; i < 3; i++) pOutSwapTextureSet->rSharedTextureHandles[i] = l_set->m_slots[i].m_handle;
    pOutSwapTextureSet->unTextureFlags = 0U;
    m_sets.push_back(std::move(l_set));
}

void CDirectModeOutput::ReleaseSet(SwapSet *f_set)
{
    for(SwapSlot &l_slot : f_set->m_slots)
    {
        // A released set may still be referenced by the layers recorded for
        // the frame in flight; drop those so Present skips them
        if(l_slot.m_handle != 0U)
        {
            for(Layer &l_layer : m_layers)
            {
                for(LayerEye &l_eye : l_layer.m_eye)
                {
                    if(l_eye.m_handle == l_slot.m_handle) l_eye.m_handle = 0U;
                }
            }
        }
        SafeRelease(l_slot.m_view);
        SafeRelease(l_slot.m_texture);
        l_slot.m_handle = 0U;
    }
}

bool CDirectModeOutput::FindSlot(vr::SharedTextureHandle_t f_handle, SwapSet *&f_set, uint32_t &f_slot)
{
    if(f_handle == 0U) return false;
    for(auto &l_set : m_sets)
    {
        for(uint32_t i = 0U; i < 3U; i++)
        {
            if(l_set->m_slots[i].m_handle == f_handle)
            {
                f_set = l_set.get();
                f_slot = i;
                return true;
            }
        }
    }
    return false;
}

void CDirectModeOutput::DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    for(auto l_iter = m_sets.begin(); l_iter != m_sets.end(); ++l_iter)
    {
        for(const SwapSlot &l_slot : (*l_iter)->m_slots)
        {
            if(l_slot.m_handle == sharedTextureHandle)
            {
                ReleaseSet(l_iter->get());
                m_sets.erase(l_iter);
                return;
            }
        }
    }
}

void CDirectModeOutput::DestroyAllSwapTextureSets(uint32_t unPid)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    for(auto l_iter = m_sets.begin(); l_iter != m_sets.end();)
    {
        if((*l_iter)->m_pid == unPid)
        {
            ReleaseSet(l_iter->get());
            l_iter = m_sets.erase(l_iter);
        }
        else ++l_iter;
    }
}

void CDirectModeOutput::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2])
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    SwapSet *l_previousSet = nullptr;
    for(int i = 0; i < 2; i++)
    {
        (*pIndices)[i] = 0U;
        SwapSet *l_set = nullptr;
        uint32_t l_slot = 0U;
        if(!FindSlot(sharedTextureHandles[i], l_set, l_slot)) continue;
        if(l_set == l_previousSet)
        {
            // Both eyes in one texture (side-by-side single set): same index
            (*pIndices)[i] = (*pIndices)[i - 1];
            continue;
        }
        (*pIndices)[i] = l_set->m_nextIndex;
        l_set->m_nextIndex = (l_set->m_nextIndex + 1U) % 3U;
        l_previousSet = l_set;
    }
}

// Every layer of the frame is kept, not just the scene: layer 0 is the app,
// the later ones are the SteamVR dashboard and overlays, and in direct mode
// the compositor leaves blending them to the driver. Present draws them in
// submission order, so the window shows what a real headset would.
void CDirectModeOutput::SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2])
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);
    if(m_layerCount < g_maxLayers)
    {
        Layer &l_layer = m_layers[m_layerCount];
        for(int i = 0; i < 2; i++)
        {
            l_layer.m_eye[i].m_handle = perEye[i].hTexture;
            l_layer.m_eye[i].m_bounds = perEye[i].bounds;
        }
        m_layerCount++;
    }
    else if(!m_layerOverflowLogged)
    {
        LogLine("CDirectModeOutput: more than %u layers submitted in one frame; the extra ones are not mirrored", g_maxLayers);
        m_layerOverflowLogged = true;
    }
}

void CDirectModeOutput::ReleaseSyncTexture()
{
    SafeRelease(m_syncMutex);
    SafeRelease(m_syncTexture);
    m_syncHandle = 0U;
}

// The compositor keeps one sync texture; it is opened on its first appearance
// and dropped (and reopened) when a different handle shows up, which is also
// what happens when the compositor recreates it
IDXGIKeyedMutex* CDirectModeOutput::ResolveSyncMutex(vr::SharedTextureHandle_t f_handle)
{
    if((f_handle == 0U) || !m_device) return nullptr;
    if(m_syncHandle == f_handle) return m_syncMutex;

    ReleaseSyncTexture();
    m_syncHandle = f_handle;
    const HRESULT l_result = m_device->OpenSharedResource(reinterpret_cast<HANDLE>(f_handle), __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&m_syncTexture));
    if(FAILED(l_result))
    {
        m_syncTexture = nullptr;
        if(!m_syncFailureLogged)
        {
            LogLine("CDirectModeOutput: opening the compositor sync texture failed, hr=0x%08lX; presenting unsynchronised", static_cast<unsigned long>(l_result));
            m_syncFailureLogged = true;
        }
        return nullptr;
    }
    if(FAILED(m_syncTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&m_syncMutex))))
    {
        m_syncMutex = nullptr;
        if(!m_syncFailureLogged)
        {
            LogLine("CDirectModeOutput: the compositor sync texture has no keyed mutex; presenting unsynchronised");
            m_syncFailureLogged = true;
        }
    }
    return m_syncMutex;
}

void CDirectModeOutput::Present(vr::SharedTextureHandle_t syncTexture)
{
    std::lock_guard<std::mutex> l_lock(m_stateLock);

    Layer l_layers[g_maxLayers];
    const uint32_t l_layerCount = m_layerCount;
    for(uint32_t i = 0U; i < l_layerCount; i++) l_layers[i] = m_layers[i];
    m_layerCount = 0U;
    if(m_presentCount == 0U) LogLine("CDirectModeOutput: first Present (layers=%u scene=%llu/%llu sync=0x%llX)", l_layerCount,
        static_cast<unsigned long long>(l_layers[0].m_eye[0].m_handle), static_cast<unsigned long long>(l_layers[0].m_eye[1].m_handle),
        static_cast<unsigned long long>(syncTexture));
    m_presentCount++;

    m_framePending = false;
    if(m_shuttingDown || !m_resourcesReady || (l_layerCount == 0U)) return;

    // The keyed mutex orders our reads after the compositor's GPU writes to
    // the layer textures. Render even when the acquire fails: the compositor
    // may hold the mutex itself across a frame it already finished, and a
    // possibly torn frame in the mirror beats a frozen one
    IDXGIKeyedMutex *l_syncMutex = ResolveSyncMutex(syncTexture);
    const bool l_acquired = l_syncMutex && (l_syncMutex->AcquireSync(0U, 10U) == S_OK);

    const float l_black[4] = { 0.f, 0.f, 0.f, 1.f };
    m_context->ClearRenderTargetView(m_outputView, l_black);
    m_context->OMSetRenderTargets(1U, &m_outputView, NULL);
    m_context->IASetInputLayout(NULL);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader, NULL, 0U);
    m_context->PSSetShader(m_pixelShader, NULL, 0U);
    m_context->VSSetConstantBuffers(0U, 1U, &m_constants);
    m_context->PSSetConstantBuffers(0U, 1U, &m_constants);
    m_context->PSSetSamplers(0U, 1U, &m_sampler);
    m_context->RSSetState(m_rasterizer);

    // Each eye's submitted bounds -> its half of the output (left eye = left half)
    const float l_halfWidth = static_cast<float>(m_width / 2U);
    for(uint32_t l_index = 0U; l_index < l_layerCount; l_index++)
    {
        m_context->OMSetBlendState((l_index == 0U) ? m_blendOpaque : m_blendAlpha, NULL, 0xFFFFFFFFU);
        for(int i = 0; i < 2; i++)
        {
            const LayerEye &l_source = l_layers[l_index].m_eye[i];
            SwapSet *l_set = nullptr;
            uint32_t l_slotIndex = 0U;
            if(!FindSlot(l_source.m_handle, l_set, l_slotIndex)) continue; // destroyed since submission, or not ours
            const SwapSlot &l_slot = l_set->m_slots[l_slotIndex];
            if(!l_slot.m_view) continue;

            D3D11_MAPPED_SUBRESOURCE l_mapped = {};
            if(FAILED(m_context->Map(m_constants, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &l_mapped))) continue;
            ShaderConstants *l_constants = static_cast<ShaderConstants*>(l_mapped.pData);
            l_constants->m_bounds[0] = l_source.m_bounds.uMin;
            l_constants->m_bounds[1] = l_source.m_bounds.vMin;
            l_constants->m_bounds[2] = l_source.m_bounds.uMax;
            l_constants->m_bounds[3] = l_source.m_bounds.vMax;
            l_constants->m_params[0] = l_set->m_encodeSrgb ? 1.f : 0.f;
            l_constants->m_params[1] = l_constants->m_params[2] = l_constants->m_params[3] = 0.f;
            m_context->Unmap(m_constants, 0U);

            D3D11_VIEWPORT l_viewport = {};
            l_viewport.TopLeftX = static_cast<float>(i) * l_halfWidth;
            l_viewport.Width = l_halfWidth;
            l_viewport.Height = static_cast<float>(m_height);
            l_viewport.MaxDepth = 1.f;
            m_context->RSSetViewports(1U, &l_viewport);
            m_context->PSSetShaderResources(0U, 1U, &l_slot.m_view);
            m_context->Draw(3U, 0U);
        }
    }

    // Unbind the layer texture so a later DestroySwapTextureSet really frees it
    ID3D11ShaderResourceView *l_nullView = nullptr;
    m_context->PSSetShaderResources(0U, 1U, &l_nullView);
    m_context->CopyResource(m_staging[m_frameParity], m_output);
    m_context->Flush();
    if(l_acquired) l_syncMutex->ReleaseSync(0U);
    m_framePending = true;
}

void CDirectModeOutput::PostPresent(const Throttling_t * /* pThrottling */)
{
    {
        std::lock_guard<std::mutex> l_lock(m_stateLock);
        if(m_framePending)
        {
            // Mapping the staging copy waits for the GPU work Present issued
            ID3D11Texture2D *l_staging = m_staging[m_frameParity];
            D3D11_MAPPED_SUBRESOURCE l_mapped = {};
            if(l_staging && SUCCEEDED(m_context->Map(l_staging, 0U, D3D11_MAP_READ, 0U, &l_mapped)))
            {
                // RowPitch may exceed width * 4; consumers expect tightly packed rows
                const size_t l_rowBytes = static_cast<size_t>(m_width) * 4U;
                const uint8_t *l_source = static_cast<const uint8_t*>(l_mapped.pData);
                uint8_t *l_target = m_scratch.data();
                if(l_mapped.RowPitch == l_rowBytes) memcpy(l_target, l_source, l_rowBytes * m_height);
                else
                {
                    for(uint32_t l_row = 0U; l_row < m_height; l_row++)
                    {
                        memcpy(l_target + (l_row * l_rowBytes), l_source + (static_cast<size_t>(l_row) * l_mapped.RowPitch), l_rowBytes);
                    }
                }
                m_publisher.PublishFrame(l_target);
                m_context->Unmap(l_staging, 0U);
            }
            m_framePending = false;
            m_frameParity ^= 1U;
        }
    }

    // "Take more time until vsync": this is the virtual panel's refresh
    SleepToNextVsync();
}

void CDirectModeOutput::SleepToNextVsync()
{
    const double l_interval = 1.0 / static_cast<double>(m_fps);
    const double l_now = GetSeconds();
    if(m_lastVsync == 0.0) m_lastVsync = l_now;

    // Advance in whole intervals; every skipped one is a dropped frame
    double l_next = m_lastVsync + l_interval;
    uint32_t l_dropped = 0U;
    while(l_next <= l_now)
    {
        l_next += l_interval;
        l_dropped++;
    }
    m_droppedLastFrame = (l_dropped > 0U) ? (l_dropped - 1U) : 0U;

    for(;;)
    {
        const double l_remaining = l_next - GetSeconds();
        if(l_remaining <= 0.0) break;
        if(l_remaining > 0.002) Sleep(1U); // 1ms slices, see timeBeginPeriod in the constructor
        else std::this_thread::yield();
    }
    m_lastVsync = l_next;
}

void CDirectModeOutput::GetFrameTiming(vr::DriverDirectMode_FrameTiming *pFrameTiming)
{
    if(pFrameTiming->m_nSize >= sizeof(vr::DriverDirectMode_FrameTiming))
    {
        pFrameTiming->m_nNumFramePresents = 1U;
        pFrameTiming->m_nNumMisPresented = 0U;
        pFrameTiming->m_nNumDroppedFrames = m_droppedLastFrame;
    }
    // Flags overlap VRCompositor_ThrottleMask and must always be cleared
    pFrameTiming->m_nReprojectionFlags = 0U;
}
