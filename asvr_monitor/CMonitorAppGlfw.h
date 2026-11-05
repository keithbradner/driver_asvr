#pragma once

// Linux monitor window: GLFW + fixed-function OpenGL sibling of the Win32/D3D11
// CMonitorApp. Shares the driver's DebugRequest text vocabulary, the VK-code
// key bindings (translated via shared/KeyMap.h) and the shm frame block.
// The RDP absolute-mouse handling and the compositor mirror-texture fallback
// of the Windows monitor are intentionally absent: raw relative mouse motion is
// always available locally, and the shm path is the primary frame source.

#include <chrono>
#include <string>
#include "openvr.h"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "../shared/KeyBindings.h"

struct GLFWwindow;
struct SharedFramePixelsInfo;

class CMonitorAppGlfw
{
public:
    CMonitorAppGlfw();

    bool Init();
    bool Update();
    void Shutdown();

private:
    bool m_active = false;

    vr::IVRSystem *m_vrSystem = nullptr;
    vr::TrackedDeviceIndex_t m_controllerDevice = vr::k_unTrackedDeviceIndexInvalid; // virtual right controller
    vr::TrackedDeviceIndex_t m_leftControllerDevice = vr::k_unTrackedDeviceIndexInvalid; // virtual left controller

    GLFWwindow *m_window = nullptr;
    bool m_captured = false;
    bool m_captureGuard = false; // swallow the LMB press that captured the mouse
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    float m_mouseDeltaX = 0.f;
    float m_mouseDeltaY = 0.f;

    int m_keyBindings[KB_Count]; // VK codes, loaded from vrsettings at Init
    int m_glfwKeys[KB_Count];    // translated; GLFW_KEY_UNKNOWN when unmapped
    int m_glfwMouseButtons[KB_Count]; // -1 when the binding is not a mouse button
    bool m_actionHeld[KB_Count];
    bool m_activeControllerLeft = false;
    bool m_mouseAimController = false;
    float m_lastJoystick[2] = { 0.f, 0.f };
    std::chrono::steady_clock::time_point m_lastTick;

    // Frame view (shm reader)
    const SharedFramePixelsInfo *m_frameInfo = nullptr;
    size_t m_frameMappingSize = 0U;
    int m_frameMapRetry = 0;
    uint64_t m_shownFrame = 0U;
    unsigned int m_texture = 0U;
    uint32_t m_textureWidth = 0U;
    uint32_t m_textureHeight = 0U;

    // HMD state (authoritative, sent as absolute pose)
    glm::vec3 m_position = glm::vec3(0.f);
    float m_yaw = 0.f;
    float m_pitch = 0.f;
    float m_roll = 0.f;

    // Virtual controller head-local offset state, [0] = right, [1] = left
    glm::vec3 m_ctrlOffsetPos[2];
    glm::quat m_ctrlOffsetRot[2];

    void CaptureMouse();
    void ReleaseMouse();
    void UpdateTitle();

    glm::quat GetOrientation() const;
    void SetOrientationFromQuat(const glm::quat &f_rot);
    vr::TrackedDeviceIndex_t ActiveControllerIndex() const;
    void SendDebugRequest(vr::TrackedDeviceIndex_t f_device, const std::string &f_message);
    void SendHmdPose();
    void SendControllerOffset(bool f_left);
    void SendControllerTrigger(bool f_state);
    void SendControllerSystem(bool f_state);
    void SendControllerButton(const char *f_button, bool f_state);
    void SendControllerJoystick(float f_x, float f_y);
    void ResetControllerOffsets();
    void ReleaseHeldActions();
    void FindControllerDevice();
    void LoadKeyBindings();
    void ProcessInput(float f_delta);
    void UpdateSharedFrame();
    void RenderFrame();
};
