#include "stdafx.h"

#include "CDisplayRedirect.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

// CopyResource needs typed formats on the monitor's SRV side; the compositor
// backbuffer arrives typeless.
static DXGI_FORMAT MakeTypedFormat(DXGI_FORMAT f_format)
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
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            l_result = DXGI_FORMAT_R10G10B10A2_UNORM;
            break;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            l_result = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
    }
    return l_result;
}

static void DriverLog(const char *f_text)
{
    vr::IVRDriverLog *l_log = vr::VRDriverLog();
    if(l_log) l_log->Log(f_text);
}

// In-process the compositor hands us a raw COM pointer as the "shared texture handle";
// probe before dereferencing so a real (non-pointer) handle can't crash the process.
// SEH only, no C++ objects (compiler forbids mixing them in one function).
static bool ProbeComPointer(void *f_pointer)
{
    bool l_result = false;
    __try
    {
        void **l_vtable = *reinterpret_cast<void***>(f_pointer);
        volatile void *l_firstEntry = l_vtable[0U];
        (void)l_firstEntry;
        l_result = true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        l_result = false;
    }
    return l_result;
}

CDisplayRedirect::CDisplayRedirect(float f_fps)
{
    m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_serial.assign("asvr_redirect_0001");
    m_fps = (f_fps > 1.f) ? f_fps : 60.f;

    m_device = nullptr;
    m_context = nullptr;
    m_adapterLuid = 0U;
    m_deviceAttempted = false;
    m_deviceAdopted = false;

    m_outputTexture = nullptr;
    m_flushTexture = nullptr;
    m_frameMapping = NULL;
    m_frameInfo = nullptr;
    m_framePending = false;

    LARGE_INTEGER l_frequency;
    QueryPerformanceFrequency(&l_frequency);
    m_qpcToSeconds = 1.0 / static_cast<double>(l_frequency.QuadPart);
    m_lastVsync = 0.0;
    m_vsyncCounter = 0U;

    timeBeginPeriod(1U);
}

CDisplayRedirect::~CDisplayRedirect()
{
    timeEndPeriod(1U);

    for(auto &l_entry : m_backbuffers)
    {
        if(l_entry.second.m_mutex) l_entry.second.m_mutex->Release();
        if(l_entry.second.m_texture) l_entry.second.m_texture->Release();
    }
    m_backbuffers.clear();

    if(m_flushTexture) m_flushTexture->Release();
    if(m_outputTexture) m_outputTexture->Release();
    if(m_frameInfo) UnmapViewOfFile(m_frameInfo);
    if(m_frameMapping) CloseHandle(m_frameMapping);
    if(m_context) m_context->Release();
    if(m_device) m_device->Release();
}

const std::string& CDisplayRedirect::GetSerial() const
{
    return m_serial;
}

double CDisplayRedirect::GetSeconds() const
{
    LARGE_INTEGER l_counter;
    QueryPerformanceCounter(&l_counter);
    return (static_cast<double>(l_counter.QuadPart) * m_qpcToSeconds);
}

// The compositor renders on the discrete GPU; shared textures only open on the
// same adapter, so skip software/Microsoft adapters (RDP remote display) and
// take the biggest hardware GPU. Null when DXGI has no usable adapter.
IDXGIAdapter1* CDisplayRedirect::PickAdapter()
{
    IDXGIFactory1 *l_factory = nullptr;
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&l_factory))))
    {
        DriverLog("CDisplayRedirect: CreateDXGIFactory1 failed");
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

// vrserver's instance only publishes properties (Present goes to the compositor's
// instance), so it needs the adapter LUID but no device: D3D11CreateDevice would
// pull the GPU's user-mode driver into vrserver, a large module load at the very
// moment vrserver initialises graphics for the new HMD on other threads (see
// CServerDriver::RegisterDevices for the crash this took part in).
void CDisplayRedirect::ResolveAdapterLuid()
{
    if(m_adapterLuid != 0U) return;
    IDXGIAdapter1 *l_adapter = PickAdapter();
    if(l_adapter) l_adapter->Release();
}

bool CDisplayRedirect::EnsureDevice()
{
    if(m_device) return true;
    if(m_deviceAttempted) return false; // don't spam device creation every frame after a failure
    m_deviceAttempted = true;

    IDXGIAdapter1 *l_adapter = PickAdapter();
    const HRESULT l_result = D3D11CreateDevice(l_adapter, l_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        NULL, 0U, NULL, 0U, D3D11_SDK_VERSION, &m_device, NULL, &m_context);
    if(l_adapter) l_adapter->Release();
    if(FAILED(l_result))
    {
        char l_line[128];
        snprintf(l_line, sizeof(l_line), "CDisplayRedirect: D3D11CreateDevice failed, hr=0x%08lX", static_cast<unsigned long>(l_result));
        DriverLog(l_line);
        m_device = nullptr;
        m_context = nullptr;
        return false;
    }

    EnsureFrameMapping();

    DriverLog("CDisplayRedirect: D3D11 device created");
    return true;
}

void CDisplayRedirect::EnsureFrameMapping()
{
    if(m_frameInfo) return;

    m_frameMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0U, sizeof(SharedFrameInfo), g_sharedFrameMappingName);
    if(m_frameMapping)
    {
        m_frameInfo = static_cast<SharedFrameInfo*>(MapViewOfFile(m_frameMapping, FILE_MAP_ALL_ACCESS, 0U, 0U, sizeof(SharedFrameInfo)));
        if(m_frameInfo) std::memset(m_frameInfo, 0, sizeof(SharedFrameInfo));
    }
    if(!m_frameInfo) DriverLog("CDisplayRedirect: shared frame mapping unavailable, asvr_monitor will fall back to the compositor mirror");
}

