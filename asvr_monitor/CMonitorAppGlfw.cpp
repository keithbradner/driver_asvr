#include "CMonitorAppGlfw.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "GLFW/glfw3.h"
#include "glm/gtc/constants.hpp"

#include "../shared/KeyMap.h"
#include "../shared/SharedFrameInfo.h"

static const glm::vec3 g_zeroPoint3(0.f);
static const glm::vec4 g_zeroPoint4(0.f, 0.f, 0.f, 1.f);
static const glm::quat g_zeroRotation(1.f, 0.f, 0.f, 0.f);
static const glm::vec3 g_axisX(1.f, 0.f, 0.f);
static const glm::vec3 g_axisY(0.f, 1.f, 0.f);
static const glm::vec3 g_axisZ(0.f, 0.f, -1.f);

static const char* const g_controllerSerial = "asvr_controller_0001"; // virtual right hand
static const char* const g_leftControllerSerial = "asvr_controller_0002"; // virtual left hand

static const float g_mouseSense = 0.001f; // radians per mouse count
static const float g_moveSpeed = 2.f; // meters per second
static const float g_ctrlMoveSpeed = 0.5f; // meters per second, controller offset
static const float g_keyRotateSpeed = glm::pi<float>() / 4.f; // radians per second (numpad)
static const float g_pitchLimit = glm::radians(89.f);

// Head-local controller offsets at startup and after a reset, [0] = right,
// [1] = left. Must match the driver's CControllerDevice constructor defaults
static const glm::vec3 g_ctrlDefaultOffset[2] = { glm::vec3(0.15f, -0.15f, -0.35f), glm::vec3(-0.15f, -0.15f, -0.35f) };

static std::string LogFilePath()
{
    const char *l_stateHome = std::getenv("XDG_STATE_HOME");
    std::string l_directory;
    if(l_stateHome && l_stateHome[0]) l_directory.assign(l_stateHome);
    else
    {
        const char *l_home = std::getenv("HOME");
        if(l_home && l_home[0]) l_directory = std::string(l_home) + "/.local/state";
    }
    if(!l_directory.empty())
    {
        l_directory += "/asvr";
        if((mkdir(l_directory.c_str(), 0755) == 0) || (errno == EEXIST)) return l_directory + "/asvr_monitor.log";
    }
    return "/tmp/asvr_monitor." + std::to_string(getuid()) + ".log";
}

