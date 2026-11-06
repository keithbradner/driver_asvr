#include "CMonitorApp.h"
#include "CSettingsPanel.h"
#include "CInputController.h"
#include "CPlatform.h"
#include "../shared/SharedFrameInfo.h"
#include "../shared/KeyBindings.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "GLFW/glfw3.h" // also brings in GL/gl.h for the texture upload
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif

static const float g_topBarHeight = 38.f; // before UI scaling
static const double g_connectRetrySeconds = 2.0;
static const double g_fastRetrySeconds = 0.5;
static const double g_fastRetryWindowSeconds = 90.0; // SteamVR can take a while on first start
static const double g_statusRefreshSeconds = 2.0;

static const ImVec4 g_colorGood(0.35f, 0.85f, 0.4f, 1.f);
static const ImVec4 g_colorBad(0.95f, 0.35f, 0.35f, 1.f);
static const ImVec4 g_colorWarn(0.95f, 0.8f, 0.3f, 1.f);
static const ImVec4 g_colorInfo(0.75f, 0.78f, 0.85f, 1.f);

// A headline drawn with the default font at a larger size; advances the cursor like a widget
static void BigText(const char *f_text, const ImVec4 &f_color, float f_scale)
{
    ImFont *l_font = ImGui::GetFont();
    const float l_size = ImGui::GetFontSize() * f_scale;
    const ImVec2 l_pos = ImGui::GetCursorScreenPos();
    const ImVec2 l_extent = l_font->CalcTextSizeA(l_size, FLT_MAX, 0.f, f_text);
    ImGui::GetWindowDrawList()->AddText(l_font, l_size, l_pos, ImGui::GetColorU32(f_color), f_text);
    ImGui::Dummy(ImVec2(l_extent.x, l_extent.y));
}

// Text centred in a rectangle, straight onto the draw list
static void CenteredText(ImDrawList *f_list, const ImVec2 &f_min, const ImVec2 &f_size, float f_yFraction, const ImVec4 &f_color, const char *f_text, float f_scale = 1.f)
{
    ImFont *l_font = ImGui::GetFont();
    const float l_size = ImGui::GetFontSize() * f_scale;
    const ImVec2 l_extent = l_font->CalcTextSizeA(l_size, FLT_MAX, 0.f, f_text);
    const ImVec2 l_pos(f_min.x + (f_size.x - l_extent.x) * 0.5f, f_min.y + f_size.y * f_yFraction - l_extent.y * 0.5f);
    f_list->AddText(l_font, l_size, l_pos, ImGui::GetColorU32(f_color), f_text);
}

CMonitorApp::CMonitorApp(bool f_launchedByDriver) : m_launchedByDriver(f_launchedByDriver)
{
}

CMonitorApp::~CMonitorApp()
{
    Shutdown();
}

