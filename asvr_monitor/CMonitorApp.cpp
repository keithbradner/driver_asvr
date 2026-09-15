#include "stdafx.h"
#include "CMonitorApp.h"
#include "../shared/SharedFrameInfo.h"

#include <cstdio>

const glm::vec3 g_zeroPoint3(0.f);
const glm::vec4 g_zeroPoint4(0.f, 0.f, 0.f, 1.f);
const glm::quat g_zeroRotation(1.f, 0.f, 0.f, 0.f);
const glm::vec3 g_axisX(1.f, 0.f, 0.f);
const glm::vec3 g_axisY(0.f, 1.f, 0.f);
const glm::vec3 g_axisZ(0.f, 0.f, -1.f);

const wchar_t g_windowClassName[] = L"AsvrMonitorWindow";
const char* const g_controllerSerial = "asvr_controller_0001"; // virtual right hand
const char* const g_leftControllerSerial = "asvr_controller_0002"; // virtual left hand

const float g_mouseSense = 0.001f; // radians per raw mouse count
const float g_moveSpeed = 2.f; // meters per second
const float g_ctrlMoveSpeed = 0.5f; // meters per second, controller offset
const float g_keyRotateSpeed = glm::pi<float>() / 4.f; // radians per second (numpad)
const float g_pitchLimit = glm::radians(89.f);

// Head-local controller offsets at startup and after a reset, [0] = right,
// [1] = left. Must match the driver's CControllerDevice constructor defaults
const glm::vec3 g_ctrlDefaultOffset[2U] = { glm::vec3(0.15f, -0.15f, -0.35f), glm::vec3(-0.15f, -0.15f, -0.35f) };

static void LogLine(const char *f_text)
{
    char l_path[MAX_PATH];
    if(GetTempPathA(MAX_PATH, l_path) != 0U)
    {
        std::string l_file(l_path);
        l_file.append("asvr_monitor.log");
        // Fresh log every run
        static bool s_appending = false;
        FILE *l_stream = nullptr;
        if(fopen_s(&l_stream, l_file.c_str(), s_appending ? "a" : "w") == 0)
        {
            s_appending = true;
            SYSTEMTIME l_time;
            GetLocalTime(&l_time);
            fprintf(l_stream, "[%02u:%02u:%02u.%03u] %s\n", l_time.wHour, l_time.wMinute, l_time.wSecond, l_time.wMilliseconds, f_text);
            fclose(l_stream);
        }
    }
}

static void LogError(const char *f_where, long f_code)
{
    char l_line[256];
    snprintf(l_line, sizeof(l_line), "%s failed, hr=0x%08lX", f_where, static_cast<unsigned long>(f_code));
    LogLine(l_line);
}

// OpenVR 3x4 row-major -> glm column-major, bottom row filled in as (0 0 0 1)
static glm::mat4 ToMat4(const vr::HmdMatrix34_t &f_mat)
{
    return glm::mat4(
        glm::vec4(f_mat.m[0][0], f_mat.m[1][0], f_mat.m[2][0], 0.f),
        glm::vec4(f_mat.m[0][1], f_mat.m[1][1], f_mat.m[2][1], 0.f),
        glm::vec4(f_mat.m[0][2], f_mat.m[1][2], f_mat.m[2][2], 0.f),
        glm::vec4(f_mat.m[0][3], f_mat.m[1][3], f_mat.m[2][3], 1.f));
}

// Space-terminated float sequence for the driver's DebugRequest text protocol
static void AppendFloats(std::string &f_out, const float *f_values, int f_count)
{
    char l_buffer[32];
    for(int i = 0; i < f_count; i++)
    {
        snprintf(l_buffer, sizeof(l_buffer), "%f ", f_values[i]);
        f_out.append(l_buffer);
    }
}

const char g_shaderSource[] =
"Texture2D g_texture : register(t0);\n"
"SamplerState g_sampler : register(s0);\n"
"struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
"VSOut VSMain(uint id : SV_VertexID)\n"
"{\n"
"    VSOut o;\n"
"    float2 uv = float2((id << 1) & 2, id & 2);\n"
"    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
"    o.uv = uv * float2(0.5, 1.0); // mirror texture holds both eyes side by side, show the left one\n"
"    return o;\n"
"}\n"
"float4 PSMain(VSOut i) : SV_Target\n"
"{\n"
"    return g_texture.Sample(g_sampler, i.uv);\n"
"}\n";

CMonitorApp::CMonitorApp()
{
    m_active = false;

    m_vrSystem = nullptr;
    m_controllerDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_leftControllerDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_event = { 0 };

    m_window = NULL;
    m_focused = false;
    m_captured = false;
    m_mouseDeltaX = 0;
    m_mouseDeltaY = 0;
    m_sizeChanged = false;
    for(int i = 0; i < KB_Count; i++) m_keyBindings[i] = g_keyBindingDefs[i].m_defaultKey;
    for(int i = 0; i < KB_Count; i++) m_actionHeld[i] = false;
    m_activeControllerLeft = false;
    m_mouseAimController = false;
    m_captureGuard = false;
    m_lastJoystick[0U] = 0.f;
    m_lastJoystick[1U] = 0.f;
    m_absMouseSeen = false;
    m_absMouseValid = false;
    m_absMouseX = 0;
    m_absMouseY = 0;

    m_device = nullptr;
    m_context = nullptr;
    m_swapChain = nullptr;
    m_renderTarget = nullptr;
    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    m_sampler = nullptr;
    m_mirrorView = nullptr;

    m_frameMapping = NULL;
    m_frameInfoView = nullptr;
    m_frameGeneration = 0U;
    m_frameMapRetry = 0;
    m_sharedFrameView = nullptr;

    m_position = g_zeroPoint3;
    m_yaw = 0.f;
    m_pitch = 0.f;
    m_roll = 0.f;

    for(size_t i = 0U; i < 2U; i++)
    {
        m_ctrlOffsetPos[i] = g_ctrlDefaultOffset[i];
        m_ctrlOffsetRot[i] = g_zeroRotation;
    }
}
CMonitorApp::~CMonitorApp()
{
}

