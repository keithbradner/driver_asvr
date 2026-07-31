#pragma once

#include "../shared/KeyBindings.h"

class CMonitorApp
{
public:
    CMonitorApp();
    ~CMonitorApp();

    bool Init();
    bool Update();
    void Shutdown();

private:
    bool m_active;

    vr::IVRSystem *m_vrSystem;
    vr::TrackedDeviceIndex_t m_controllerDevice; // virtual right controller
    vr::TrackedDeviceIndex_t m_leftControllerDevice; // virtual left controller
    vr::VREvent_t m_event;

    // Window
    HWND m_window;
    bool m_focused;
    bool m_captured;
    long m_mouseDeltaX;
    long m_mouseDeltaY;
    bool m_sizeChanged;
    int m_keyBindings[KB_Count]; // VK codes, loaded from vrsettings at Init
    bool m_actionHeld[KB_Count]; // edge detection for button-type actions
    bool m_activeControllerLeft; // which virtual controller the bindings drive
    bool m_mouseAimController; // toggle: mouse rotates the active controller, not the head
    bool m_captureGuard; // swallow the LMB press that captured the mouse
    float m_lastJoystick[2U];

    // RDP and other injected input deliver absolute mouse positions, not deltas
    bool m_absMouseSeen;
    bool m_absMouseValid;
    long m_absMouseX;
    long m_absMouseY;
    std::chrono::steady_clock::time_point m_lastTick;

    // D3D11
    ID3D11Device *m_device;
    ID3D11DeviceContext *m_context;
    IDXGISwapChain *m_swapChain;
    ID3D11RenderTargetView *m_renderTarget;
    ID3D11VertexShader *m_vertexShader;
    ID3D11PixelShader *m_pixelShader;
    ID3D11SamplerState *m_sampler;
    ID3D11ShaderResourceView *m_mirrorView;

    // Frame shared by the driver's DisplayRedirect device (virtual display mode);
    // preferred over the compositor mirror texture
    HANDLE m_frameMapping;
    void *m_frameInfoView;
    unsigned int m_frameGeneration;
    int m_frameMapRetry;
    ID3D11ShaderResourceView *m_sharedFrameView;

    // HMD state (authoritative, sent as absolute pose)
    glm::vec3 m_position;
    float m_yaw;
    float m_pitch;
    float m_roll;

    // Virtual controller head-local offset state, [0] = right, [1] = left
    glm::vec3 m_ctrlOffsetPos[2U];
    glm::quat m_ctrlOffsetRot[2U];

    static LRESULT CALLBACK WindowProcStatic(HWND f_window, UINT f_message, WPARAM f_wParam, LPARAM f_lParam);
    LRESULT WindowProc(UINT f_message, WPARAM f_wParam, LPARAM f_lParam);

    bool CreateMainWindow();
    bool CreateRenderer();
    void DestroyRenderer();
    void UpdateSharedFrame();
    void RenderFrame();

    void CaptureMouse();
    void ReleaseMouse();
    void UpdateTitle();
    void UpdateCursorClip();

    glm::quat GetOrientation() const;
    void SetOrientationFromQuat(const glm::quat &f_rot);
    vr::TrackedDeviceIndex_t ActiveControllerIndex() const;
    void SendHmdPose();
    void SendControllerOffset(bool f_left);
    void SendControllerTrigger(bool f_state);
    void SendControllerSystem(bool f_state);
    void SendControllerButton(const char *f_button, bool f_state);
    void SendControllerJoystick(float f_x, float f_y);
    void ReleaseHeldActions();
    void FindControllerDevice();
    void LoadKeyBindings();
    void RecenterAbsoluteCursor();
    void ProcessInput(float f_delta);
};
