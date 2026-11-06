#include "CInputController.h"
#include "CVRLink.h"
#include "CPlatform.h"

#include <cmath>
#include <cstdio>
#include "GLFW/glfw3.h"
#include "glm/gtc/constants.hpp"
#include "../shared/ControllerPose.h"
#include "../shared/KeyMap.h"

static const glm::vec3 g_zeroPoint3(0.f);
static const glm::vec3 g_axisX(1.f, 0.f, 0.f);
static const glm::vec3 g_axisY(0.f, 1.f, 0.f);
static const glm::vec3 g_axisZ(0.f, 0.f, -1.f);

static const float g_mouseSense = 0.001f; // radians per mouse count
static const float g_moveSpeed = 2.f; // meters per second
static const float g_ctrlMoveSpeed = 0.5f; // meters per second, controller offset
static const float g_keyRotateSpeed = glm::pi<float>() / 4.f; // radians per second (numpad)
static const float g_pitchLimit = glm::radians(89.f);

CInputController::CInputController(GLFWwindow *f_window, CVRLink &f_link) : m_window(f_window), m_link(f_link)
{
    int l_defaults[KB_Count];
    for(int i = 0; i < KB_Count; i++)
    {
        l_defaults[i] = g_keyBindingDefs[i].m_defaultKey;
        m_actionHeld[i] = false;
    }
    SetBindings(l_defaults);
    for(size_t i = 0U; i < 2U; i++)
    {
        m_ctrlOffsetPos[i] = g_ctrlDefaultPosition[i];
        m_ctrlOffsetRot[i] = GetDefaultControllerRotation(i == 1U);
    }
}

void CInputController::SetBindings(const int *f_vkCodes)
{
    // VK codes are the persisted format; translate once for polling
    for(int i = 0; i < KB_Count; i++)
    {
        m_keyBindings[i] = f_vkCodes[i];
        m_glfwMouseButtons[i] = KeyMapMouseButton(m_keyBindings[i]);
        m_glfwKeys[i] = (m_glfwMouseButtons[i] < 0) ? KeyMapToGlfw(m_keyBindings[i]) : GLFW_KEY_UNKNOWN;
        m_glfwKeysAlternate[i] = (m_glfwMouseButtons[i] < 0) ? KeyMapToGlfwAlternate(m_keyBindings[i]) : GLFW_KEY_UNKNOWN;
        if((m_glfwMouseButtons[i] < 0) && (m_glfwKeys[i] == GLFW_KEY_UNKNOWN))
        {
            char l_line[128];
            snprintf(l_line, sizeof(l_line), "no GLFW mapping for VK 0x%02X (%s), binding disabled", m_keyBindings[i], g_keyBindingDefs[i].m_label);
            CPlatform::Log(l_line);
        }
    }
}

void CInputController::AdoptHeadPose(const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    m_position = f_pos;
    SetOrientationFromQuat(f_rot);
}

void CInputController::ResetSession()
{
    for(int i = 0; i < KB_Count; i++) m_actionHeld[i] = false;
    m_lastJoystick[0] = 0.f;
    m_lastJoystick[1] = 0.f;
    m_captureGuard = false;
    for(size_t i = 0U; i < 2U; i++)
    {
        m_ctrlOffsetPos[i] = g_ctrlDefaultPosition[i];
        m_ctrlOffsetRot[i] = GetDefaultControllerRotation(i == 1U);
    }
}

void CInputController::OnCaptured()
{
    m_captureGuard = true;
}

void CInputController::OnReleased()
{
    ReleaseHeldActions();
    m_captureGuard = false;
    // The one-shot and toggle edges would otherwise fire again on the next
    // capture if the key is still held
    m_actionHeld[KB_SwitchController] = false;
    m_actionHeld[KB_MouseAim] = false;
    m_actionHeld[KB_ResetHeadPose] = false;
    m_actionHeld[KB_ResetControllers] = false;
}

glm::quat CInputController::GetOrientation() const
{
    return glm::angleAxis(m_yaw, g_axisY) * glm::angleAxis(m_pitch, g_axisX) * glm::angleAxis(m_roll, g_axisZ);
}

void CInputController::SetOrientationFromQuat(const glm::quat &f_rot)
{
    const glm::vec3 l_forward = f_rot * g_axisZ;
    if(std::abs(l_forward.y) < 0.98f) m_yaw = std::atan2(-l_forward.x, -l_forward.z);
    else
    {
        // Looking (almost) straight up or down: the forward vector no longer
        // says where "ahead" is, but the head's up vector does - it leans
        // backward when looking up and forward when looking down. Without this
        // a saved pose at the pitch limit came back with an arbitrary yaw and
        // the difference folded into roll
        const glm::vec3 l_up = f_rot * g_axisY;
        m_yaw = (l_forward.y > 0.f) ? std::atan2(l_up.x, l_up.z) : std::atan2(-l_up.x, -l_up.z);
    }
    m_pitch = std::asin(glm::clamp(l_forward.y, -1.f, 1.f));
    m_pitch = glm::clamp(m_pitch, -g_pitchLimit, g_pitchLimit);

    const glm::quat l_residual = glm::inverse(glm::angleAxis(m_yaw, g_axisY) * glm::angleAxis(m_pitch, g_axisX)) * f_rot;
    m_roll = -glm::roll(l_residual);
}

