#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "openvr.h"
#include "imgui.h"

#include "CConfigCore.h"
#include "CVRLink.h"
#include "CFrameReader.h"
#include "CBindingsView.h"
#include "CUiState.h"

struct GLFWwindow;
struct GLFWcursor;
class CSettingsPanel;
class CInputController;

// The ASVR window: one GLFW + ImGui + OpenGL app on both platforms. Shows the
// virtual headset's view and drives it with mouse and keyboard while SteamVR
// runs; offers to start SteamVR (and to install the driver) while it does not;
// carries the driver's settings in a side panel and the key bindings in a
// panel at the bottom.
class CMonitorApp final
{
public:
    explicit CMonitorApp(bool f_launchedByDriver);
    ~CMonitorApp();

    bool Init();
    bool Update(); // false: time to exit
    void Shutdown();

private:
    void TryConnect(double f_now);
    void OnDisconnected(double f_now);
    void ReloadBindings();
    void CaptureMouse();
    void ReleaseMouse();
    static void CursorPosCallback(GLFWwindow *f_window, double f_x, double f_y);
    void OnCursorMoved(double f_x, double f_y);
    void RecenterCursorNearEdge(float f_delta);
    void EdgeZone(double f_x, double f_y, int f_width, int f_height, int &f_zoneX, int &f_zoneY) const;
    void UpdateFrameTexture();
    void SaveUi();
    void StartSteamVRFeedback();

    void DrawFrame();
    void DrawTopBar(float f_height);
    void DrawView();
    void DrawStartScreen(const ImVec2 &f_areaMin, const ImVec2 &f_areaSize);
    void DrawForeignHmdScreen(const ImVec2 &f_areaMin, const ImVec2 &f_areaSize);
    void DrawStatusBar(float f_height);
    void DescribeState(std::string &f_text, ImVec4 &f_color) const;

    bool m_launchedByDriver;
    bool m_active = false;
    GLFWwindow *m_window = nullptr;
    float m_uiScale = 1.f;

    UiState m_ui;
    CConfigCore m_core;
    std::unique_ptr<CSettingsPanel> m_settings;
    CBindingsView m_bindingsView;

    CVRLink m_link;
    std::unique_ptr<CInputController> m_input;
    CFrameReader m_frames;
    bool m_poseAdopted = false; // the driver's restored pose has been read; before that nothing is sent
    bool m_formatWarned = false;
    unsigned long m_connectedPid = 0UL; // the vrserver we connected to; another one means SteamVR restarted
    vr::EVRInitError m_lastInitError = vr::VRInitError_None;
    double m_nextConnectAttempt = 0.0;
    double m_fastRetryUntil = 0.0; // after "Start SteamVR": poll faster for a while
    double m_lastStatusRefresh = 0.0;
    double m_lastTick = 0.0;

    // Mouse capture (look / aim): the cursor is hidden and confined to the
    // window, motion is the difference between real cursor positions, and the
    // cursor is warped back to the centre only when it nears the edge - see
    // OnCursorMoved for why GLFW's own disabled-cursor mode is not used
    GLFWcursor *m_blankCursor = nullptr;
    bool m_captured = false;
    bool m_escapeWasDown = false;
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    float m_pendingDeltaX = 0.f; // motion gathered between frames
    float m_pendingDeltaY = 0.f;
    double m_warpX = 0.0;        // where the cursor was last warped to (the window centre)
    double m_warpY = 0.0;
    int m_eventsSinceWarp = 0;   // the warp's own cursor event is still to come while this is small
    bool m_warpCatchUpPending = false; // the first real event after a warp decides relative vs injected pointer
    // What kind of pointer this is, learnt from that decision: a relative one
    // (local mouse, RDP) carries on from the centre after a warp, an injected
    // one (Chrome Remote Desktop) lands back where it was
    enum EPointerKind { PK_Unknown, PK_Relative, PK_Injected };
    EPointerKind m_pointerKind = PK_Unknown;
    int m_pushX = 0;                   // edge the pointer is held against (-1/0/+1): keep turning that way
    int m_pushY = 0;

    // The view texture: the driver's side-by-side frame, left eye shown
    unsigned int m_texture = 0U;
    uint32_t m_textureWidth = 0U;
    uint32_t m_textureHeight = 0U;
    uint64_t m_shownFrame = 0U;
};
