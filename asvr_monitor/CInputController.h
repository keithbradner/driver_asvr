#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "../shared/KeyBindings.h"

struct GLFWwindow;
class CVRLink;

// Mouse and keyboard -> the virtual headset and controllers. Polls GLFW for
// the bound keys while the mouse is captured, keeps the authoritative head
// pose and the head-local controller offsets, and sends changes to the
// driver through CVRLink.
class CInputController final
{
public:
    CInputController(GLFWwindow *f_window, CVRLink &f_link);

    void SetBindings(const int *f_vkCodes); // KB_Count Windows VK codes
    const int* GetBindings() const { return m_keyBindings; }

    void AdoptHeadPose(const glm::vec3 &f_pos, const glm::quat &f_rot);
    void ResetSession(); // SteamVR went away: forget held state, keep the bindings

    void OnCaptured(); // the click that captured must not fire the trigger
    void OnReleased(); // release every held button so nothing sticks in VR

    void Update(float f_delta, bool f_captured, float f_mouseDeltaX, float f_mouseDeltaY);

    bool IsLeftActive() const { return m_activeControllerLeft; }
    bool IsMouseAim() const { return m_mouseAimController; }

    // Live key state for the bindings view (window focus permitting): a VK
    // code counts both physical keys it stands for (see KeyMapToGlfwAlternate)
    bool IsVkDown(int f_vk) const;
    bool IsGlfwKeyDown(int f_key) const;

private:
    glm::quat GetOrientation() const;
    void SetOrientationFromQuat(const glm::quat &f_rot);
    bool IsBindingDown(EKeyBinding f_binding) const;
    void SendControllerOffset(bool f_left);
    void ResetControllerOffsets();
    void ReleaseHeldActions();

    GLFWwindow *m_window;
    CVRLink &m_link;

    int m_keyBindings[KB_Count]; // VK codes
    int m_glfwKeys[KB_Count];    // translated; GLFW_KEY_UNKNOWN when unmapped
    int m_glfwKeysAlternate[KB_Count]; // the other side of a modifier / numpad Enter, or GLFW_KEY_UNKNOWN
    int m_glfwMouseButtons[KB_Count]; // -1 when the binding is not a mouse button
    bool m_actionHeld[KB_Count]; // edge detection for button-type actions
    bool m_activeControllerLeft = false; // which virtual controller the bindings drive
    bool m_mouseAimController = false; // toggle: mouse rotates the active controller, not the head
    bool m_captureGuard = false; // swallow the LMB press that captured the mouse
    float m_lastJoystick[2] = { 0.f, 0.f };

    // HMD state (authoritative, sent as absolute pose)
    glm::vec3 m_position = glm::vec3(0.f);
    float m_yaw = 0.f;
    float m_pitch = 0.f;
    float m_roll = 0.f;

    // Virtual controller head-local offset state, [0] = right, [1] = left
    glm::vec3 m_ctrlOffsetPos[2];
    glm::quat m_ctrlOffsetRot[2];
};