bool CMonitorApp::Init()
{
    if(!m_active)
    {
        vr::EVRInitError l_error;
        // Background gives access to the compositor (mirror texture); Utility does not.
        // Background requires a running vrserver, which is the case when the driver launches us.
        m_vrSystem = vr::VR_Init(&l_error, vr::VRApplication_Background);
        if(l_error != vr::VRInitError_None) m_vrSystem = vr::VR_Init(&l_error, vr::VRApplication_Utility);
        if(l_error == vr::VRInitError_None)
        {
            FindControllerDevice();
            LoadKeyBindings();

            // Adopt the HMD's current pose as our authoritative state
            vr::TrackedDevicePose_t l_poses[vr::k_unMaxTrackedDeviceCount];
            m_vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated, 0.f, l_poses, vr::k_unMaxTrackedDeviceCount);
            if(l_poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
            {
                const glm::mat4 l_headTransform = ToMat4(l_poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
                m_position = l_headTransform*g_zeroPoint4;
                SetOrientationFromQuat(glm::quat_cast(l_headTransform));
            }

            if(CreateMainWindow())
            {
                CreateRenderer(); // window still works for input without a renderer
                m_lastTick = std::chrono::steady_clock::now();
                m_active = true;
            }
            else
            {
                vr::VR_Shutdown();
                m_vrSystem = nullptr;
            }
        }
    }

    return m_active;
}

void CMonitorApp::Shutdown()
{
    if(m_active)
    {
        if(m_captured) ReleaseMouse();
        DestroyRenderer();
        if(m_window)
        {
            DestroyWindow(m_window);
            m_window = NULL;
        }
        UnregisterClassW(g_windowClassName, GetModuleHandleW(NULL));

        if(m_frameInfoView)
        {
            UnmapViewOfFile(m_frameInfoView);
            m_frameInfoView = nullptr;
        }
        if(m_frameMapping)
        {
            CloseHandle(m_frameMapping);
            m_frameMapping = NULL;
        }

        vr::VR_Shutdown();
        m_vrSystem = nullptr;

        m_active = false;
    }
}

// Window
LRESULT CALLBACK CMonitorApp::WindowProcStatic(HWND f_window, UINT f_message, WPARAM f_wParam, LPARAM f_lParam)
{
    CMonitorApp *l_core = nullptr;
    if(f_message == WM_NCCREATE)
    {
        l_core = static_cast<CMonitorApp*>(reinterpret_cast<CREATESTRUCTW*>(f_lParam)->lpCreateParams);
        SetWindowLongPtrW(f_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(l_core));
        l_core->m_window = f_window;
    }
    else l_core = reinterpret_cast<CMonitorApp*>(GetWindowLongPtrW(f_window, GWLP_USERDATA));

    LRESULT l_result;
    if(l_core) l_result = l_core->WindowProc(f_message, f_wParam, f_lParam);
    else l_result = DefWindowProcW(f_window, f_message, f_wParam, f_lParam);
    return l_result;
}

LRESULT CMonitorApp::WindowProc(UINT f_message, WPARAM f_wParam, LPARAM f_lParam)
{
    LRESULT l_result = 0;
    switch(f_message)
    {
        case WM_SETFOCUS:
            m_focused = true;
            break;
        case WM_KILLFOCUS:
        {
            m_focused = false;
            if(m_captured) ReleaseMouse();
        } break;

        case WM_LBUTTONDOWN:
        {
            // Once captured, mouse buttons are handled as polled key bindings
            if(!m_captured)
            {
                CaptureMouse();
                m_captureGuard = true; // this press must not fire the trigger binding
            }
        } break;

        case WM_INPUT:
        {
            RAWINPUT l_rawInput;
            UINT l_rawSize = sizeof(RAWINPUT);
            if(GetRawInputData(reinterpret_cast<HRAWINPUT>(f_lParam), RID_INPUT, &l_rawInput, &l_rawSize, sizeof(RAWINPUTHEADER)) != UINT(-1))
            {
                if(l_rawInput.header.dwType == RIM_TYPEMOUSE)
                {
                    const RAWMOUSE &l_mouse = l_rawInput.data.mouse;
                    if(l_mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
                    {
                        // RDP (and other injected input) reports absolute positions
                        // normalized to 0..65535 instead of deltas; derive deltas from
                        // consecutive samples
                        const bool l_virtualDesktop = ((l_mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0);
                        const int l_screenWidth = GetSystemMetrics(l_virtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
                        const int l_screenHeight = GetSystemMetrics(l_virtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
                        const long l_absX = static_cast<long>((static_cast<double>(l_mouse.lLastX) / 65535.0) * l_screenWidth);
                        const long l_absY = static_cast<long>((static_cast<double>(l_mouse.lLastY) / 65535.0) * l_screenHeight);
                        if(m_absMouseValid && m_captured && m_focused)
                        {
                            const long l_deltaX = l_absX - m_absMouseX;
                            const long l_deltaY = l_absY - m_absMouseY;
                            // A recenter warp arrives as one huge jump; real per-sample
                            // movement never gets near this
                            if((labs(l_deltaX) < 200) && (labs(l_deltaY) < 200))
                            {
                                m_mouseDeltaX += l_deltaX;
                                m_mouseDeltaY += l_deltaY;
                            }
                        }
                        m_absMouseX = l_absX;
                        m_absMouseY = l_absY;
                        m_absMouseValid = true;
                        m_absMouseSeen = true;
                    }
                    else if(m_captured && m_focused)
                    {
                        m_mouseDeltaX += l_mouse.lLastX;
                        m_mouseDeltaY += l_mouse.lLastY;
                    }
                }
            }
            l_result = DefWindowProcW(m_window, f_message, f_wParam, f_lParam);
        } break;

        case WM_KEYDOWN:
        {
            if(f_wParam == VK_ESCAPE)
            {
                if(m_captured) ReleaseMouse();
            }
        } break;

        case WM_SIZE:
        {
            m_sizeChanged = true;
            if(m_captured) UpdateCursorClip();
        } break;

        case WM_CLOSE:
            DestroyWindow(m_window);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            l_result = DefWindowProcW(m_window, f_message, f_wParam, f_lParam);
            break;
    }
    return l_result;
}

bool CMonitorApp::CreateMainWindow()
{
    HINSTANCE l_instance = GetModuleHandleW(NULL);

    WNDCLASSEXW l_windowClass = { 0 };
    l_windowClass.cbSize = sizeof(WNDCLASSEXW);
    l_windowClass.style = CS_HREDRAW | CS_VREDRAW;
    l_windowClass.lpfnWndProc = &CMonitorApp::WindowProcStatic;
    l_windowClass.hInstance = l_instance;
    l_windowClass.hIcon = LoadIconW(l_instance, MAKEINTRESOURCEW(101));
    l_windowClass.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    l_windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    l_windowClass.lpszClassName = g_windowClassName;
    if(!RegisterClassExW(&l_windowClass)) return false;

    RECT l_windowRect = { 0, 0, 1280, 720 };
    AdjustWindowRect(&l_windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND l_window = CreateWindowExW(0, g_windowClassName, L"ASVR", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, l_windowRect.right - l_windowRect.left, l_windowRect.bottom - l_windowRect.top,
        NULL, NULL, l_instance, this);
    if(!l_window) return false;
    m_window = l_window;

    RAWINPUTDEVICE l_rawDevice;
    l_rawDevice.usUsagePage = 0x01; // generic desktop
    l_rawDevice.usUsage = 0x02; // mouse
    l_rawDevice.dwFlags = 0;
    l_rawDevice.hwndTarget = m_window;
    RegisterRawInputDevices(&l_rawDevice, 1, sizeof(RAWINPUTDEVICE));

    UpdateTitle();
    ShowWindow(m_window, SW_SHOW);
    UpdateWindow(m_window);
    return true;
}

void CMonitorApp::CaptureMouse()
{
    if(!m_captured)
    {
        m_captured = true;
        ShowCursor(FALSE);
        UpdateCursorClip();
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
        UpdateTitle();
    }
}

void CMonitorApp::ReleaseMouse()
{
    if(m_captured)
    {
        m_captured = false;
        ClipCursor(NULL);
        ShowCursor(TRUE);
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
        m_captureGuard = false;
        ReleaseHeldActions();
        m_actionHeld[KB_SwitchController] = false;
        m_actionHeld[KB_MouseAim] = false;
        m_actionHeld[KB_ResetHeadPose] = false;
        m_actionHeld[KB_ResetControllers] = false;
        UpdateTitle();
    }
}

void CMonitorApp::UpdateCursorClip()
{
    RECT l_clientRect;
    GetClientRect(m_window, &l_clientRect);
    POINT l_topLeft = { l_clientRect.left, l_clientRect.top };
    POINT l_bottomRight = { l_clientRect.right, l_clientRect.bottom };
    ClientToScreen(m_window, &l_topLeft);
    ClientToScreen(m_window, &l_bottomRight);
    RECT l_clipRect = { l_topLeft.x, l_topLeft.y, l_bottomRight.x, l_bottomRight.y };
    ClipCursor(&l_clipRect);
}

void CMonitorApp::UpdateTitle()
{
    const wchar_t *l_title = L"ASVR - click to capture mouse";
    if(m_captured)
    {
        if(m_mouseAimController) l_title = (m_activeControllerLeft ? L"ASVR - LEFT controller, mouse aims it (Tab switches, Backspace to head, Esc releases)" : L"ASVR - RIGHT controller, mouse aims it (Tab switches, Backspace to head, Esc releases)");
        else l_title = (m_activeControllerLeft ? L"ASVR - LEFT controller, mouse looks (Tab switches, Backspace to aim, Esc releases)" : L"ASVR - RIGHT controller, mouse looks (Tab switches, Backspace to aim, Esc releases)");
    }
    SetWindowTextW(m_window, l_title);
}

// D3D11
bool CMonitorApp::CreateRenderer()
{
    DXGI_SWAP_CHAIN_DESC l_swapDesc = { 0 };
    l_swapDesc.BufferCount = 2;
    l_swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    l_swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    l_swapDesc.OutputWindow = m_window;
    l_swapDesc.SampleDesc.Count = 1;
    l_swapDesc.Windowed = TRUE;
    l_swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    // Create the device on the same GPU as the VR compositor. On headless/RDP machines the
    // default adapter is the Microsoft remote display adapter, and opening the compositor's
    // shared mirror texture cross-adapter yields garbage (green screen).
    // Preferred: exact LUID from GetOutputDevice; fallback: biggest non-Microsoft hardware GPU.
    IDXGIAdapter1 *l_adapter = nullptr;
    IDXGIFactory1 *l_factory = nullptr;
    if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&l_factory))))
    {
        uint64_t l_compositorLuid = 0U;
        m_vrSystem->GetOutputDevice(&l_compositorLuid, vr::TextureType_DirectX);

        bool l_exactMatch = false;
        SIZE_T l_bestMemory = 0U;
        for(UINT i = 0U; !l_exactMatch; i++)
        {
            IDXGIAdapter1 *l_candidate = nullptr;
            if(l_factory->EnumAdapters1(i, &l_candidate) != S_OK) break;

            DXGI_ADAPTER_DESC1 l_desc;
            l_candidate->GetDesc1(&l_desc);
            const uint64_t l_candidateLuid = (static_cast<uint64_t>(static_cast<uint32_t>(l_desc.AdapterLuid.HighPart)) << 32) | l_desc.AdapterLuid.LowPart;
            const bool l_softwareLike = ((l_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U) || (l_desc.VendorId == 0x1414U); // 0x1414 = Microsoft (basic render / remote display)

            bool l_take = false;
            if((l_compositorLuid != 0U) && (l_candidateLuid == l_compositorLuid))
            {
                l_take = true;
                l_exactMatch = true;
            }
            else if(!l_softwareLike && (l_desc.DedicatedVideoMemory > l_bestMemory))
            {
                l_take = true;
                l_bestMemory = l_desc.DedicatedVideoMemory;
            }

            if(l_take)
            {
                if(l_adapter) l_adapter->Release();
                l_adapter = l_candidate;
            }
            else l_candidate->Release();
        }
        l_factory->Release();

        if(l_adapter)
        {
            DXGI_ADAPTER_DESC1 l_chosenDesc;
            l_adapter->GetDesc1(&l_chosenDesc);
            char l_line[320];
            snprintf(l_line, sizeof(l_line), "Renderer adapter: %ls (compositor LUID %s, luid=0x%llX)",
                l_chosenDesc.Description, l_exactMatch ? "matched" : "not matched - using biggest hardware GPU",
                (static_cast<unsigned long long>(static_cast<uint32_t>(l_chosenDesc.AdapterLuid.HighPart)) << 32) | l_chosenDesc.AdapterLuid.LowPart);
            LogLine(l_line);
        }
        else LogLine("Renderer adapter: none matched, using default adapter");
    }

    HRESULT l_result = D3D11CreateDeviceAndSwapChain(l_adapter, l_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &l_swapDesc, &m_swapChain, &m_device, NULL, &m_context);
    if(l_adapter) l_adapter->Release();
    if(FAILED(l_result))
    {
        LogError("D3D11CreateDeviceAndSwapChain", l_result);
        return false;
    }

    ID3DBlob *l_vsBlob = nullptr;
    ID3DBlob *l_psBlob = nullptr;
    ID3DBlob *l_errorBlob = nullptr;
    l_result = D3DCompile(g_shaderSource, sizeof(g_shaderSource) - 1, NULL, NULL, NULL, "VSMain", "vs_4_0", 0, 0, &l_vsBlob, &l_errorBlob);
    if(FAILED(l_result))
    {
        LogError("D3DCompile(VSMain)", l_result);
        if(l_errorBlob) l_errorBlob->Release();
        DestroyRenderer();
        return false;
    }
    l_result = D3DCompile(g_shaderSource, sizeof(g_shaderSource) - 1, NULL, NULL, NULL, "PSMain", "ps_4_0", 0, 0, &l_psBlob, &l_errorBlob);
    if(FAILED(l_result))
    {
        LogError("D3DCompile(PSMain)", l_result);
        if(l_errorBlob) l_errorBlob->Release();
        l_vsBlob->Release();
        DestroyRenderer();
        return false;
    }
    m_device->CreateVertexShader(l_vsBlob->GetBufferPointer(), l_vsBlob->GetBufferSize(), NULL, &m_vertexShader);
    m_device->CreatePixelShader(l_psBlob->GetBufferPointer(), l_psBlob->GetBufferSize(), NULL, &m_pixelShader);
    l_vsBlob->Release();
    l_psBlob->Release();

    D3D11_SAMPLER_DESC l_samplerDesc;
    ZeroMemory(&l_samplerDesc, sizeof(D3D11_SAMPLER_DESC));
    l_samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    l_samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    l_samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    l_samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    l_samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&l_samplerDesc, &m_sampler);

    m_sizeChanged = true; // create the render target on first frame
    return true;
}

void CMonitorApp::DestroyRenderer()
{
    if(m_sharedFrameView)
    {
        m_sharedFrameView->Release();
        m_sharedFrameView = nullptr;
        m_frameGeneration = 0U; // reopen against the new device
    }
    if(m_mirrorView)
    {
        vr::IVRCompositor *l_compositor = vr::VRCompositor();
        if(l_compositor) l_compositor->ReleaseMirrorTextureD3D11(m_mirrorView);
        m_mirrorView = nullptr;
    }
    if(m_sampler)
    {
        m_sampler->Release();
        m_sampler = nullptr;
    }
    if(m_pixelShader)
    {
        m_pixelShader->Release();
        m_pixelShader = nullptr;
    }
    if(m_vertexShader)
    {
        m_vertexShader->Release();
        m_vertexShader = nullptr;
    }
    if(m_renderTarget)
    {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
    if(m_swapChain)
    {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
    if(m_context)
    {
        m_context->Release();
        m_context = nullptr;
    }
    if(m_device)
    {
        m_device->Release();
        m_device = nullptr;
    }
}

void CMonitorApp::UpdateSharedFrame()
{
    if(!m_frameInfoView)
    {
        // The driver publishes the mapping once the compositor starts presenting
        if(++m_frameMapRetry < 60) return;
        m_frameMapRetry = 0;

        if(!m_frameMapping) m_frameMapping = OpenFileMappingA(FILE_MAP_READ, FALSE, g_sharedFrameMappingName);
        if(m_frameMapping) m_frameInfoView = MapViewOfFile(m_frameMapping, FILE_MAP_READ, 0U, 0U, sizeof(SharedFrameInfo));
        if(!m_frameInfoView) return;
    }

    const SharedFrameInfo *l_info = static_cast<const SharedFrameInfo*>(m_frameInfoView);
    if(l_info->magic != g_sharedFrameMagic) return;
    if((l_info->generation == m_frameGeneration) || (l_info->sharedHandle == 0U)) return;

    if(m_sharedFrameView)
    {
        m_sharedFrameView->Release();
        m_sharedFrameView = nullptr;
    }

    ID3D11Texture2D *l_texture = nullptr;
    HRESULT l_openResult = m_device->OpenSharedResource(reinterpret_cast<HANDLE>(l_info->sharedHandle), __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&l_texture));
    if(FAILED(l_openResult))
    {
        LogError("OpenSharedResource (driver frame)", l_openResult);
        m_frameGeneration = l_info->generation; // don't retry a broken handle every frame
        return;
    }

    HRESULT l_viewResult = m_device->CreateShaderResourceView(l_texture, NULL, &m_sharedFrameView);
    if(FAILED(l_viewResult))
    {
        LogError("CreateShaderResourceView (driver frame)", l_viewResult);
        m_sharedFrameView = nullptr;
    }
    else LogLine("Using driver shared frame texture");
    l_texture->Release();
    m_frameGeneration = l_info->generation;
}

void CMonitorApp::RenderFrame()
{
    if(!m_swapChain)
    {
        // Renderer never came up (or died), retry every ~2 seconds
        static int s_retryDelay = 0;
        if(++s_retryDelay >= 120)
        {
            s_retryDelay = 0;
            CreateRenderer();
        }
        if(!m_swapChain) return;
    }

    if(m_sizeChanged || !m_renderTarget)
    {
        m_context->OMSetRenderTargets(0, NULL, NULL);
        if(m_renderTarget)
        {
            m_renderTarget->Release();
            m_renderTarget = nullptr;
        }
        HRESULT l_resizeResult = m_swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        if(FAILED(l_resizeResult)) LogError("ResizeBuffers", l_resizeResult);
        ID3D11Texture2D *l_backBuffer = nullptr;
        HRESULT l_bufferResult = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&l_backBuffer));
        if(SUCCEEDED(l_bufferResult))
        {
            HRESULT l_rtvResult = m_device->CreateRenderTargetView(l_backBuffer, NULL, &m_renderTarget);
            if(FAILED(l_rtvResult)) LogError("CreateRenderTargetView", l_rtvResult);
            l_backBuffer->Release();
        }
        else LogError("GetBuffer", l_bufferResult);
        m_sizeChanged = false;
    }
    if(!m_renderTarget) return;

    UpdateSharedFrame();

    if(!m_sharedFrameView && !m_mirrorView)
    {
        // Compositor may not be ready yet (or unavailable in Utility fallback), retry every frame
        vr::IVRCompositor *l_compositor = vr::VRCompositor();
        if(l_compositor) l_compositor->GetMirrorTextureD3D11(vr::Eye_Left, m_device, reinterpret_cast<void**>(&m_mirrorView));
    }

    RECT l_clientRect;
    GetClientRect(m_window, &l_clientRect);
    const float l_clientWidth = static_cast<float>(l_clientRect.right - l_clientRect.left);
    const float l_clientHeight = static_cast<float>(l_clientRect.bottom - l_clientRect.top);

    // Driver projection is symmetric -1..1 (square FOV), letterbox into a centered square
    const float l_side = (l_clientWidth < l_clientHeight) ? l_clientWidth : l_clientHeight;
    D3D11_VIEWPORT l_viewport = { 0 };
    l_viewport.TopLeftX = (l_clientWidth - l_side)*0.5f;
    l_viewport.TopLeftY = (l_clientHeight - l_side)*0.5f;
    l_viewport.Width = l_side;
    l_viewport.Height = l_side;
    l_viewport.MaxDepth = 1.f;

    const float l_clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
    m_context->OMSetRenderTargets(1, &m_renderTarget, NULL);
    m_context->ClearRenderTargetView(m_renderTarget, l_clearColor);
    m_context->RSSetViewports(1, &l_viewport);

    ID3D11ShaderResourceView *l_sourceView = (m_sharedFrameView ? m_sharedFrameView : m_mirrorView);
    if(l_sourceView && m_vertexShader && m_pixelShader)
    {
        m_context->IASetInputLayout(NULL);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vertexShader, NULL, 0);
        m_context->PSSetShader(m_pixelShader, NULL, 0);
        m_context->PSSetShaderResources(0, 1, &l_sourceView);
        m_context->PSSetSamplers(0, 1, &m_sampler);
        m_context->Draw(3, 0);

        ID3D11ShaderResourceView *l_nullView = nullptr;
        m_context->PSSetShaderResources(0, 1, &l_nullView);
    }

    HRESULT l_presentResult = m_swapChain->Present(1, 0);
    if((l_presentResult == DXGI_ERROR_DEVICE_REMOVED) || (l_presentResult == DXGI_ERROR_DEVICE_RESET))
    {
        LogError("Present (device lost, recreating)", l_presentResult);
        DestroyRenderer();
    }
    else if(FAILED(l_presentResult)) LogError("Present", l_presentResult);
}