bool CInputController::IsGlfwKeyDown(int f_key) const
{
    return (f_key != GLFW_KEY_UNKNOWN) && (glfwGetKey(m_window, f_key) == GLFW_PRESS);
}

bool CInputController::IsVkDown(int f_vk) const
{
    const int l_button = KeyMapMouseButton(f_vk);
    if(l_button >= 0) return (glfwGetMouseButton(m_window, l_button) == GLFW_PRESS);
    return IsGlfwKeyDown(KeyMapToGlfw(f_vk)) || IsGlfwKeyDown(KeyMapToGlfwAlternate(f_vk));
}

bool CInputController::IsBindingDown(EKeyBinding f_binding) const
{
    if(m_glfwMouseButtons[f_binding] >= 0)
    {
        if(m_captureGuard && (m_glfwMouseButtons[f_binding] == GLFW_MOUSE_BUTTON_LEFT)) return false;
        return (glfwGetMouseButton(m_window, m_glfwMouseButtons[f_binding]) == GLFW_PRESS);
    }
    return IsGlfwKeyDown(m_glfwKeys[f_binding]) || IsGlfwKeyDown(m_glfwKeysAlternate[f_binding]);
}

void CInputController::SendControllerOffset(bool f_left)
{
    m_link.SendControllerOffset(f_left, m_ctrlOffsetPos[f_left ? 1 : 0], m_ctrlOffsetRot[f_left ? 1 : 0]);
}

// Both controllers back to where they start: default offsets, aimed at the view centre
void CInputController::ResetControllerOffsets()
{
    for(size_t i = 0U; i < 2U; i++)
    {
        m_ctrlOffsetPos[i] = g_ctrlDefaultPosition[i];
        m_ctrlOffsetRot[i] = GetDefaultControllerRotation(i == 1U);
    }
    SendControllerOffset(false);
    SendControllerOffset(true);
}

// Send release for every held button-type action on the active controller
// (mouse release, focus loss, or switching the active controller). Deliberately
// leaves KB_SwitchController's edge state alone - clearing it would retrigger
// the switch on the next frame while the key is still held.
void CInputController::ReleaseHeldActions()
{
    const bool l_left = m_activeControllerLeft;
    if(m_actionHeld[KB_Trigger]) m_link.SendControllerTrigger(l_left, false);
    if(m_actionHeld[KB_Grip]) m_link.SendControllerButton(l_left, "grip", false);
    if(m_actionHeld[KB_SystemButton]) m_link.SendControllerSystem(l_left, false);
    if(m_actionHeld[KB_ButtonA]) m_link.SendControllerButton(l_left, l_left ? "x" : "a", false);
    if(m_actionHeld[KB_ButtonB]) m_link.SendControllerButton(l_left, l_left ? "y" : "b", false);
    m_actionHeld[KB_Trigger] = false;
    m_actionHeld[KB_Grip] = false;
    m_actionHeld[KB_SystemButton] = false;
    m_actionHeld[KB_ButtonA] = false;
    m_actionHeld[KB_ButtonB] = false;

    if((m_lastJoystick[0] != 0.f) || (m_lastJoystick[1] != 0.f))
    {
        m_lastJoystick[0] = 0.f;
        m_lastJoystick[1] = 0.f;
        m_link.SendControllerJoystick(l_left, 0.f, 0.f);
    }
}