void CDisplayRedirect::AdoptDevice(ID3D11Device *f_device)
{
    if(m_device == f_device) return;

    for(auto &l_entry : m_backbuffers)
    {
        if(l_entry.second.m_mutex) l_entry.second.m_mutex->Release();
        if(l_entry.second.m_texture) l_entry.second.m_texture->Release();
    }
    m_backbuffers.clear();
    if(m_flushTexture)
    {
        m_flushTexture->Release();
        m_flushTexture = nullptr;
    }
    if(m_outputTexture)
    {
        m_outputTexture->Release();
        m_outputTexture = nullptr;
    }
    if(m_context)
    {
        m_context->Release();
        m_context = nullptr;
    }
    if(m_device) m_device->Release();

    m_device = f_device;
    m_device->AddRef();
    m_device->GetImmediateContext(&m_context);
    m_deviceAdopted = true;
    EnsureFrameMapping();
    DriverLog("CDisplayRedirect: adopted compositor D3D11 device");
}

bool CDisplayRedirect::EnsureOutputTexture(const D3D11_TEXTURE2D_DESC &f_desc)
{
    if(m_outputTexture)
    {
        D3D11_TEXTURE2D_DESC l_current;
        m_outputTexture->GetDesc(&l_current);
        if((l_current.Width == f_desc.Width) && (l_current.Height == f_desc.Height) && (l_current.Format == MakeTypedFormat(f_desc.Format))) return true;

        m_outputTexture->Release();
        m_outputTexture = nullptr;
        if(m_flushTexture)
        {
            m_flushTexture->Release();
            m_flushTexture = nullptr;
        }

        // The compositor recreated its swapchain; drop cached backbuffers of the old
        // size so their textures actually release. Same-size entries stay, including
        // the one Present() currently holds a pointer to.
        for(auto iter = m_backbuffers.begin(); iter != m_backbuffers.end();)
        {
            D3D11_TEXTURE2D_DESC l_cachedDesc;
            iter->second.m_texture->GetDesc(&l_cachedDesc);
            if((l_cachedDesc.Width != f_desc.Width) || (l_cachedDesc.Height != f_desc.Height))
            {
                if(iter->second.m_mutex) iter->second.m_mutex->Release();
                iter->second.m_texture->Release();
                iter = m_backbuffers.erase(iter);
            }
            else ++iter;
        }
    }

    D3D11_TEXTURE2D_DESC l_outputDesc = { 0 };
    l_outputDesc.Width = f_desc.Width;
    l_outputDesc.Height = f_desc.Height;
    l_outputDesc.MipLevels = 1U;
    l_outputDesc.ArraySize = 1U;
    l_outputDesc.Format = MakeTypedFormat(f_desc.Format);
    l_outputDesc.SampleDesc.Count = 1U;
    l_outputDesc.Usage = D3D11_USAGE_DEFAULT;
    l_outputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    l_outputDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    if(FAILED(m_device->CreateTexture2D(&l_outputDesc, NULL, &m_outputTexture)))
    {
        DriverLog("CDisplayRedirect: output texture creation failed");
        m_outputTexture = nullptr;
        return false;
    }

    D3D11_TEXTURE2D_DESC l_flushDesc = l_outputDesc;
    l_flushDesc.Width = 1U;
    l_flushDesc.Height = 1U;
    l_flushDesc.Usage = D3D11_USAGE_STAGING;
    l_flushDesc.BindFlags = 0U;
    l_flushDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    l_flushDesc.MiscFlags = 0U;
    if(FAILED(m_device->CreateTexture2D(&l_flushDesc, NULL, &m_flushTexture))) m_flushTexture = nullptr;

    HANDLE l_sharedHandle = NULL;
    IDXGIResource *l_resource = nullptr;
    if(SUCCEEDED(m_outputTexture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&l_resource))))
    {
        l_resource->GetSharedHandle(&l_sharedHandle);
        l_resource->Release();
    }
    if(m_frameInfo)
    {
        m_frameInfo->width = f_desc.Width;
        m_frameInfo->height = f_desc.Height;
        m_frameInfo->sharedHandle = reinterpret_cast<uint64_t>(l_sharedHandle);
        m_frameInfo->magic = g_sharedFrameMagic;
        MemoryBarrier(); // the monitor keys off generation; all fields must land first
        m_frameInfo->generation++;
    }

    char l_line[128];
    snprintf(l_line, sizeof(l_line), "CDisplayRedirect: output texture %ux%u format %d", f_desc.Width, f_desc.Height, l_outputDesc.Format);
    DriverLog(l_line);
    return true;
}

