#include "stdafx.h"

#include "CTrackerDevice.h"

// Default head-relative offsets for a standing user with the eye at about 1.65 m
// (x right, y up, z back): hips 0.75 m below the eye, feet 1.6 m below and 0.12 m apart
static glm::vec3 DefaultOffset(const std::string &f_role)
{
    if(!f_role.compare("waist")) return glm::vec3(0.f, -0.75f, 0.f);
    if(!f_role.compare("chest")) return glm::vec3(0.f, -0.45f, -0.02f);
    if(!f_role.compare("left_foot")) return glm::vec3(-0.12f, -1.6f, 0.f);
    if(!f_role.compare("right_foot")) return glm::vec3(0.12f, -1.6f, 0.f);
    if(!f_role.compare("left_knee")) return glm::vec3(-0.12f, -1.15f, -0.05f);
    if(!f_role.compare("right_knee")) return glm::vec3(0.12f, -1.15f, -0.05f);
    if(!f_role.compare("left_elbow")) return glm::vec3(-0.4f, -0.45f, -0.1f);
    if(!f_role.compare("right_elbow")) return glm::vec3(0.4f, -0.45f, -0.1f);
    if(!f_role.compare("left_shoulder")) return glm::vec3(-0.18f, -0.25f, 0.f);
    if(!f_role.compare("right_shoulder")) return glm::vec3(0.18f, -0.25f, 0.f);
    return glm::vec3(0.3f, -0.3f, -0.4f); // handheld
}

const std::vector<std::pair<std::string, std::string>>& CTrackerDevice::KnownRoles()
{
    static const std::vector<std::pair<std::string, std::string>> s_roles
    {
        { "waist", "TrackerRole_Waist" },
        { "chest", "TrackerRole_Chest" },
        { "left_foot", "TrackerRole_LeftFoot" },
        { "right_foot", "TrackerRole_RightFoot" },
        { "left_knee", "TrackerRole_LeftKnee" },
        { "right_knee", "TrackerRole_RightKnee" },
        { "left_elbow", "TrackerRole_LeftElbow" },
        { "right_elbow", "TrackerRole_RightElbow" },
        { "left_shoulder", "TrackerRole_LeftShoulder" },
        { "right_shoulder", "TrackerRole_RightShoulder" },
        { "handheld", "TrackerRole_Handed" }
    };
    return s_roles;
}

bool CTrackerDevice::IsKnownRole(const std::string &f_role)
{
    for(const auto &l_role : KnownRoles())
    {
        if(!l_role.first.compare(f_role)) return true;
    }
    return false;
}

CTrackerDevice::CTrackerDevice(const std::string &f_role)
{
    m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_propertyContainer = vr::k_ulInvalidPropertyContainer;
    m_role = f_role;
    m_serial = "asvr_tracker_" + f_role;
    m_position = DefaultOffset(f_role);
    m_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    m_absolute = false;
    m_tracking = true;
    m_power = m_triggerClick = m_triggerValue = m_grip = m_menu = m_thumb = m_haptic = vr::k_ulInvalidInputComponentHandle;

    m_pose = { 0 };
    m_pose.poseTimeOffset = 0.f;
    m_pose.qWorldFromDriverRotation.w = 1.0;
    m_pose.qDriverFromHeadRotation.w = 1.0;
    m_pose.qRotation.w = 1.0;
    m_pose.poseIsValid = false;
    m_pose.willDriftInYaw = false;
    m_pose.shouldApplyHeadModel = false;
    m_pose.result = vr::TrackingResult_Uninitialized;
    m_pose.deviceIsConnected = false;
}

CTrackerDevice::~CTrackerDevice()
{
}

// vr::ITrackedDeviceServerDriver
vr::EVRInitError CTrackerDevice::Activate(uint32_t unObjectId)
{
    m_trackedDevice = unObjectId;
    m_propertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_trackedDevice);

    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_SerialNumber_String, m_serial.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ModelNumber_String, "asvr_tracker");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ManufacturerName_String, "ASVR");
    // Presented as a Vive tracker: SteamVR's role handling, bindings and its OpenXR
    // vive_tracker_htcx profile all key on this controller type and the htc profile
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_RenderModelName_String, "{htc}/rendermodels/vr_tracker_vive_1_0");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_InputProfilePath_String, "{htc}/input/vive_tracker_profile.json");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ControllerType_String, "vive_tracker");
    vr::VRProperties()->SetInt32Property(m_propertyContainer, vr::Prop_ControllerRoleHint_Int32, vr::TrackedControllerRole_OptOut);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_WillDriftInYaw_Bool, false);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_DeviceIsWireless_Bool, true);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_DeviceProvidesBatteryStatus_Bool, false);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_Identifiable_Bool, false);

    // The Vive tracker profile's inputs, so the per-app bindings SteamVR builds for the tracker resolve
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/power/click", &m_power);
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/trigger/click", &m_triggerClick);
    vr::VRDriverInput()->CreateScalarComponent(m_propertyContainer, "/input/trigger/value", &m_triggerValue, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/grip/click", &m_grip);
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/application_menu/click", &m_menu);
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/thumb/click", &m_thumb);
    vr::VRDriverInput()->CreateHapticComponent(m_propertyContainer, "/output/haptic", &m_haptic);

    // The role lives in SteamVR's own "trackers" section (what its tracker UI writes);
    // set it once, so a role the user reassigned there stays
    vr::IVRSettings *l_settings = vr::VRSettings();
    if(l_settings)
    {
        const std::string l_key = "/devices/asvr/" + m_serial;
        char l_current[64] = { 0 };
        vr::EVRSettingsError l_error = vr::VRSettingsError_None;
        l_settings->GetString(vr::k_pch_Trackers_Section, l_key.c_str(), l_current, sizeof(l_current), &l_error);
        if((l_error != vr::VRSettingsError_None) || (l_current[0] == '\0'))
        {
            for(const auto &l_role : KnownRoles())
            {
                if(!l_role.first.compare(m_role))
                {
                    l_settings->SetString(vr::k_pch_Trackers_Section, l_key.c_str(), l_role.second.c_str(), &l_error);
                    if(l_error != vr::VRSettingsError_None) vr::VRDriverLog()->Log(("CTrackerDevice: could not write the tracker role for " + m_serial).c_str());
                    break;
                }
            }
        }
    }

    m_pose.deviceIsConnected = true;
    m_pose.poseIsValid = m_tracking;
    m_pose.result = m_tracking ? vr::TrackingResult_Running_OK : vr::TrackingResult_Running_OutOfRange;

    return vr::VRInitError_None;
}