// VR communication
glm::quat CMonitorApp::GetOrientation() const
{
    return glm::angleAxis(m_yaw, g_axisY)*glm::angleAxis(m_pitch, g_axisX)*glm::angleAxis(m_roll, g_axisZ);
}

void CMonitorApp::SetOrientationFromQuat(const glm::quat &f_rot)
{
    const glm::vec3 l_forward = f_rot*g_axisZ;
    m_yaw = std::atan2(-l_forward.x, -l_forward.z);
    m_pitch = std::asin(glm::clamp(l_forward.y, -1.f, 1.f));
    m_pitch = glm::clamp(m_pitch, -g_pitchLimit, g_pitchLimit);

    const glm::quat l_residual = glm::inverse(glm::angleAxis(m_yaw, g_axisY)*glm::angleAxis(m_pitch, g_axisX))*f_rot;
    m_roll = -glm::roll(l_residual);
}

void CMonitorApp::SendHmdPose()
{
    const glm::quat l_rotation = GetOrientation();
    const float l_pose[7] = { m_position.x, m_position.y, m_position.z, l_rotation.x, l_rotation.y, l_rotation.z, l_rotation.w };

    std::string l_message("setpose ");
    AppendFloats(l_message, l_pose, 7);

    char l_response[32U];
    std::memset(l_response, 0, 32U);
    vr::VRDebug()->DriverDebugRequest(vr::k_unTrackedDeviceIndex_Hmd, l_message.c_str(), l_response, 32U);
}