void CInputController::Update(float f_delta, bool f_captured, float f_mouseDeltaX, float f_mouseDeltaY)
{
    if(!f_captured) return;

    // The click that captured the mouse must not fire the trigger binding
    if(m_captureGuard && (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)) m_captureGuard = false;

    // -1 = released this frame, +1 = pressed this frame, 0 = no change
    auto l_edge = [this](EKeyBinding f_binding) -> int
    {
        const bool l_down = IsBindingDown(f_binding);
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
    }
    if((l_change = l_edge(KB_MouseAim)) > 0) m_mouseAimController = !m_mouseAimController;

    const bool l_left = m_activeControllerLeft;
    // One-shot resets on key press: the head reset is applied below, before this
    // frame's movement, so held movement keys keep working through it
    const bool l_resetHead = (l_edge(KB_ResetHeadPose) > 0);
    if(l_edge(KB_ResetControllers) > 0) ResetControllerOffsets();
    if((l_change = l_edge(KB_Trigger)) != 0) m_link.SendControllerTrigger(l_left, l_change > 0);
    if((l_change = l_edge(KB_Grip)) != 0) m_link.SendControllerButton(l_left, "grip", l_change > 0);
    if((l_change = l_edge(KB_SystemButton)) != 0) m_link.SendControllerSystem(l_left, l_change > 0);
    if((l_change = l_edge(KB_ButtonA)) != 0) m_link.SendControllerButton(l_left, l_left ? "x" : "a", l_change > 0);
    if((l_change = l_edge(KB_ButtonB)) != 0) m_link.SendControllerButton(l_left, l_left ? "y" : "b", l_change > 0);

    // Digital joystick emulation on the active controller
    const float l_joyX = (IsBindingDown(KB_JoystickRight) ? 1.f : 0.f) - (IsBindingDown(KB_JoystickLeft) ? 1.f : 0.f);
    const float l_joyY = (IsBindingDown(KB_JoystickUp) ? 1.f : 0.f) - (IsBindingDown(KB_JoystickDown) ? 1.f : 0.f);
    if((l_joyX != m_lastJoystick[0]) || (l_joyY != m_lastJoystick[1]))
    {
        m_lastJoystick[0] = l_joyX;
        m_lastJoystick[1] = l_joyY;
        m_link.SendControllerJoystick(l_left, l_joyX, l_joyY);
    }

    // IJKL: move the active controller in head-local space
    glm::vec3 l_ctrlMove(0.f);
    if(IsBindingDown(KB_ControllerForward)) l_ctrlMove.z -= 1.f;
    if(IsBindingDown(KB_ControllerBackward)) l_ctrlMove.z += 1.f;
    if(IsBindingDown(KB_ControllerLeft)) l_ctrlMove.x -= 1.f;
    if(IsBindingDown(KB_ControllerRight)) l_ctrlMove.x += 1.f;
    if(l_ctrlMove != g_zeroPoint3)
    {
        m_ctrlOffsetPos[l_left ? 1 : 0] += l_ctrlMove * (g_ctrlMoveSpeed * f_delta);
        SendControllerOffset(l_left);
    }

    float l_yawDelta = -f_mouseDeltaX * g_mouseSense;
    float l_pitchDelta = -f_mouseDeltaY * g_mouseSense;

    if(m_mouseAimController)
    {
        // Mouse steers the active controller's pointer direction; the head only
        // rotates via the numpad keys while this mode is on
        if((l_yawDelta != 0.f) || (l_pitchDelta != 0.f))
        {
            const size_t l_active = (l_left ? 1 : 0);
            m_ctrlOffsetRot[l_active] = glm::angleAxis(l_yawDelta, g_axisY) * glm::angleAxis(l_pitchDelta, g_axisX) * m_ctrlOffsetRot[l_active];
            SendControllerOffset(l_left);
        }
        l_yawDelta = 0.f;
        l_pitchDelta = 0.f;
    }

    // Local movement intent: x = right, y = up, z = forward
    glm::vec3 l_move(0.f);
    if(IsBindingDown(KB_MoveForward)) l_move.z += 1.f;
    if(IsBindingDown(KB_MoveBackward)) l_move.z -= 1.f;
    if(IsBindingDown(KB_MoveLeft)) l_move.x -= 1.f;
    if(IsBindingDown(KB_MoveRight)) l_move.x += 1.f;
    if(IsBindingDown(KB_MoveUp)) l_move.y += 1.f;
    if(IsBindingDown(KB_MoveDown)) l_move.y -= 1.f;

    float l_keyYaw = 0.f;
    float l_keyPitch = 0.f;
    float l_keyRoll = 0.f;
    if(IsBindingDown(KB_YawLeft)) l_keyYaw += g_keyRotateSpeed * f_delta;
    if(IsBindingDown(KB_YawRight)) l_keyYaw -= g_keyRotateSpeed * f_delta;
    if(IsBindingDown(KB_PitchDown)) l_keyPitch -= g_keyRotateSpeed * f_delta;
    if(IsBindingDown(KB_PitchUp)) l_keyPitch += g_keyRotateSpeed * f_delta;
    if(IsBindingDown(KB_RollLeft)) l_keyRoll -= g_keyRotateSpeed * f_delta;
    if(IsBindingDown(KB_RollRight)) l_keyRoll += g_keyRotateSpeed * f_delta;

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
        glm::vec3 l_shift = l_yawRotation * glm::vec3(l_move.x, 0.f, -l_move.z);
        l_shift.y += l_move.y;
        m_position += l_shift * (g_moveSpeed * f_delta);
        l_changed = true;
    }
    if(l_changed) m_link.SendHmdPose(m_position, GetOrientation());
}
