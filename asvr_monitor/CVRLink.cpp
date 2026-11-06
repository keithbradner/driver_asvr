#include "CVRLink.h"
#include "CPlatform.h"

#include <cstdio>
#include <cstring>

static const char* const g_hmdSerial = "asvr_hmd_0001"; // virtual headset
static const char* const g_controllerSerial = "asvr_controller_0001"; // virtual right hand
static const char* const g_leftControllerSerial = "asvr_controller_0002"; // virtual left hand

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

CVRLink::~CVRLink()
{
    Disconnect();
}

bool CVRLink::Connect(vr::EVRInitError &f_error)
{
    if(m_vrSystem)
    {
        f_error = vr::VRInitError_None;
        return true;
    }
    // Background: never launches SteamVR (Utility would start vrserver in a
    // headless mode, and Scene would be an app with a headset)
    m_vrSystem = vr::VR_Init(&f_error, vr::VRApplication_Background);
    if(f_error != vr::VRInitError_None)
    {
        m_vrSystem = nullptr;
        return false;
    }
    FindControllerDevices();
    CheckHmdOwnership();
    CPlatform::Log("Connected to SteamVR");
    return true;
}

void CVRLink::Disconnect()
{
    if(!m_vrSystem) return;
    vr::VR_Shutdown();
    m_vrSystem = nullptr;
    m_controllerDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_leftControllerDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_foreignHmd = false;
    m_hmdSerial.clear();
    CPlatform::Log("Disconnected from SteamVR");
}

bool CVRLink::PollEvents()
{
    if(!m_vrSystem) return false;
    vr::VREvent_t l_event;
    while(m_vrSystem->PollNextEvent(&l_event, sizeof(vr::VREvent_t)))
    {
        switch(l_event.eventType)
        {
            case vr::VREvent_Quit:
            {
                // Let vrserver finish shutting down without waiting on us
                m_vrSystem->AcknowledgeQuit_Exiting();
                CPlatform::Log("SteamVR is shutting down");
                Disconnect();
                return false;
            }
            case vr::VREvent_TrackedDeviceDeactivated:
            {
                if(m_controllerDevice == l_event.trackedDeviceIndex) m_controllerDevice = vr::k_unTrackedDeviceIndexInvalid;
                if(m_leftControllerDevice == l_event.trackedDeviceIndex) m_leftControllerDevice = vr::k_unTrackedDeviceIndexInvalid;
            } break;
            case vr::VREvent_TrackedDeviceActivated:
            {
                if((m_controllerDevice == vr::k_unTrackedDeviceIndexInvalid) || (m_leftControllerDevice == vr::k_unTrackedDeviceIndexInvalid)) FindControllerDevices();
                if(l_event.trackedDeviceIndex == vr::k_unTrackedDeviceIndex_Hmd) CheckHmdOwnership();
            } break;
        }
    }
    return true;
}

bool CVRLink::HasController(bool f_left) const
{
    return ((f_left ? m_leftControllerDevice : m_controllerDevice) != vr::k_unTrackedDeviceIndexInvalid);
}

bool CVRLink::ReadHmdPose(glm::vec3 &f_pos, glm::quat &f_rot) const
{
    if(!m_vrSystem || m_foreignHmd) return false;
    vr::TrackedDevicePose_t l_poses[vr::k_unMaxTrackedDeviceCount];
    m_vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated, 0.f, l_poses, vr::k_unMaxTrackedDeviceCount);
    if(!l_poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid) return false;
    const glm::mat4 l_headTransform = ToMat4(l_poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
    f_pos = glm::vec3(l_headTransform * glm::vec4(0.f, 0.f, 0.f, 1.f));
    f_rot = glm::quat_cast(l_headTransform);
    return true;
}

void CVRLink::SendDebugRequest(vr::TrackedDeviceIndex_t f_device, const std::string &f_message)
{
    if(!m_vrSystem || (f_device == vr::k_unTrackedDeviceIndexInvalid)) return;
    char l_response[32];
    memset(l_response, 0, sizeof(l_response));
    vr::VRDebug()->DriverDebugRequest(f_device, f_message.c_str(), l_response, sizeof(l_response));
}

void CVRLink::SendHmdPose(const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    if(m_foreignHmd) return; // index 0 is someone else's headset; its driver would get our setpose
    const float l_pose[7] = { f_pos.x, f_pos.y, f_pos.z, f_rot.x, f_rot.y, f_rot.z, f_rot.w };
    std::string l_message("setpose ");
    AppendFloats(l_message, l_pose, 7);
    SendDebugRequest(vr::k_unTrackedDeviceIndex_Hmd, l_message);
}

void CVRLink::SendControllerOffset(bool f_left, const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    const float l_offset[7] = { f_pos.x, f_pos.y, f_pos.z, f_rot.x, f_rot.y, f_rot.z, f_rot.w };
    std::string l_message("offset ");
    AppendFloats(l_message, l_offset, 7);
    SendDebugRequest(f_left ? m_leftControllerDevice : m_controllerDevice, l_message);
}

void CVRLink::SendControllerTrigger(bool f_left, bool f_state)
{
    SendDebugRequest(f_left ? m_leftControllerDevice : m_controllerDevice, f_state ? "trigger 1" : "trigger 0");
}

void CVRLink::SendControllerSystem(bool f_left, bool f_state)
{
    SendDebugRequest(f_left ? m_leftControllerDevice : m_controllerDevice, f_state ? "system 1" : "system 0");
}

void CVRLink::SendControllerButton(bool f_left, const char *f_button, bool f_state)
{
    std::string l_message("button ");
    l_message.append(f_button);
    l_message.append(f_state ? " 1" : " 0");
    SendDebugRequest(f_left ? m_leftControllerDevice : m_controllerDevice, l_message);
}

void CVRLink::SendControllerJoystick(bool f_left, float f_x, float f_y)
{
    std::string l_message("joystick ");
    l_message.append(std::to_string(f_x));
    l_message.push_back(' ');
    l_message.append(std::to_string(f_y));
    SendDebugRequest(f_left ? m_leftControllerDevice : m_controllerDevice, l_message);
}

void CVRLink::FindControllerDevices()
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

// With a real headset connected its driver usually registers first and our
// virtual HMD is refused (the driver then starts nothing else either). Index 0
// is that headset, so head control stays off and the window says why.
void CVRLink::CheckHmdOwnership()
{
    char l_serial[64] = { 0 };
    vr::ETrackedPropertyError l_propError = vr::TrackedProp_Success;
    m_vrSystem->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String, l_serial, sizeof(l_serial), &l_propError);
    m_hmdSerial.assign((l_propError == vr::TrackedProp_Success) ? l_serial : "");
    m_foreignHmd = ((l_propError == vr::TrackedProp_Success) && (strcmp(l_serial, g_hmdSerial) != 0));
    if(m_foreignHmd) CPlatform::Log("HMD slot is owned by '" + m_hmdSerial + "', not " + g_hmdSerial + ": head control disabled");
}