vr::TrackedDeviceIndex_t CMonitorApp::ActiveControllerIndex() const
{
    return (m_activeControllerLeft ? m_leftControllerDevice : m_controllerDevice);
}

void CMonitorApp::SendControllerOffset(bool f_left)
{
    const vr::TrackedDeviceIndex_t l_device = (f_left ? m_leftControllerDevice : m_controllerDevice);
    if(l_device != vr::k_unTrackedDeviceIndexInvalid)
    {
        const glm::vec3 &l_pos = m_ctrlOffsetPos[f_left ? 1U : 0U];
        const glm::quat &l_rot = m_ctrlOffsetRot[f_left ? 1U : 0U];
        const float l_offset[7] = { l_pos.x, l_pos.y, l_pos.z, l_rot.x, l_rot.y, l_rot.z, l_rot.w };

        std::string l_message("offset ");
        AppendFloats(l_message, l_offset, 7);

        char l_response[32U];
        std::memset(l_response, 0, 32U);
        vr::VRDebug()->DriverDebugRequest(l_device, l_message.c_str(), l_response, 32U);
    }
}

void CMonitorApp::SendControllerTrigger(bool f_state)
{
    const vr::TrackedDeviceIndex_t l_device = ActiveControllerIndex();
    if(l_device != vr::k_unTrackedDeviceIndexInvalid)
    {
        std::string l_message("trigger ");
        l_message.append(f_state ? "1" : "0");

        char l_response[32U];
        std::memset(l_response, 0, 32U);
        vr::VRDebug()->DriverDebugRequest(l_device, l_message.c_str(), l_response, 32U);
    }
}