CDisplayRedirect::OpenedBackbuffer* CDisplayRedirect::ResolveBackbuffer(uint64_t f_handle)
{
    auto l_search = m_backbuffers.find(f_handle);
    if(l_search != m_backbuffers.end()) return &l_search->second;

    OpenedBackbuffer l_opened = { nullptr, nullptr };

    // In-process the handle is the compositor's own texture object
    IUnknown *l_unknown = reinterpret_cast<IUnknown*>(f_handle);
    if(ProbeComPointer(l_unknown) && SUCCEEDED(l_unknown->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&l_opened.m_texture))))
    {
        ID3D11Device *l_owningDevice = nullptr;
        l_opened.m_texture->GetDevice(&l_owningDevice);
        if(l_owningDevice)
        {
            AdoptDevice(l_owningDevice);
            l_owningDevice->Release();
        }
        static bool s_loggedPointer = false;
        if(!s_loggedPointer)
        {
            DriverLog("CDisplayRedirect: backbuffer resolved as in-process texture pointer");
            s_loggedPointer = true;
        }
    }
    else
    {
        if(!EnsureDevice()) return nullptr;
        if(FAILED(m_device->OpenSharedResource(reinterpret_cast<HANDLE>(f_handle), __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&l_opened.m_texture))))
        {
            static bool s_loggedFailure = false;
            if(!s_loggedFailure)
            {
                DriverLog("CDisplayRedirect: backbuffer is neither a texture pointer nor an openable shared handle");
                s_loggedFailure = true;
            }
            return nullptr;
        }
    }
    l_opened.m_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void**>(&l_opened.m_mutex));

    auto l_inserted = m_backbuffers.insert(std::make_pair(f_handle, l_opened));
    return &l_inserted.first->second;
}

// vr::ITrackedDeviceServerDriver
vr::EVRInitError CDisplayRedirect::Activate(uint32_t unObjectId)
{
    m_trackedDevice = unObjectId;
    // Only vrserver activates us, and that instance never presents, see ResolveAdapterLuid
    ResolveAdapterLuid();
    EnsureFrameMapping(); // created early so the monitor finds it as soon as it starts
    m_lastVsync = GetSeconds(); // sane baseline before the first WaitForPresent

    vr::PropertyContainerHandle_t l_container = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_trackedDevice);
    vr::VRProperties()->SetStringProperty(l_container, vr::Prop_ModelNumber_String, m_serial.c_str());
    vr::VRProperties()->SetFloatProperty(l_container, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.f);
    if(m_adapterLuid != 0U) vr::VRProperties()->SetUint64Property(l_container, vr::Prop_GraphicsAdapterLuid_Uint64, m_adapterLuid);

    return vr::VRInitError_None;
}

void CDisplayRedirect::Deactivate()
{
    m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
}

void CDisplayRedirect::EnterStandby()
{
}

void* CDisplayRedirect::GetComponent(const char *pchComponentNameAndVersion)
{
    void *l_result = nullptr;
    if(!strcmp(pchComponentNameAndVersion, vr::IVRVirtualDisplay_Version)) l_result = dynamic_cast<vr::IVRVirtualDisplay*>(this);

    char l_line[192];
    snprintf(l_line, sizeof(l_line), "CDisplayRedirect: GetComponent('%s') -> %s", pchComponentNameAndVersion, l_result ? "provided" : "null");
    DriverLog(l_line);
    return l_result;
}

void CDisplayRedirect::DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if(unResponseBufferSize > 0U) pchResponseBuffer[0U] = '\0';
}