bool CMonitorApp::Init()
{
    if(m_active) return true;
    CPlatform::Log(m_launchedByDriver ? "asvr_monitor starting (launched by the driver)" : "asvr_monitor starting");
    m_ui = LoadUiState();

    glfwSetErrorCallback([](int f_code, const char *f_text)
    {
        CPlatform::Log("GLFW error " + std::to_string(f_code) + ": " + f_text);
    });
    if(!glfwInit())
    {
        CPlatform::Log("glfwInit failed (no display?)");
        return false;
    }

    // HiDPI: scale the font and the style, not the framebuffer
    float l_scaleX = 1.f;
    float l_scaleY = 1.f;
    GLFWmonitor *l_monitor = glfwGetPrimaryMonitor();
    if(l_monitor) glfwGetMonitorContentScale(l_monitor, &l_scaleX, &l_scaleY);
    m_uiScale = std::clamp(l_scaleX, 1.f, 3.f);

    int l_width = m_ui.m_windowWidth;
    int l_height = m_ui.m_windowHeight;
    if((l_width < 640) || (l_height < 420))
    {
        l_width = static_cast<int>(1280.f * m_uiScale);
        l_height = static_cast<int>(860.f * m_uiScale);
        if(l_monitor)
        {
            int l_workX = 0, l_workY = 0, l_workWidth = 0, l_workHeight = 0;
            glfwGetMonitorWorkarea(l_monitor, &l_workX, &l_workY, &l_workWidth, &l_workHeight);
            if((l_workWidth > 0) && (l_workHeight > 0))
            {
                l_width = std::min(l_width, l_workWidth - 40);
                l_height = std::min(l_height, l_workHeight - 80);
            }
        }
    }

    // 3.3 for texture swizzling (see the view texture below); ImGui's "#version 150" shaders are fine with it
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(l_width, l_height, "ASVR", nullptr, nullptr);
    if(!m_window)
    {
        CPlatform::Log("glfwCreateWindow failed");
        glfwTerminate();
        return false;
    }
    glfwSetWindowSizeLimits(m_window, 640, 420, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    // Installed before ImGui's backend, which chains to whatever was there
    glfwSetWindowUserPointer(m_window, this);
    glfwSetCursorPosCallback(m_window, &CMonitorApp::CursorPosCallback);
    // A fully transparent cursor: GLFW's captured mode keeps the cursor
    // visible, this is what makes it disappear while looking around
    const unsigned char l_clearPixel[4] = { 0, 0, 0, 0 };
    GLFWimage l_blankImage;
    l_blankImage.width = 1;
    l_blankImage.height = 1;
    l_blankImage.pixels = const_cast<unsigned char*>(l_clearPixel);
    m_blankCursor = glfwCreateCursor(&l_blankImage, 0, 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &l_io = ImGui::GetIO();
    l_io.IniFilename = nullptr; // panel layout is ours to remember (CUiState)
    ImFontConfig l_fontConfig;
    l_fontConfig.SizePixels = std::floor(13.f * m_uiScale);
    l_io.Fonts->AddFontDefault(&l_fontConfig);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(m_uiScale);
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    m_core.Initialize();
    m_settings.reset(new CSettingsPanel(m_core));
    m_input.reset(new CInputController(m_window, m_link));
    ReloadBindings();

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // The frame is drawn through ImGui, which blends by alpha, and apps leave
    // whatever they like in their alpha channel (the Linux driver passes it
    // through); sample it as opaque
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);

    m_lastTick = glfwGetTime();
    m_lastStatusRefresh = m_lastTick;
    m_nextConnectAttempt = 0.0; // first attempt right away
    m_active = true;
    CPlatform::Log("window up");
    return true;
}

void CMonitorApp::Shutdown()
{
    if(!m_active) return;
    if(m_captured) ReleaseMouse();
    SaveUi();
    m_frames.Close();
    m_link.Disconnect();
    m_input.reset();
    m_settings.reset();

    if(m_texture)
    {
        glDeleteTextures(1, &m_texture);
        m_texture = 0U;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if(m_blankCursor)
    {
        glfwDestroyCursor(m_blankCursor);
        m_blankCursor = nullptr;
    }
    glfwDestroyWindow(m_window);
    m_window = nullptr;
    glfwTerminate();
    m_active = false;
}

void CMonitorApp::SaveUi()
{
    if(m_window) glfwGetWindowSize(m_window, &m_ui.m_windowWidth, &m_ui.m_windowHeight);
    SaveUiState(m_ui);
}

void CMonitorApp::ReloadBindings()
{
    // The saved bindings (steamvr.vrsettings, driver_asvr section) are what the
    // view polls; the panel's unsaved edits are not
    CVRSettings &l_settings = m_core.GetVRSettings();
    int l_codes[KB_Count];
    for(int i = 0; i < KB_Count; i++)
    {
        const int l_code = l_settings.GetDriverKey(g_keyBindingDefs[i].m_settingsKey, g_keyBindingDefs[i].m_defaultKey);
        l_codes[i] = ((l_code > 0) && (l_code < 256)) ? l_code : g_keyBindingDefs[i].m_defaultKey;
    }
    m_input->SetBindings(l_codes);
}

void CMonitorApp::TryConnect(double f_now)
{
    vr::EVRInitError l_error = vr::VRInitError_None;
    if(m_link.Connect(l_error))
    {
        m_lastInitError = vr::VRInitError_None;
        m_poseAdopted = false;
        m_input->ResetSession();
        m_frames.Close();
        m_shownFrame = 0U;
        m_textureWidth = 0U;
        m_textureHeight = 0U;
        m_fastRetryUntil = 0.0;
        m_core.RefreshStatus();
        m_connectedPid = m_core.GetVRServerPid();
        return;
    }
    m_lastInitError = l_error;
    m_nextConnectAttempt = f_now + ((f_now < m_fastRetryUntil) ? g_fastRetrySeconds : g_connectRetrySeconds);
}

void CMonitorApp::OnDisconnected(double f_now)
{
    if(m_captured) ReleaseMouse();
    m_link.Disconnect();
    m_frames.Close();
    m_poseAdopted = false;
    m_shownFrame = 0U;
    m_textureWidth = 0U;
    m_textureHeight = 0U;
    m_input->ResetSession();
    // Give vrserver time to go away before probing again, so a server that is
    // still shutting down is not mistaken for a fresh one
    m_nextConnectAttempt = f_now + g_connectRetrySeconds;
    m_fastRetryUntil = 0.0;
}

void CMonitorApp::StartSteamVRFeedback()
{
    const double l_now = glfwGetTime();
    m_fastRetryUntil = l_now + g_fastRetryWindowSeconds;
    m_nextConnectAttempt = l_now + 1.5;
}

void CMonitorApp::CaptureMouse()
{
    if(m_captured) return;
    m_captured = true;
    // Confined to the window and invisible, but a real cursor with real
    // positions - not GLFW's disabled-cursor mode, see OnCursorMoved
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_CAPTURED);
    if(m_blankCursor) glfwSetCursor(m_window, m_blankCursor);
    glfwGetCursorPos(m_window, &m_lastCursorX, &m_lastCursorY);
    m_pendingDeltaX = 0.f;
    m_pendingDeltaY = 0.f;
    m_eventsSinceWarp = 1000;
    m_warpCatchUpPending = false;
    m_pointerKind = PK_Unknown;
    m_pushX = 0;
    m_pushY = 0;
    m_input->OnCaptured();
}

void CMonitorApp::ReleaseMouse()
{
    if(!m_captured) return;
    m_captured = false;
    m_input->OnReleased();
    glfwSetCursor(m_window, nullptr);
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    m_pendingDeltaX = 0.f;
    m_pendingDeltaY = 0.f;
    m_pushX = 0;
    m_pushY = 0;
}

void CMonitorApp::CursorPosCallback(GLFWwindow *f_window, double f_x, double f_y)
{
    CMonitorApp *l_app = static_cast<CMonitorApp*>(glfwGetWindowUserPointer(f_window));
    if(l_app) l_app->OnCursorMoved(f_x, f_y);
}

// Mouse look from real cursor positions. GLFW's disabled-cursor mode warps
// the cursor back to the centre on every poll, which is fine for a local
// mouse but breaks every source that injects absolute positions: Chrome
// Remote Desktop keeps its own pointer where the client left it, so each
// warp is followed by a jump straight back, and the view spins. Here the
// cursor is only warped when it is about to pin against the window edge
// (RecenterCursorNearEdge), the warp's own event is swallowed, and the first
// real event after a warp tells the two kinds of pointer apart: a relative
// one (local mouse, RDP, which moves the client pointer along) carries on
// from the centre, an injected one lands back where it was - far away - and
// that jump is no motion. Over RDP the client pointer follows the warp, so
// nothing is lost there.
void CMonitorApp::OnCursorMoved(double f_x, double f_y)
{
    if(!m_captured)
    {
        m_lastCursorX = f_x;
        m_lastCursorY = f_y;
        return;
    }
    const double l_deltaX = f_x - m_lastCursorX;
    const double l_deltaY = f_y - m_lastCursorY;
    m_lastCursorX = f_x;
    m_lastCursorY = f_y;

    // The warp itself, not motion: SetCursorPos queues its event at once, so
    // it is always the very next one (a later event at the centre is a real
    // move back to the middle)
    if((m_eventsSinceWarp == 0) && (std::abs(f_x - m_warpX) < 3.0) && (std::abs(f_y - m_warpY) < 3.0))
    {
        m_eventsSinceWarp++;
        return;
    }
    m_eventsSinceWarp++;
    if(m_warpCatchUpPending)
    {
        m_warpCatchUpPending = false;
        int l_width = 0;
        int l_height = 0;
        glfwGetWindowSize(m_window, &l_width, &l_height);
        // A hand cannot move the cursor an eighth of the window between the
        // warp and the very next event; an injected pointer snapping back can
        const double l_far = 0.125 * static_cast<double>(std::min(l_width, l_height));
        const bool l_injected = ((std::abs(f_x - m_warpX) > l_far) || (std::abs(f_y - m_warpY) > l_far));
        m_pointerKind = l_injected ? PK_Injected : PK_Relative;
        // Where the injected pointer actually is decides whether the edge
        // turn goes on (still at an edge) or stops (back inside); a relative
        // pointer never turns this way
        EdgeZone(f_x, f_y, l_width, l_height, m_pushX, m_pushY);
        if(!l_injected)
        {
            m_pushX = 0;
            m_pushY = 0;
        }
        else return;
    }
    m_pendingDeltaX += static_cast<float>(l_deltaX);
    m_pendingDeltaY += static_cast<float>(l_deltaY);
}

// Which window edge (if any) a client position is within the margin of
void CMonitorApp::EdgeZone(double f_x, double f_y, int f_width, int f_height, int &f_zoneX, int &f_zoneY) const
{
    const double l_margin = 40.0 * m_uiScale;
    f_zoneX = (f_x < l_margin) ? -1 : ((f_x > (f_width - l_margin)) ? 1 : 0);
    f_zoneY = (f_y < l_margin) ? -1 : ((f_y > (f_height - l_margin)) ? 1 : 0);
}

// Absolute-input sources stop producing new positions once the cursor pins
// against the confinement rect, which kills mouse look in that direction;
// warp it back to the centre before it gets there. A pointer the warp cannot
// move (Chrome Remote Desktop's) sends nothing at all while it is held
// still beyond the edge, so the view keeps turning towards the edge it was
// last seen at until an event shows it back inside - turning is then not
// limited to the window's width. Until the pointer's kind is known the turn
// is started on trust: a relative pointer's very next event stops it, and
// its owner would have to stop moving in the one frame between reaching the
// edge and the warp to notice at all.
void CMonitorApp::RecenterCursorNearEdge(float /* f_delta */)
{
    int l_width = 0;
    int l_height = 0;
    glfwGetWindowSize(m_window, &l_width, &l_height);
    if((l_width <= 0) || (l_height <= 0)) return;
    double l_x = 0.0;
    double l_y = 0.0;
    glfwGetCursorPos(m_window, &l_x, &l_y);
    int l_zoneX = 0;
    int l_zoneY = 0;
    EdgeZone(l_x, l_y, l_width, l_height, l_zoneX, l_zoneY);
    if((l_zoneX == 0) && (l_zoneY == 0)) return;

    if(m_pointerKind != PK_Relative)
    {
        m_pushX = l_zoneX;
        m_pushY = l_zoneY;
    }

    m_warpX = std::floor(l_width * 0.5);
    m_warpY = std::floor(l_height * 0.5);
    m_eventsSinceWarp = 0;
    m_warpCatchUpPending = true;
    glfwSetCursorPos(m_window, m_warpX, m_warpY);
    // Should the warp's own event never arrive, the next real one is still relative to the centre
    m_lastCursorX = m_warpX;
    m_lastCursorY = m_warpY;
}

void CMonitorApp::UpdateFrameTexture()
{
    const SharedFramePixelsInfo *l_info = m_frames.GetInfo();
    const unsigned char *l_pixels = m_frames.GetLatestPixels();
    if(!l_info || !l_pixels) return;
    if(!SharedPixelFormatIsKnown(l_info->pixelFormat))
    {
        if(!m_formatWarned)
        {
            CPlatform::Log("unsupported pixel format " + std::to_string(l_info->pixelFormat) + " in the frame block");
            m_formatWarned = true;
        }
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_texture);
    if((m_textureWidth != l_info->width) || (m_textureHeight != l_info->height))
    {
        m_textureWidth = l_info->width;
        m_textureHeight = l_info->height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(m_textureWidth), static_cast<GLsizei>(m_textureHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_shownFrame = 0U;
    }
    if(l_info->frameCounter != m_shownFrame)
    {
        m_shownFrame = l_info->frameCounter;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(l_info->rowPitch / 4U));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(m_textureWidth), static_cast<GLsizei>(m_textureHeight),
            SharedPixelFormatIsBgra(l_info->pixelFormat) ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, l_pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
}

bool CMonitorApp::Update()
{
    if(!m_active || glfwWindowShouldClose(m_window)) return false;

    // Nothing moves on screen without SteamVR, so there is no reason to spin;
    // input events still wake the loop at once
    if(m_link.IsConnected()) glfwPollEvents();
    else glfwWaitEventsTimeout(0.05);
    if(glfwWindowShouldClose(m_window)) return false;

    // The user double-clicked the binary while this (driver-opened) window was
    // up: from now on it is theirs and stays after SteamVR quits
    if(CPlatform::PollUserLaunchSignal() && m_launchedByDriver)
    {
        m_launchedByDriver = false;
        CPlatform::Log("opened by the user as well: the window now outlives SteamVR");
    }

    const double l_now = glfwGetTime();
    const float l_delta = std::clamp(static_cast<float>(l_now - m_lastTick), 0.f, 0.1f);
    m_lastTick = l_now;

    if((l_now - m_lastStatusRefresh) >= g_statusRefreshSeconds)
    {
        m_lastStatusRefresh = l_now;
        m_core.RefreshStatus();
        // vrserver gone without a Quit event (a crash), or already replaced by
        // a restarted one: the client side would just hang up
        if(m_link.IsConnected())
        {
            const unsigned long l_pid = m_core.GetVRServerPid();
            const bool l_gone = (l_pid == 0UL) || ((m_connectedPid != 0UL) && (l_pid != m_connectedPid));
            if(l_gone)
            {
                CPlatform::Log((l_pid == 0UL) ? "vrserver is gone" : "vrserver was restarted");
                OnDisconnected(l_now);
                if(m_launchedByDriver) return false;
            }
        }
    }

    if(!m_link.IsConnected() && (l_now >= m_nextConnectAttempt)) TryConnect(l_now);

    if(m_link.IsConnected() && !m_link.PollEvents())
    {
        OnDisconnected(l_now);
        if(m_launchedByDriver) return false; // the driver's copy lives and dies with SteamVR
    }

    // The driver restores the headset pose it saved last time; adopt it before
    // sending any pose of our own
    if(m_link.IsConnected() && !m_poseAdopted)
    {
        glm::vec3 l_pos;
        glm::quat l_rot;
        if(m_link.ReadHmdPose(l_pos, l_rot))
        {
            m_input->AdoptHeadPose(l_pos, l_rot);
            m_poseAdopted = true;
        }
    }

    // Input: only while captured; Esc, focus loss and losing the headset release
    const bool l_focused = (glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != 0);
    const bool l_escapeDown = (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    const bool l_escapePressed = l_escapeDown && !m_escapeWasDown;
    m_escapeWasDown = l_escapeDown;
    const bool l_controllable = m_link.IsConnected() && !m_link.IsForeignHmd() && m_poseAdopted;
    if(m_captured && (!l_focused || !l_controllable || l_escapePressed)) ReleaseMouse();
    if(m_captured)
    {
        // Motion gathered by OnCursorMoved since the last frame, plus the
        // steady turn of an injected pointer held against an edge (the same
        // speed as dragging the mouse at 1200 px/s)
        const float l_push = 1200.f * m_uiScale * l_delta;
        const float l_deltaX = m_pendingDeltaX + static_cast<float>(m_pushX) * l_push;
        const float l_deltaY = m_pendingDeltaY + static_cast<float>(m_pushY) * l_push;
        m_pendingDeltaX = 0.f;
        m_pendingDeltaY = 0.f;
        m_input->Update(l_delta, true, l_deltaX, l_deltaY);
        RecenterCursorNearEdge(l_delta);
    }

    if(m_link.IsConnected())
    {
        m_frames.Update();
        UpdateFrameTexture();
    }

    // The panel rewrote the settings file: the bindings the view polls follow
    if(m_settings->TakeDriverSettingsChanged()) ReloadBindings();

    // Minimised: nothing to draw into (GLFW reports a 0x0 client area, which
    // would also upset the layout maths), but SteamVR events were still handled above
    if(glfwGetWindowAttrib(m_window, GLFW_ICONIFIED))
    {
        glfwWaitEventsTimeout(0.1);
        return true;
    }

    // While captured ImGui sees no input and must not restore its own cursor
    // over the blank one
    ImGuiIO &l_io = ImGui::GetIO();
    const ImGuiConfigFlags l_capturedFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    if(m_captured) l_io.ConfigFlags |= l_capturedFlags;
    else l_io.ConfigFlags &= ~l_capturedFlags;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    DrawFrame();
    ImGui::Render();

    int l_width = 0;
    int l_height = 0;
    glfwGetFramebufferSize(m_window, &l_width, &l_height);
    glViewport(0, 0, l_width, l_height);
    glClearColor(0.07f, 0.07f, 0.09f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
    return true;
}

void CMonitorApp::DescribeState(std::string &f_text, ImVec4 &f_color) const
{
    if(!m_link.IsConnected())
    {
        if(m_core.IsVRServerRunning())
        {
            f_text = "SteamVR is starting";
            f_color = g_colorWarn;
        }
        else
        {
            f_text = "SteamVR is not running";
            f_color = g_colorInfo;
        }
        return;
    }
    if(m_link.IsForeignHmd())
    {
        f_text = "Another headset owns the HMD slot - ASVR is inactive";
        f_color = g_colorWarn;
        return;
    }
    if(!m_poseAdopted)
    {
        f_text = "Connected - waiting for the ASVR headset";
        f_color = g_colorWarn;
        return;
    }
    f_text = m_input->IsLeftActive() ? "LEFT controller" : "RIGHT controller";
    f_text += m_input->IsMouseAim() ? " - mouse aims it" : " - mouse looks";
    f_text += m_captured ? " - Esc releases the mouse" : " - click the view to capture the mouse";
    f_color = g_colorGood;
}

void CMonitorApp::DrawFrame()
{
    const ImGuiViewport *l_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(l_viewport->WorkPos);
    ImGui::SetNextWindowSize(l_viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    const ImGuiStyle &l_style = ImGui::GetStyle();
    const ImVec2 l_total = ImGui::GetContentRegionAvail();
    const float l_topHeight = g_topBarHeight * m_uiScale;
    const float l_statusHeight = ImGui::GetTextLineHeight() + 2.f * l_style.WindowPadding.y;
    float l_bindingsHeight = 0.f;
    if(m_ui.m_bindingsOpen)
    {
        l_bindingsHeight = CBindingsView::PreferredHeight(l_total.x - 2.f * l_style.WindowPadding.x) + 2.f * l_style.WindowPadding.y;
        l_bindingsHeight = std::min(l_bindingsHeight, l_total.y * 0.45f);
    }
    const float l_middleHeight = std::max(80.f, l_total.y - l_topHeight - l_bindingsHeight - l_statusHeight);

    DrawTopBar(l_topHeight);

    // The panels butt against each other (zero spacing between the children),
    // while their contents keep the normal spacing: the zero is pushed only
    // around the calls that place a child, never while one is being filled
    const ImVec2 l_noSpacing(0.f, 0.f);
    if(m_ui.m_settingsOpen)
    {
        const float l_settingsWidth = std::clamp(480.f * m_uiScale, 300.f, std::max(300.f, l_total.x * 0.55f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, l_noSpacing);
        ImGui::BeginChild("##settings", ImVec2(l_settingsWidth, l_middleHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        m_settings->Draw(m_link.IsConnected());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, l_noSpacing);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::PopStyleVar();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, l_noSpacing);
    ImGui::BeginChild("##view", ImVec2(0.f, l_middleHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    DrawView();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, l_noSpacing);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if(m_ui.m_bindingsOpen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, l_noSpacing);
        ImGui::BeginChild("##bindings", ImVec2(0.f, l_bindingsHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        BindingsViewInput l_input;
        l_input.m_bindings = m_input->GetBindings();
        l_input.m_input = m_input.get();
        l_input.m_activeLeft = m_input->IsLeftActive();
        l_input.m_mouseAim = m_input->IsMouseAim();
        l_input.m_captured = m_captured;
        l_input.m_focused = (glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != 0);
        const ImVec2 l_available = ImGui::GetContentRegionAvail();
        m_bindingsView.Draw(l_input, l_available.x, l_available.y);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, l_noSpacing);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    DrawStatusBar(l_statusHeight);
    ImGui::End();
}

void CMonitorApp::DrawTopBar(float f_height)
{
    const ImGuiStyle &l_style = ImGui::GetStyle();
    ImGui::BeginChild("##top", ImVec2(0.f, f_height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList *l_list = ImGui::GetWindowDrawList();

    // The hamburger: three lines in a square button
    const float l_button = f_height - 2.f * l_style.WindowPadding.y;
    const ImVec2 l_pos = ImGui::GetCursorScreenPos();
    if(ImGui::InvisibleButton("##menu", ImVec2(l_button, l_button)))
    {
        m_ui.m_settingsOpen = !m_ui.m_settingsOpen;
        SaveUi();
    }
    const bool l_hovered = ImGui::IsItemHovered();
    if(l_hovered || m_ui.m_settingsOpen)
    {
        l_list->AddRectFilled(l_pos, ImVec2(l_pos.x + l_button, l_pos.y + l_button), ImGui::GetColorU32(l_hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button), 4.f);
    }
    const float l_lineWidth = l_button * 0.55f;
    const float l_lineX = l_pos.x + (l_button - l_lineWidth) * 0.5f;
    for(int i = 0; i < 3; i++)
    {
        const float l_y = l_pos.y + l_button * (0.3f + 0.2f * i);
        l_list->AddLine(ImVec2(l_lineX, l_y), ImVec2(l_lineX + l_lineWidth, l_y), ImGui::GetColorU32(ImGuiCol_Text), 2.f * m_uiScale);
    }
    if(l_hovered) ImGui::SetTooltip("Settings: install, the driver switch, SteamVR flags, resolution, trackers, key bindings");

    // The key bindings toggle sits at the right edge; the state text in
    // between is clipped to it so the two never overlap on a narrow window
    const char *l_label = m_ui.m_bindingsOpen ? "Hide key bindings" : "Show key bindings";
    const float l_labelWidth = ImGui::CalcTextSize(l_label).x + 2.f * l_style.FramePadding.x;
    const float l_contentRight = l_pos.x + ImGui::GetContentRegionAvail().x;
    const float l_buttonX = l_contentRight - l_labelWidth;

    // Title and state, vertically centred on the button
    ImGui::SameLine();
    const float l_textY = l_pos.y + (l_button - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + l_style.ItemSpacing.x, l_textY));
    ImGui::TextUnformatted("ASVR");
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, l_textY));
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    std::string l_state;
    ImVec4 l_stateColor;
    DescribeState(l_state, l_stateColor);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, l_textY));
    ImGui::PushClipRect(ImVec2(l_pos.x, l_pos.y), ImVec2(l_buttonX - l_style.ItemSpacing.x, l_pos.y + l_button), true);
    ImGui::TextColored(l_stateColor, "%s", l_state.c_str());
    ImGui::PopClipRect();

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(l_buttonX, l_pos.y + (l_button - ImGui::GetFrameHeight()) * 0.5f));
    if(ImGui::Button(l_label))
    {
        m_ui.m_bindingsOpen = !m_ui.m_bindingsOpen;
        SaveUi();
    }
    ImGui::EndChild();
}

void CMonitorApp::DrawView()
{
    const ImVec2 l_pos = ImGui::GetCursorScreenPos();
    const ImVec2 l_size = ImGui::GetContentRegionAvail();
    if((l_size.x <= 1.f) || (l_size.y <= 1.f)) return;
    ImDrawList *l_list = ImGui::GetWindowDrawList();
    l_list->AddRectFilled(l_pos, ImVec2(l_pos.x + l_size.x, l_pos.y + l_size.y), ImGui::GetColorU32(ImVec4(0.04f, 0.04f, 0.06f, 1.f)));

    if(!m_link.IsConnected())
    {
        DrawStartScreen(l_pos, l_size);
        return;
    }
    if(m_link.IsForeignHmd())
    {
        DrawForeignHmdScreen(l_pos, l_size);
        return;
    }

    // The whole area captures the mouse on click
    ImGui::SetCursorScreenPos(l_pos);
    ImGui::InvisibleButton("##capture", l_size);
    if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && !m_captured && m_poseAdopted) CaptureMouse();

    if((m_textureWidth > 0U) && (m_shownFrame != 0U))
    {
        // The driver's projection is a symmetric square FOV: letterbox the left
        // eye (left half of the texture) into a centred square
        const float l_side = std::min(l_size.x, l_size.y);
        const ImVec2 l_min(l_pos.x + (l_size.x - l_side) * 0.5f, l_pos.y + (l_size.y - l_side) * 0.5f);
        l_list->AddImage(static_cast<ImTextureID>(m_texture), l_min, ImVec2(l_min.x + l_side, l_min.y + l_side), ImVec2(0.f, 0.f), ImVec2(0.5f, 1.f));
    }
    else
    {
        CenteredText(l_list, l_pos, l_size, 0.45f, g_colorInfo, m_poseAdopted ? "Waiting for frames from the compositor" : "Waiting for the ASVR headset", 1.4f);
        CenteredText(l_list, l_pos, l_size, 0.45f + 40.f * m_uiScale / l_size.y, ImVec4(0.5f, 0.52f, 0.58f, 1.f),
            m_poseAdopted ? "The driver publishes frames once vrcompositor starts presenting" : "The driver adds its devices a moment after SteamVR starts");
    }

    const char *l_hint = m_captured ? "Esc releases the mouse" : (m_poseAdopted ? "Click to capture the mouse" : nullptr);
    if(l_hint)
    {
        const ImVec2 l_extent = ImGui::CalcTextSize(l_hint);
        const ImVec2 l_center(l_pos.x + l_size.x * 0.5f, l_pos.y + l_size.y - 18.f * m_uiScale);
        const ImVec2 l_padding(10.f * m_uiScale, 4.f * m_uiScale);
        l_list->AddRectFilled(ImVec2(l_center.x - l_extent.x * 0.5f - l_padding.x, l_center.y - l_extent.y * 0.5f - l_padding.y),
            ImVec2(l_center.x + l_extent.x * 0.5f + l_padding.x, l_center.y + l_extent.y * 0.5f + l_padding.y), ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.55f)), 6.f);
        l_list->AddText(ImVec2(l_center.x - l_extent.x * 0.5f, l_center.y - l_extent.y * 0.5f), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.92f, 1.f)), l_hint);
    }
}