void CMonitorApp::SendControllerSystem(bool f_state)
{
    const vr::TrackedDeviceIndex_t l_device = ActiveControllerIndex();
    if(l_device != vr::k_unTrackedDeviceIndexInvalid)
    {
        std::string l_message("system ");
        l_message.append(f_state ? "1" : "0");

        char l_response[32U];
        std::memset(l_response, 0, 32U);
        vr::VRDebug()->DriverDebugRequest(l_device, l_message.c_str(), l_response, 32U);
    }
}

void CMonitorApp::SendControllerButton(const char *f_button, bool f_state)
{
    const vr::TrackedDeviceIndex_t l_device = ActiveControllerIndex();
    if(l_device != vr::k_unTrackedDeviceIndexInvalid)
    {
        std::string l_message("button ");
        l_message.append(f_button);
        l_message.append(f_state ? " 1" : " 0");

        char l_response[32U];
        std::memset(l_response, 0, 32U);
        vr::VRDebug()->DriverDebugRequest(l_device, l_message.c_str(), l_response, 32U);
    }
}

void CMonitorApp::SendControllerJoystick(float f_x, float f_y)
{
    const vr::TrackedDeviceIndex_t l_device = ActiveControllerIndex();
    if(l_device != vr::k_unTrackedDeviceIndexInvalid)
    {
        std::string l_message("joystick ");
        l_message.append(std::to_string(f_x));
        l_message.push_back(' ');
        l_message.append(std::to_string(f_y));

        char l_response[32U];
        std::memset(l_response, 0, 32U);
        vr::VRDebug()->DriverDebugRequest(l_device, l_message.c_str(), l_response, 32U);
    }
}