void CTrackerDevice::Deactivate()
{
    m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_propertyContainer = vr::k_ulInvalidPropertyContainer;
}

void CTrackerDevice::EnterStandby()
{
}

void* CTrackerDevice::GetComponent(const char *pchComponentNameAndVersion)
{
    return nullptr;
}

void CTrackerDevice::DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if(unResponseBufferSize > 0U) pchResponseBuffer[0U] = '\0';

    // "offset x y z qx qy qz qw" or "pose x y z qx qy qz qw"; malformed input must not corrupt the state
    std::stringstream l_message(pchRequest);
    std::string l_messageType;
    l_message >> l_messageType;
    const bool l_offset = !l_messageType.compare("offset");
    if(l_offset || !l_messageType.compare("pose"))
    {
        glm::vec3 l_pos(0.f);
        glm::quat l_rot(1.f, 0.f, 0.f, 0.f);
        for(int i = 0; i < 3; i++) l_message >> l_pos[i];
        for(int i = 0; i < 4; i++) l_message >> l_rot[i];
        if(l_message.fail()) return;
        if(l_offset) SetOffset(l_pos, l_rot);
        else SetAbsolute(l_pos, l_rot);
    }
}

vr::DriverPose_t CTrackerDevice::GetPose()
{
    return m_pose;
}

// CTrackerDevice
const std::string& CTrackerDevice::GetRole() const
{
    return m_role;
}

const std::string& CTrackerDevice::GetSerial() const
{
    return m_serial;
}

bool CTrackerDevice::SetOffset(const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    if(glm::length(f_rot) < 0.001f) return false;
    std::lock_guard<std::mutex> l_lock(m_lock);
    m_position = f_pos;
    m_rotation = glm::normalize(f_rot);
    m_absolute = false;
    return true;
}

bool CTrackerDevice::SetAbsolute(const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    if(glm::length(f_rot) < 0.001f) return false;
    std::lock_guard<std::mutex> l_lock(m_lock);
    m_position = f_pos;
    m_rotation = glm::normalize(f_rot);
    m_absolute = true;
    return true;
}

void CTrackerDevice::SetTracking(bool f_state)
{
    std::lock_guard<std::mutex> l_lock(m_lock);
    m_tracking = f_state;
}

void CTrackerDevice::Describe(std::stringstream &f_stream) const
{
    std::lock_guard<std::mutex> l_lock(m_lock);
    f_stream << m_role << ' ' << (m_absolute ? "world" : "offset") << ' '
        << m_position.x << ' ' << m_position.y << ' ' << m_position.z << ' '
        << m_rotation.x << ' ' << m_rotation.y << ' ' << m_rotation.z << ' ' << m_rotation.w
        << ' ' << (m_tracking ? "tracking" : "lost");
}

void CTrackerDevice::UpdateWorldTransform(const glm::vec3 &f_headPos, const glm::quat &f_headRot)
{
    glm::vec3 l_position;
    glm::quat l_rotation;
    bool l_tracking;
    {
        std::lock_guard<std::mutex> l_lock(m_lock);
        if(m_absolute)
        {
            l_position = m_position;
            l_rotation = m_rotation;
        }
        else
        {
            l_position = f_headPos + f_headRot * m_position;
            l_rotation = f_headRot * m_rotation;
        }
        l_tracking = m_tracking;
    }

    m_pose.vecPosition[0U] = l_position.x;
    m_pose.vecPosition[1U] = l_position.y;
    m_pose.vecPosition[2U] = l_position.z;
    m_pose.qRotation.x = l_rotation.x;
    m_pose.qRotation.y = l_rotation.y;
    m_pose.qRotation.z = l_rotation.z;
    m_pose.qRotation.w = l_rotation.w;
    m_pose.poseIsValid = l_tracking;
    m_pose.result = l_tracking ? vr::TrackingResult_Running_OK : vr::TrackingResult_Running_OutOfRange;
}

void CTrackerDevice::RunFrame()
{
    if(m_trackedDevice != vr::k_unTrackedDeviceIndexInvalid)
    {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_trackedDevice, m_pose, sizeof(vr::DriverPose_t));
    }
}