static void LogLine(const char *f_text)
{
    static const std::string s_path = LogFilePath();
    static bool s_appending = false;
    FILE *l_stream = fopen(s_path.c_str(), s_appending ? "a" : "w"); // fresh log every run
    if(!l_stream) return;
    s_appending = true;
    timespec l_now;
    clock_gettime(CLOCK_REALTIME, &l_now);
    tm l_time;
    localtime_r(&l_now.tv_sec, &l_time);
    fprintf(l_stream, "[%02d:%02d:%02d.%03ld] %s\n", l_time.tm_hour, l_time.tm_min, l_time.tm_sec, l_now.tv_nsec / 1000000L, f_text);
    fclose(l_stream);
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

CMonitorAppGlfw::CMonitorAppGlfw()
{
    for(int i = 0; i < KB_Count; i++)
    {
        m_keyBindings[i] = g_keyBindingDefs[i].m_defaultKey;
        m_glfwKeys[i] = GLFW_KEY_UNKNOWN;
        m_glfwMouseButtons[i] = -1;
        m_actionHeld[i] = false;
    }
    for(size_t i = 0U; i < 2U; i++)
    {
        m_ctrlOffsetPos[i] = g_ctrlDefaultOffset[i];
        m_ctrlOffsetRot[i] = g_zeroRotation;
    }
}

bool CMonitorAppGlfw::Init()
{
    if(m_active) return true;

    vr::EVRInitError l_error;
    // Background requires a running vrserver, which is the case when the driver launches us
    m_vrSystem = vr::VR_Init(&l_error, vr::VRApplication_Background);
    if(l_error != vr::VRInitError_None) m_vrSystem = vr::VR_Init(&l_error, vr::VRApplication_Utility);
    if(l_error != vr::VRInitError_None)
    {
        char l_line[128];
        snprintf(l_line, sizeof(l_line), "VR_Init failed: %d", l_error);
        LogLine(l_line);
        return false;
    }

    FindControllerDevice();
    LoadKeyBindings();

    // Adopt the HMD's current pose as our authoritative state
    vr::TrackedDevicePose_t l_poses[vr::k_unMaxTrackedDeviceCount];
    m_vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated, 0.f, l_poses, vr::k_unMaxTrackedDeviceCount);
    if(l_poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
    {
        const glm::mat4 l_headTransform = ToMat4(l_poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
        m_position = l_headTransform * g_zeroPoint4;
        SetOrientationFromQuat(glm::quat_cast(l_headTransform));
    }

    if(!glfwInit())
    {
        LogLine("glfwInit failed (no display?)");
        vr::VR_Shutdown();
        m_vrSystem = nullptr;
        return false;
    }
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(768, 768, "ASVR", nullptr, nullptr);
    if(!m_window)
    {
        LogLine("glfwCreateWindow failed");
        glfwTerminate();
        vr::VR_Shutdown();
        m_vrSystem = nullptr;
        return false;
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetMouseButtonCallback(m_window, [](GLFWwindow *f_window, int f_button, int f_action, int /*f_mods*/)
    {
        CMonitorAppGlfw *l_app = static_cast<CMonitorAppGlfw*>(glfwGetWindowUserPointer(f_window));
        if((f_button == GLFW_MOUSE_BUTTON_LEFT) && (f_action == GLFW_PRESS) && !l_app->m_captured) l_app->CaptureMouse();
    });
    glfwSetKeyCallback(m_window, [](GLFWwindow *f_window, int f_key, int /*f_scancode*/, int f_action, int /*f_mods*/)
    {
        CMonitorAppGlfw *l_app = static_cast<CMonitorAppGlfw*>(glfwGetWindowUserPointer(f_window));
        if((f_key == GLFW_KEY_ESCAPE) && (f_action == GLFW_PRESS) && l_app->m_captured) l_app->ReleaseMouse();
    });
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    UpdateTitle();
    m_lastTick = std::chrono::steady_clock::now();
    m_active = true;
    LogLine("monitor up");
    return true;
}

void CMonitorAppGlfw::Shutdown()
{
    if(!m_active) return;
    if(m_captured) ReleaseMouse();

    if(m_frameInfo)
    {
        munmap(const_cast<SharedFramePixelsInfo*>(m_frameInfo), m_frameMappingSize);
        m_frameInfo = nullptr;
    }
    if(m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();

    vr::VR_Shutdown();
    m_vrSystem = nullptr;
    m_active = false;
}

void CMonitorAppGlfw::CaptureMouse()
{
    m_captured = true;
    m_captureGuard = true; // the click that captured must not fire the trigger binding
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if(glfwRawMouseMotionSupported()) glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwGetCursorPos(m_window, &m_lastCursorX, &m_lastCursorY);
    UpdateTitle();
}

void CMonitorAppGlfw::ReleaseMouse()
{
    ReleaseHeldActions();
    m_captured = false;
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    UpdateTitle();
}

void CMonitorAppGlfw::UpdateTitle()
{
    std::string l_title("ASVR - ");
    l_title += (m_activeControllerLeft ? "left" : "right");
    l_title += " controller";
    if(m_mouseAimController) l_title += " - mouse aims controller";
    l_title += (m_captured ? " - Esc releases the mouse" : " - click to control");
    glfwSetWindowTitle(m_window, l_title.c_str());
}

// VR communication
glm::quat CMonitorAppGlfw::GetOrientation() const
{
    return glm::angleAxis(m_yaw, g_axisY) * glm::angleAxis(m_pitch, g_axisX) * glm::angleAxis(m_roll, g_axisZ);
}

void CMonitorAppGlfw::SetOrientationFromQuat(const glm::quat &f_rot)
{
    const glm::vec3 l_forward = f_rot * g_axisZ;
    m_yaw = std::atan2(-l_forward.x, -l_forward.z);
    m_pitch = std::asin(glm::clamp(l_forward.y, -1.f, 1.f));
    m_pitch = glm::clamp(m_pitch, -g_pitchLimit, g_pitchLimit);

    const glm::quat l_residual = glm::inverse(glm::angleAxis(m_yaw, g_axisY) * glm::angleAxis(m_pitch, g_axisX)) * f_rot;
    m_roll = -glm::roll(l_residual);
}

vr::TrackedDeviceIndex_t CMonitorAppGlfw::ActiveControllerIndex() const
{
    return (m_activeControllerLeft ? m_leftControllerDevice : m_controllerDevice);
}

void CMonitorAppGlfw::SendDebugRequest(vr::TrackedDeviceIndex_t f_device, const std::string &f_message)
{
    if(f_device == vr::k_unTrackedDeviceIndexInvalid) return;
    char l_response[32];
    std::memset(l_response, 0, sizeof(l_response));
    vr::VRDebug()->DriverDebugRequest(f_device, f_message.c_str(), l_response, sizeof(l_response));
}

void CMonitorAppGlfw::SendHmdPose()
{
    const glm::quat l_rotation = GetOrientation();
    const float l_pose[7] = { m_position.x, m_position.y, m_position.z, l_rotation.x, l_rotation.y, l_rotation.z, l_rotation.w };
    std::string l_message("setpose ");
    AppendFloats(l_message, l_pose, 7);
    SendDebugRequest(vr::k_unTrackedDeviceIndex_Hmd, l_message);
}

void CMonitorAppGlfw::SendControllerOffset(bool f_left)
{
    const vr::TrackedDeviceIndex_t l_device = (f_left ? m_leftControllerDevice : m_controllerDevice);
    const glm::vec3 &l_pos = m_ctrlOffsetPos[f_left ? 1 : 0];
    const glm::quat &l_rot = m_ctrlOffsetRot[f_left ? 1 : 0];
    const float l_offset[7] = { l_pos.x, l_pos.y, l_pos.z, l_rot.x, l_rot.y, l_rot.z, l_rot.w };
    std::string l_message("offset ");
    AppendFloats(l_message, l_offset, 7);
    SendDebugRequest(l_device, l_message);
}

void CMonitorAppGlfw::SendControllerTrigger(bool f_state)
{
    SendDebugRequest(ActiveControllerIndex(), f_state ? "trigger 1" : "trigger 0");
}

void CMonitorAppGlfw::SendControllerSystem(bool f_state)
{
    SendDebugRequest(ActiveControllerIndex(), f_state ? "system 1" : "system 0");
}

void CMonitorAppGlfw::SendControllerButton(const char *f_button, bool f_state)
{
    std::string l_message("button ");
    l_message.append(f_button);
    l_message.append(f_state ? " 1" : " 0");
    SendDebugRequest(ActiveControllerIndex(), l_message);
}

void CMonitorAppGlfw::SendControllerJoystick(float f_x, float f_y)
{
    std::string l_message("joystick ");
    l_message.append(std::to_string(f_x));
    l_message.push_back(' ');
    l_message.append(std::to_string(f_y));
    SendDebugRequest(ActiveControllerIndex(), l_message);
}

// Send release for every held button-type action on the active controller
// (mouse release, focus loss, or switching the active controller). Deliberately
// leaves KB_SwitchController's edge state alone - clearing it would retrigger
// the switch on the next frame while the key is still held.
// Both controllers back to where they start: default offsets, no aim rotation
void CMonitorAppGlfw::ResetControllerOffsets()
{
    for(size_t i = 0U; i < 2U; i++)
    {
        m_ctrlOffsetPos[i] = g_ctrlDefaultOffset[i];
        m_ctrlOffsetRot[i] = g_zeroRotation;
    }
    SendControllerOffset(false);
    SendControllerOffset(true);
}

void CMonitorAppGlfw::ReleaseHeldActions()
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

    if((m_lastJoystick[0] != 0.f) || (m_lastJoystick[1] != 0.f))
    {
        m_lastJoystick[0] = 0.f;
        m_lastJoystick[1] = 0.f;
        SendControllerJoystick(0.f, 0.f);
    }
}

void CMonitorAppGlfw::LoadKeyBindings()
{
    // User overrides written by the Config tool; defaults come from the driver's
    // default.vrsettings (with the compiled-in table as a last resort)
    vr::IVRSettings *l_settings = vr::VRSettings();
    if(l_settings)
    {
        for(int i = 0; i < KB_Count; i++)
        {
            vr::EVRSettingsError l_error = vr::VRSettingsError_None;
            const int32_t l_key = l_settings->GetInt32(g_driverSettingsSection, g_keyBindingDefs[i].m_settingsKey, &l_error);
            if((l_error == vr::VRSettingsError_None) && (l_key > 0) && (l_key < 256)) m_keyBindings[i] = l_key;
        }
    }

    // VK codes are the persisted format; translate once for polling
    for(int i = 0; i < KB_Count; i++)
    {
        m_glfwMouseButtons[i] = KeyMapMouseButton(m_keyBindings[i]);
        m_glfwKeys[i] = (m_glfwMouseButtons[i] < 0) ? KeyMapToGlfw(m_keyBindings[i]) : GLFW_KEY_UNKNOWN;
        if((m_glfwMouseButtons[i] < 0) && (m_glfwKeys[i] == GLFW_KEY_UNKNOWN))
        {
            char l_line[128];
            snprintf(l_line, sizeof(l_line), "no GLFW mapping for VK 0x%02X (%s), binding disabled", m_keyBindings[i], g_keyBindingDefs[i].m_label);
            LogLine(l_line);
        }
    }
}

void CMonitorAppGlfw::FindControllerDevice()
{
    for(vr::TrackedDeviceIndex_t i = 0U; i < vr::k_unMaxTrackedDeviceCount; i++)
    {
        char l_serial[64] = { 0 };
        vr::ETrackedPropertyError l_propError = vr::TrackedProp_Success;
        m_vrSystem->GetStringTrackedDeviceProperty(i, vr::Prop_SerialNumber_String, l_serial, sizeof(l_serial), &l_propError);
        if(l_propError != vr::TrackedProp_Success) continue;
        if(!strcmp(l_serial, g_controllerSerial)) m_controllerDevice = i;
        else if(!strcmp(l_serial, g_leftControllerSerial)) m_leftControllerDevice = i;
    }
}

void CMonitorAppGlfw::ProcessInput(float f_delta)
{
    const bool l_focused = (glfwGetWindowAttrib(m_window, GLFW_FOCUSED) != 0);
    if(!l_focused && m_captured) ReleaseMouse();
    if(!l_focused || !m_captured)
    {
        m_mouseDeltaX = 0.f;
        m_mouseDeltaY = 0.f;
        return;
    }

    // The click that captured the mouse must not fire the trigger binding
    if(m_captureGuard && (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)) m_captureGuard = false;

    auto l_isDown = [this](EKeyBinding f_binding) -> bool
    {
        if(m_glfwMouseButtons[f_binding] >= 0)
        {
            if(m_captureGuard && (m_glfwMouseButtons[f_binding] == GLFW_MOUSE_BUTTON_LEFT)) return false;
            return (glfwGetMouseButton(m_window, m_glfwMouseButtons[f_binding]) == GLFW_PRESS);
        }
        if(m_glfwKeys[f_binding] == GLFW_KEY_UNKNOWN) return false;
        return (glfwGetKey(m_window, m_glfwKeys[f_binding]) == GLFW_PRESS);
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
    if((l_joyX != m_lastJoystick[0]) || (l_joyY != m_lastJoystick[1]))
    {
        m_lastJoystick[0] = l_joyX;
        m_lastJoystick[1] = l_joyY;
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
        m_ctrlOffsetPos[m_activeControllerLeft ? 1 : 0] += l_ctrlMove * (g_ctrlMoveSpeed * f_delta);
        SendControllerOffset(m_activeControllerLeft);
    }

    float l_yawDelta = -m_mouseDeltaX * g_mouseSense;
    float l_pitchDelta = -m_mouseDeltaY * g_mouseSense;
    m_mouseDeltaX = 0.f;
    m_mouseDeltaY = 0.f;

    if(m_mouseAimController)
    {
        // Mouse steers the active controller's pointer direction; the head only
        // rotates via the numpad keys while this mode is on
        if((l_yawDelta != 0.f) || (l_pitchDelta != 0.f))
        {
            const size_t l_active = (m_activeControllerLeft ? 1 : 0);
            m_ctrlOffsetRot[l_active] = glm::angleAxis(l_yawDelta, g_axisY) * glm::angleAxis(l_pitchDelta, g_axisX) * m_ctrlOffsetRot[l_active];
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
    if(l_isDown(KB_YawLeft)) l_keyYaw += g_keyRotateSpeed * f_delta;
    if(l_isDown(KB_YawRight)) l_keyYaw -= g_keyRotateSpeed * f_delta;
    if(l_isDown(KB_PitchDown)) l_keyPitch -= g_keyRotateSpeed * f_delta;
    if(l_isDown(KB_PitchUp)) l_keyPitch += g_keyRotateSpeed * f_delta;
    if(l_isDown(KB_RollLeft)) l_keyRoll -= g_keyRotateSpeed * f_delta;
    if(l_isDown(KB_RollRight)) l_keyRoll += g_keyRotateSpeed * f_delta;

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
    if(l_changed) SendHmdPose();
}

void CMonitorAppGlfw::UpdateSharedFrame()
{
    if(m_frameInfo) return;
    if(m_frameMapRetry-- > 0) return;
    m_frameMapRetry = 60; // once a second at 60fps until the driver publishes

    const int l_fd = shm_open(g_sharedFramePixelsName, O_RDONLY, 0);
    if(l_fd < 0) return;

    SharedFramePixelsInfo l_header = {};
    void *l_view = mmap(nullptr, sizeof(SharedFramePixelsInfo), PROT_READ, MAP_SHARED, l_fd, 0);
    if(l_view == MAP_FAILED)
    {
        close(l_fd);
        return;
    }
    memcpy(&l_header, l_view, sizeof(l_header));
    munmap(l_view, sizeof(SharedFramePixelsInfo));
    if((l_header.magic != g_sharedFramePixelsMagic) || (l_header.slotSize == 0U))
    {
        close(l_fd);
        return;
    }

    m_frameMappingSize = static_cast<size_t>(l_header.slotOffset[1]) + l_header.slotSize;
    const void *l_full = mmap(nullptr, m_frameMappingSize, PROT_READ, MAP_SHARED, l_fd, 0);
    close(l_fd);
    if(l_full == MAP_FAILED) return;
    m_frameInfo = static_cast<const SharedFramePixelsInfo*>(l_full);
    LogLine("shared frame block mapped");
}

void CMonitorAppGlfw::RenderFrame()
{
    int l_width = 0;
    int l_height = 0;
    glfwGetFramebufferSize(m_window, &l_width, &l_height);
    if((l_width <= 0) || (l_height <= 0)) return;

    glViewport(0, 0, l_width, l_height);
    glClearColor(0.05f, 0.05f, 0.08f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if(m_frameInfo && (m_frameInfo->frameCounter != 0U))
    {
        if((m_textureWidth != m_frameInfo->width) || (m_textureHeight != m_frameInfo->height))
        {
            m_textureWidth = m_frameInfo->width;
            m_textureHeight = m_frameInfo->height;
            glBindTexture(GL_TEXTURE_2D, m_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_textureWidth, m_textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        if(m_frameInfo->frameCounter != m_shownFrame)
        {
            m_shownFrame = m_frameInfo->frameCounter;
            const char *l_pixels = reinterpret_cast<const char*>(m_frameInfo) + m_frameInfo->slotOffset[m_frameInfo->latestSlot & 1U];
            glBindTexture(GL_TEXTURE_2D, m_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_textureWidth, m_textureHeight, GL_RGBA, GL_UNSIGNED_BYTE, l_pixels);
        }

        // Centered square viewport - the driver's projection is a symmetric
        // square FOV, matching the Windows monitor's letterboxing
        const int l_side = (l_width < l_height) ? l_width : l_height;
        glViewport((l_width - l_side) / 2, (l_height - l_side) / 2, l_side, l_side);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glBegin(GL_QUADS);
        // Left eye = left half of the side-by-side texture; v flipped so the
        // top pixel row lands at the top of the window
        glTexCoord2f(0.0f, 0.f);
        glVertex2f(-1.f, 1.f);
        glTexCoord2f(0.5f, 0.f);
        glVertex2f(1.f, 1.f);
        glTexCoord2f(0.5f, 1.f);
        glVertex2f(1.f, -1.f);
        glTexCoord2f(0.0f, 1.f);
        glVertex2f(-1.f, -1.f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

    glfwSwapBuffers(m_window);
}

bool CMonitorAppGlfw::Update()
{
    glfwPollEvents();
    if(glfwWindowShouldClose(m_window)) return false;

    // Mouse-look deltas: with the cursor disabled GLFW reports an unbounded
    // virtual position; consecutive differences are the raw motion
    if(m_captured)
    {
        double l_cursorX = 0.0;
        double l_cursorY = 0.0;
        glfwGetCursorPos(m_window, &l_cursorX, &l_cursorY);
        m_mouseDeltaX += static_cast<float>(l_cursorX - m_lastCursorX);
        m_mouseDeltaY += static_cast<float>(l_cursorY - m_lastCursorY);
        m_lastCursorX = l_cursorX;
        m_lastCursorY = l_cursorY;
    }

    const std::chrono::steady_clock::time_point l_now = std::chrono::steady_clock::now();
    float l_delta = std::chrono::duration<float>(l_now - m_lastTick).count();
    m_lastTick = l_now;
    l_delta = glm::clamp(l_delta, 0.f, 0.1f);

    vr::VREvent_t l_event;
    while(m_vrSystem->PollNextEvent(&l_event, sizeof(vr::VREvent_t)))
    {
        switch(l_event.eventType)
        {
            case vr::VREvent_Quit:
                return false;
            case vr::VREvent_TrackedDeviceDeactivated:
            {
                if(m_controllerDevice == l_event.trackedDeviceIndex) m_controllerDevice = vr::k_unTrackedDeviceIndexInvalid;
                if(m_leftControllerDevice == l_event.trackedDeviceIndex) m_leftControllerDevice = vr::k_unTrackedDeviceIndexInvalid;
            } break;
            case vr::VREvent_TrackedDeviceActivated:
            {
                if((m_controllerDevice == vr::k_unTrackedDeviceIndexInvalid) || (m_leftControllerDevice == vr::k_unTrackedDeviceIndexInvalid)) FindControllerDevice();
            } break;
        }
    }

    ProcessInput(l_delta);
    UpdateSharedFrame();
    RenderFrame();

    return true;
}