// Both controllers back to where they start: default offsets, no aim rotation
void CMonitorApp::ResetControllerOffsets()
{
    for(size_t i = 0U; i < 2U; i++)
    {
        m_ctrlOffsetPos[i] = g_ctrlDefaultOffset[i];
        m_ctrlOffsetRot[i] = g_zeroRotation;
    }
    SendControllerOffset(false);
    SendControllerOffset(true);
}

// Send release for every held button-type action on the active controller
// (mouse release, focus loss, or switching the active controller). Deliberately
// leaves KB_SwitchController's edge state alone - clearing it would retrigger
// the switch on the next frame while the key is still held.
void CMonitorApp::ReleaseHeldActions()
{
    if(m_actionHeld[KB_Trigger]) SendControllerTrigger(false);
    if(m_actionHeld[KB_Grip]) SendControllerButton("grip", false);
    if(m_actionHeld[KB_SystemButton]) SendControllerSystem(false);
    if(m_actionHeld[KB_ButtonA]) SendControllerButton(m_activeControllerLeft ? "x" : "a", false);
    if(m_actionHeld[KB_ButtonB]) SendControllerButton(m_activeControllerLeft ? "y" : "b", false);
    m_actionHeld[KB_Trigger] = false;
    m_actionHeld[KB_Grip] = false;
    m_actionHeld[KB_SystemButton] = false;
    m_actionHeld[KB_ButtonA] = false;
    m_actionHeld[KB_ButtonB] = false;

    if((m_lastJoystick[0U] != 0.f) || (m_lastJoystick[1U] != 0.f))
    {
        m_lastJoystick[0U] = 0.f;
        m_lastJoystick[1U] = 0.f;
        SendControllerJoystick(0.f, 0.f);
    }
}