void CMonitorApp::DrawStartScreen(const ImVec2 &f_areaMin, const ImVec2 &f_areaSize)
{
    const ImGuiStyle &l_style = ImGui::GetStyle();
    const float l_blockWidth = std::min(f_areaSize.x - 2.f * l_style.WindowPadding.x, 720.f * m_uiScale);
    const float l_top = std::max(16.f, f_areaSize.y * 0.1f);
    ImGui::SetCursorScreenPos(ImVec2(f_areaMin.x + (f_areaSize.x - l_blockWidth) * 0.5f, f_areaMin.y + l_top));
    ImGui::BeginChild("##start", ImVec2(l_blockWidth, f_areaSize.y - l_top - 8.f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushTextWrapPos(l_blockWidth);

    const bool l_starting = m_core.IsVRServerRunning();
    BigText(l_starting ? "SteamVR is starting" : "SteamVR is not running", l_starting ? g_colorWarn : ImVec4(0.92f, 0.93f, 0.95f, 1.f), 1.8f);
    if(l_starting)
    {
        ImGui::TextDisabled("vrserver is up; this window connects as soon as it accepts clients (%s)",
            (m_lastInitError == vr::VRInitError_None) ? "connecting" : vr::VR_GetVRInitErrorAsEnglishDescription(m_lastInitError));
    }
    else if((m_lastInitError != vr::VRInitError_None) && (m_lastInitError != vr::VRInitError_Init_NoServerForBackgroundApp))
    {
        ImGui::TextDisabled("Last connection attempt: %s", vr::VR_GetVRInitErrorAsEnglishDescription(m_lastInitError));
    }
    ImGui::Spacing();
    ImGui::Spacing();

    // The driver's state first: without it SteamVR has nothing of ours to start
    if(!m_core.AreDriverFilesFound())
    {
        ImGui::TextColored(g_colorBad, "Driver files not found at %s", m_core.GetDriverPath().c_str());
        ImGui::TextWrapped("asvr_monitor belongs next to the asvr folder of the extracted package (or inside asvr/bin/win64 - asvr/bin/linux64 on Linux). Re-extract the package and run it from there.");
    }
    else if(!m_core.GetOpenVRPaths().IsLoaded())
    {
        ImGui::TextColored(g_colorWarn, "%s", m_core.GetOpenVRPaths().GetError().c_str());
    }
    else if(!m_core.IsDriverRegistered())
    {
        ImGui::TextColored(g_colorWarn, "The ASVR driver is not registered with SteamVR yet.");
        if(ImGui::Button("Install the driver"))
        {
            if(m_core.Install())
            {
                m_settings->SyncFromCore();
                ReloadBindings();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("registers the asvr folder and applies the SteamVR settings headless use needs");
    }
    else if(!m_core.GetVRSettings().GetDriverEnabled())
    {
        ImGui::TextColored(g_colorWarn, "The ASVR driver is disabled; SteamVR will skip it.");
        if(ImGui::Button("Enable the driver")) m_core.SetDriverEnabled(true);
    }
    else
    {
        ImGui::TextColored(g_colorGood, "ASVR driver installed and enabled.");
    }
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Start SteamVR");
    const std::vector<std::string> &l_runtimes = m_core.GetRuntimePaths();
    if(l_runtimes.empty())
    {
        ImGui::TextWrapped("No SteamVR install is listed in openvrpaths.vrpath. Install SteamVR through Steam and run it once.");
    }
    else
    {
        ImGui::TextDisabled("Every SteamVR install listed in openvrpaths.vrpath:");
        std::error_code l_error;
        for(size_t i = 0U; i < l_runtimes.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i));
            const bool l_present = std::filesystem::exists(CPlatform::GetSteamVRLauncher(l_runtimes[i]), l_error);
            ImGui::BeginDisabled(!l_present);
            if(ImGui::Button("Start", ImVec2(90.f * m_uiScale, 0.f)))
            {
                if(m_core.StartSteamVR(l_runtimes[i])) StartSteamVRFeedback();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if(l_present) ImGui::TextWrapped("%s", l_runtimes[i].c_str());
            else ImGui::TextDisabled("%s  (no launcher found)", l_runtimes[i].c_str());
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    if(ImGui::Button("Launch through Steam", ImVec2(0.f, 0.f)))
    {
        if(m_core.StartSteamVRViaSteam()) StartSteamVRFeedback();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("steam://rungameid/250820 - whichever install Steam manages");
    ImGui::Spacing();
    ImGui::TextDisabled("Once SteamVR is up this window connects by itself. Starting SteamVR from Steam works too: the driver opens this window if it is not already open.");

    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void CMonitorApp::DrawForeignHmdScreen(const ImVec2 &f_areaMin, const ImVec2 &f_areaSize)
{
    const ImGuiStyle &l_style = ImGui::GetStyle();
    const float l_blockWidth = std::min(f_areaSize.x - 2.f * l_style.WindowPadding.x, 720.f * m_uiScale);
    const float l_top = std::max(16.f, f_areaSize.y * 0.15f);
    ImGui::SetCursorScreenPos(ImVec2(f_areaMin.x + (f_areaSize.x - l_blockWidth) * 0.5f, f_areaMin.y + l_top));
    ImGui::BeginChild("##foreign", ImVec2(l_blockWidth, f_areaSize.y - l_top - 8.f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushTextWrapPos(l_blockWidth);

    BigText("Another headset owns the HMD slot", g_colorWarn, 1.8f);
    ImGui::Spacing();
    ImGui::TextWrapped("SteamVR's headset is \"%s\". vrserver allows one HMD and that driver registered first, so the ASVR headset, "
        "its controllers and its trackers were not started and this window has nothing to show or control.", m_link.GetHmdSerial().c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("To use ASVR instead: tick forcedDriver under SteamVR Settings in the settings panel and restart SteamVR "
        "(SteamVR then loads only the asvr driver until you untick it), or disconnect the other headset / close its software and restart SteamVR.");
    ImGui::Spacing();
    if(ImGui::Button("Open settings"))
    {
        m_ui.m_settingsOpen = true;
        SaveUi();
    }

    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void CMonitorApp::DrawStatusBar(float f_height)
{
    ImGui::BeginChild("##status", ImVec2(0.f, f_height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const std::string &l_message = m_core.GetLastMessage();
    if(l_message.empty()) ImGui::TextDisabled("Ready");
    else ImGui::TextColored(m_core.IsLastMessageError() ? g_colorBad : g_colorGood, "%s", l_message.c_str());
    ImGui::EndChild();
}