vr::DriverPose_t CDisplayRedirect::GetPose()
{
    // Deprecated for this device class, never called
    vr::DriverPose_t l_pose = { 0 };
    l_pose.qWorldFromDriverRotation.w = 1.0;
    l_pose.qDriverFromHeadRotation.w = 1.0;
    l_pose.qRotation.w = 1.0;
    l_pose.deviceIsConnected = true;
    l_pose.poseIsValid = true;
    l_pose.result = vr::TrackingResult_Running_OK;
    return l_pose;
}

// vr::IVRVirtualDisplay
void CDisplayRedirect::Present(const vr::PresentInfo_t *pPresentInfo, uint32_t unPresentInfoSize)
{
    static bool s_logged = false;
    if(!s_logged)
    {
        char l_line[192];
        snprintf(l_line, sizeof(l_line), "CDisplayRedirect: first Present, infoSize=%u handle=0x%llX", unPresentInfoSize,
            pPresentInfo ? static_cast<unsigned long long>(pPresentInfo->backbufferTextureHandle) : 0ULL);
        DriverLog(l_line);
        s_logged = true;
    }
    if(!pPresentInfo || (unPresentInfoSize < sizeof(vr::SharedTextureHandle_t))) return;

    OpenedBackbuffer *l_backbuffer = ResolveBackbuffer(pPresentInfo->backbufferTextureHandle);
    if(!l_backbuffer) return;

    D3D11_TEXTURE2D_DESC l_desc;
    l_backbuffer->m_texture->GetDesc(&l_desc);
    if(!EnsureOutputTexture(l_desc)) return;

    // Copy even when the acquire fails: in-process the compositor already synchronized
    // this texture and may itself hold the keyed mutex
    bool l_locked = false;
    if(l_backbuffer->m_mutex) l_locked = (l_backbuffer->m_mutex->AcquireSync(0U, 10U) == S_OK);
    if(l_desc.SampleDesc.Count > 1U) m_context->ResolveSubresource(m_outputTexture, 0U, l_backbuffer->m_texture, 0U, MakeTypedFormat(l_desc.Format));
    else m_context->CopyResource(m_outputTexture, l_backbuffer->m_texture);
    if(m_flushTexture)
    {
        // 1x1 staging copy: mapped in WaitForPresent to block until the GPU is done
        D3D11_BOX l_box = { 0U, 0U, 0U, 1U, 1U, 1U };
        m_context->CopySubresourceRegion(m_flushTexture, 0U, 0U, 0U, 0U, m_outputTexture, 0U, &l_box);
    }
    if(l_locked) l_backbuffer->m_mutex->ReleaseSync(0U);
    m_context->Flush();
    m_framePending = true;
}

void CDisplayRedirect::WaitForPresent()
{
    static bool s_logged = false;
    if(!s_logged)
    {
        DriverLog("CDisplayRedirect: first WaitForPresent");
        s_logged = true;
    }
    if(m_framePending)
    {
        if(m_flushTexture && m_context)
        {
            D3D11_MAPPED_SUBRESOURCE l_mapped = { 0 };
            if(SUCCEEDED(m_context->Map(m_flushTexture, 0U, D3D11_MAP_READ, 0U, &l_mapped))) m_context->Unmap(m_flushTexture, 0U);
        }
        if(m_frameInfo) m_frameInfo->frameCounter++;
        m_framePending = false;
    }

    // Simulated vsync: advance in whole frame intervals and sleep to the boundary
    const double l_interval = 1.0 / static_cast<double>(m_fps);
    double l_now = GetSeconds();
    if(m_lastVsync == 0.0) m_lastVsync = l_now - l_interval;

    int32_t l_frames = static_cast<int32_t>((l_now - m_lastVsync) / l_interval);
    if(l_frames < 0) l_frames = 0;
    l_frames += 1;
    const double l_nextVsync = m_lastVsync + (l_interval * static_cast<double>(l_frames));

    for(;;)
    {
        const double l_remaining = l_nextVsync - GetSeconds();
        if(l_remaining <= 0.0) break;
        if(l_remaining > 0.002) Sleep(1U);
        else std::this_thread::yield();
    }

    m_lastVsync = l_nextVsync;
    m_vsyncCounter += static_cast<uint64_t>(l_frames);
}

bool CDisplayRedirect::GetTimeSinceLastVsync(float *pfSecondsSinceLastVsync, uint64_t *pulFrameCounter)
{
    static bool s_logged = false;
    if(!s_logged)
    {
        DriverLog("CDisplayRedirect: first GetTimeSinceLastVsync");
        s_logged = true;
    }
    if(m_lastVsync == 0.0) m_lastVsync = GetSeconds(); // no Activate in the compositor process
    if(pfSecondsSinceLastVsync) *pfSecondsSinceLastVsync = static_cast<float>(GetSeconds() - m_lastVsync);
    if(pulFrameCounter) *pulFrameCounter = m_vsyncCounter;
    return true;
}