void CMonitorApp::LoadKeyBindings()
{
    // User overrides written by the Config tool; defaults come from the driver's
    // default.vrsettings (with the compiled-in table as a last resort)
    vr::IVRSettings *l_settings = vr::VRSettings();
    if(!l_settings) return;
    for(int i = 0; i < KB_Count; i++)
    {
        vr::EVRSettingsError l_error = vr::VRSettingsError_None;
        const int32_t l_key = l_settings->GetInt32(g_driverSettingsSection, g_keyBindingDefs[i].m_settingsKey, &l_error);
        if((l_error == vr::VRSettingsError_None) && (l_key > 0) && (l_key < 256)) m_keyBindings[i] = l_key;
    }
}

void CMonitorApp::FindControllerDevice()
{
    for(vr::TrackedDeviceIndex_t i = 0U; i < vr::k_unMaxTrackedDeviceCount; i++)
    {
        char l_serial[64U] = { 0 };
        vr::ETrackedPropertyError l_propError = vr::TrackedProp_Success;
        m_vrSystem->GetStringTrackedDeviceProperty(i, vr::Prop_SerialNumber_String, l_serial, sizeof(l_serial), &l_propError);
        if(l_propError != vr::TrackedProp_Success) continue;
        if(!strcmp(l_serial, g_controllerSerial)) m_controllerDevice = i;
        else if(!strcmp(l_serial, g_leftControllerSerial)) m_leftControllerDevice = i;
    }
}

// Absolute-input sources (RDP) stop producing new positions once the cursor
// pins against the clip rect, which kills mouse look entirely; warp it back to
// the window center before it gets there. The warp's own jump is discarded by
// the delta spike filter in the WM_INPUT handler.
void CMonitorApp::RecenterAbsoluteCursor()
{
    if(!m_absMouseSeen) return;

    RECT l_clientRect;
    GetClientRect(m_window, &l_clientRect);
    POINT l_topLeft = { l_clientRect.left, l_clientRect.top };
    POINT l_bottomRight = { l_clientRect.right, l_clientRect.bottom };
    ClientToScreen(m_window, &l_topLeft);
    ClientToScreen(m_window, &l_bottomRight);

    POINT l_cursor;
    if(!GetCursorPos(&l_cursor)) return;

    const long l_margin = 40;
    if(((l_cursor.x - l_topLeft.x) < l_margin) || ((l_bottomRight.x - l_cursor.x) < l_margin) ||
        ((l_cursor.y - l_topLeft.y) < l_margin) || ((l_bottomRight.y - l_cursor.y) < l_margin))
    {
        SetCursorPos((l_topLeft.x + l_bottomRight.x) / 2, (l_topLeft.y + l_bottomRight.y) / 2);
    }
}

void CMonitorApp::ProcessInput(float f_delta)
{
    if(!m_focused || !m_captured)
    {
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
        return;
    }

    // The click that captured the mouse must not fire the trigger binding
    if(m_captureGuard && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) m_captureGuard = false;

    auto l_isDown = [this](EKeyBinding f_binding) -> bool
    {
        const int l_key = m_keyBindings[f_binding];
        if(m_captureGuard && (l_key == VK_LBUTTON)) return false;
        return ((GetAsyncKeyState(l_key) & 0x8000) != 0);
    };
    // -1 = released this frame, +1 = pressed this frame, 0 = no change
    auto l_edge = [this, &l_isDown](EKeyBinding f_binding) -> int
    {
        const bool l_down = l_isDown(f_binding);
        if(l_down == m_actionHeld[f_binding]) return 0;
        m_actionHeld[f_binding] = l_down;
        return (l_down ? 1 : -1);
    };

    int l_change;
    if((l_change = l_edge(KB_SwitchController)) > 0)
    {
        // Release held buttons on the outgoing controller so nothing sticks
        ReleaseHeldActions();
        m_activeControllerLeft = !m_activeControllerLeft;
        UpdateTitle();
    }
    if((l_change = l_edge(KB_MouseAim)) > 0)
    {
        m_mouseAimController = !m_mouseAimController;
        UpdateTitle();
    }
    // One-shot resets on key press: the head reset is applied below, before this
    // frame's movement, so held movement keys keep working through it
    const bool l_resetHead = (l_edge(KB_ResetHeadPose) > 0);
    if(l_edge(KB_ResetControllers) > 0) ResetControllerOffsets();
    if((l_change = l_edge(KB_Trigger)) != 0) SendControllerTrigger(l_change > 0);
    if((l_change = l_edge(KB_Grip)) != 0) SendControllerButton("grip", l_change > 0);
    if((l_change = l_edge(KB_SystemButton)) != 0) SendControllerSystem(l_change > 0);
    if((l_change = l_edge(KB_ButtonA)) != 0) SendControllerButton(m_activeControllerLeft ? "x" : "a", l_change > 0);
    if((l_change = l_edge(KB_ButtonB)) != 0) SendControllerButton(m_activeControllerLeft ? "y" : "b", l_change > 0);

    // Digital joystick emulation on the active controller
    const float l_joyX = (l_isDown(KB_JoystickRight) ? 1.f : 0.f) - (l_isDown(KB_JoystickLeft) ? 1.f : 0.f);
    const float l_joyY = (l_isDown(KB_JoystickUp) ? 1.f : 0.f) - (l_isDown(KB_JoystickDown) ? 1.f : 0.f);
    if((l_joyX != m_lastJoystick[0U]) || (l_joyY != m_lastJoystick[1U]))
    {
        m_lastJoystick[0U] = l_joyX;
        m_lastJoystick[1U] = l_joyY;
        SendControllerJoystick(l_joyX, l_joyY);
    }

    // IJKL: move the active controller in head-local space
    glm::vec3 l_ctrlMove(0.f);
    if(l_isDown(KB_ControllerForward)) l_ctrlMove.z -= 1.f;
    if(l_isDown(KB_ControllerBackward)) l_ctrlMove.z += 1.f;
    if(l_isDown(KB_ControllerLeft)) l_ctrlMove.x -= 1.f;
    if(l_isDown(KB_ControllerRight)) l_ctrlMove.x += 1.f;
    if(l_ctrlMove != g_zeroPoint3)
    {
        m_ctrlOffsetPos[m_activeControllerLeft ? 1U : 0U] += l_ctrlMove*(g_ctrlMoveSpeed*f_delta);
        SendControllerOffset(m_activeControllerLeft);
    }

    float l_yawDelta = -static_cast<float>(m_mouseDeltaX)*g_mouseSense;
    float l_pitchDelta = -static_cast<float>(m_mouseDeltaY)*g_mouseSense;
    m_mouseDeltaX = 0;
    m_mouseDeltaY = 0;

    if(m_mouseAimController)
    {
        // Mouse steers the active controller's pointer direction; the head only
        // rotates via the numpad keys while this mode is on
        if((l_yawDelta != 0.f) || (l_pitchDelta != 0.f))
        {
            const size_t l_active = (m_activeControllerLeft ? 1U : 0U);
            m_ctrlOffsetRot[l_active] = glm::angleAxis(l_yawDelta, g_axisY)*glm::angleAxis(l_pitchDelta, g_axisX)*m_ctrlOffsetRot[l_active];
            SendControllerOffset(m_activeControllerLeft);
        }
        l_yawDelta = 0.f;
        l_pitchDelta = 0.f;
    }

    // Local movement intent: x = right, y = up, z = forward
    glm::vec3 l_move(0.f);
    if(l_isDown(KB_MoveForward)) l_move.z += 1.f;
    if(l_isDown(KB_MoveBackward)) l_move.z -= 1.f;
    if(l_isDown(KB_MoveLeft)) l_move.x -= 1.f;
    if(l_isDown(KB_MoveRight)) l_move.x += 1.f;
    if(l_isDown(KB_MoveUp)) l_move.y += 1.f;
    if(l_isDown(KB_MoveDown)) l_move.y -= 1.f;

    float l_keyYaw = 0.f;
    float l_keyPitch = 0.f;
    float l_keyRoll = 0.f;
    if(l_isDown(KB_YawLeft)) l_keyYaw += g_keyRotateSpeed*f_delta;
    if(l_isDown(KB_YawRight)) l_keyYaw -= g_keyRotateSpeed*f_delta;
    if(l_isDown(KB_PitchDown)) l_keyPitch -= g_keyRotateSpeed*f_delta;
    if(l_isDown(KB_PitchUp)) l_keyPitch += g_keyRotateSpeed*f_delta;
    if(l_isDown(KB_RollLeft)) l_keyRoll -= g_keyRotateSpeed*f_delta;
    if(l_isDown(KB_RollRight)) l_keyRoll += g_keyRotateSpeed*f_delta;

    const bool l_hasRotation = ((l_yawDelta != 0.f) || (l_pitchDelta != 0.f) || (l_keyYaw != 0.f) || (l_keyPitch != 0.f) || (l_keyRoll != 0.f));
    const bool l_hasMove = (l_move != g_zeroPoint3);

    // Move the head, FPS-style
    bool l_changed = false;
    if(l_resetHead)
    {
        // Back to the origin, facing -Z with no pitch or roll (the only way to
        // get rid of roll picked up from a fumbled numpad press)
        m_position = g_zeroPoint3;
        m_yaw = 0.f;
        m_pitch = 0.f;
        m_roll = 0.f;
        l_changed = true;
    }
    if(l_hasRotation)
    {
        m_yaw += (l_yawDelta + l_keyYaw);
        m_pitch = glm::clamp(m_pitch + l_pitchDelta + l_keyPitch, -g_pitchLimit, g_pitchLimit);
        m_roll += l_keyRoll;
        l_changed = true;
    }
    if(l_hasMove)
    {
        const glm::quat l_yawRotation = glm::angleAxis(m_yaw, g_axisY);
        glm::vec3 l_shift = l_yawRotation*glm::vec3(l_move.x, 0.f, -l_move.z);
        l_shift.y += l_move.y;
        m_position += l_shift*(g_moveSpeed*f_delta);
        l_changed = true;
    }
    if(l_changed) SendHmdPose();

    RecenterAbsoluteCursor();
}

bool CMonitorApp::Update()
{
    // Window messages
    MSG l_message;
    while(PeekMessageW(&l_message, NULL, 0U, 0U, PM_REMOVE))
    {
        if(l_message.message == WM_QUIT) m_active = false;
        TranslateMessage(&l_message);
        DispatchMessageW(&l_message);
    }
    if(!m_active) return false;


    const std::chrono::steady_clock::time_point l_now = std::chrono::steady_clock::now();
    float l_delta = std::chrono::duration<float>(l_now - m_lastTick).count();
    m_lastTick = l_now;
    l_delta = glm::clamp(l_delta, 0.f, 0.1f);

    while(m_vrSystem->PollNextEvent(&m_event, sizeof(vr::VREvent_t)))
    {
        switch(m_event.eventType)
        {
            case vr::VREvent_Quit:
                m_active = false;
                break;
            case vr::VREvent_TrackedDeviceDeactivated:
            {
                if(m_controllerDevice == m_event.trackedDeviceIndex) m_controllerDevice = vr::k_unTrackedDeviceIndexInvalid;
                if(m_leftControllerDevice == m_event.trackedDeviceIndex) m_leftControllerDevice = vr::k_unTrackedDeviceIndexInvalid;
            } break;
            case vr::VREvent_TrackedDeviceActivated:
            {
                if((m_controllerDevice == vr::k_unTrackedDeviceIndexInvalid) || (m_leftControllerDevice == vr::k_unTrackedDeviceIndexInvalid)) FindControllerDevice();
            } break;
        }
    }
    if(!m_active) return false;

    ProcessInput(l_delta);

    RenderFrame();
    if(!m_swapChain) std::this_thread::sleep_for(std::chrono::milliseconds(16U));

    return m_active;
}
