#include "stdafx.h"

#include "CControllerDevice.h"
#include "../shared/ControllerPose.h"

CControllerDevice::CControllerDevice(bool f_leftHand)
{
    m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_propertyContainer = vr::k_ulInvalidPropertyContainer;
    m_leftHand = f_leftHand;
    m_pose = { 0 };

    m_pose.poseTimeOffset = 0.f;
    m_pose.qWorldFromDriverRotation.w = 1.0;
    m_pose.qWorldFromDriverRotation.x = .0;
    m_pose.qWorldFromDriverRotation.y = .0;
    m_pose.qWorldFromDriverRotation.z = .0;
    m_pose.qDriverFromHeadRotation.w = 1.0;
    m_pose.qDriverFromHeadRotation.x = .0;
    m_pose.qDriverFromHeadRotation.y = .0;
    m_pose.qDriverFromHeadRotation.z = .0;
    m_pose.vecDriverFromHeadTranslation[0U] = .0;
    m_pose.vecDriverFromHeadTranslation[1U] = .0;
    m_pose.vecDriverFromHeadTranslation[2U] = .0;
    m_pose.qRotation.w = 1.0;
    m_pose.qRotation.x = .0;
    m_pose.qRotation.y = .0;
    m_pose.qRotation.z = .0;
    m_pose.vecPosition[0U] = .0;
    m_pose.vecPosition[1U] = .0;
    m_pose.vecPosition[2U] = .0;
    m_pose.poseIsValid = false;
    m_pose.willDriftInYaw = false;
    m_pose.shouldApplyHeadModel = false;
    m_pose.result = vr::TrackingResult_Uninitialized;
    m_pose.deviceIsConnected = false;

    m_triggerClick = vr::k_ulInvalidInputComponentHandle;
    m_triggerValue = vr::k_ulInvalidInputComponentHandle;
    m_systemClick = vr::k_ulInvalidInputComponentHandle;
    m_joystickX = vr::k_ulInvalidInputComponentHandle;
    m_joystickY = vr::k_ulInvalidInputComponentHandle;

    m_serial.assign(m_leftHand ? "asvr_controller_0002" : "asvr_controller_0001");
    m_offsetPosition = g_ctrlDefaultPosition[m_leftHand ? 1U : 0U];
    m_offsetRotation = GetDefaultControllerRotation(m_leftHand);
}

CControllerDevice::~CControllerDevice()
{
}

// vr::ITrackedDeviceServerDriver
vr::EVRInitError CControllerDevice::Activate(uint32_t unObjectId)
{
    m_trackedDevice = unObjectId;
    m_propertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_trackedDevice);

    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_SerialNumber_String, m_serial.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ModelNumber_String, m_serial.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_RenderModelName_String, m_leftHand ? "oculus_quest_plus_controller_left" : "oculus_quest_plus_controller_right");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_InputProfilePath_String, "{asvr}/input/asvr_controller_profile.json");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ControllerType_String, "asvr_controller");
    vr::VRProperties()->SetInt32Property(m_propertyContainer, vr::Prop_ControllerRoleHint_Int32, m_leftHand ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand);

    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/trigger/click", &m_triggerClick);
    vr::VRDriverInput()->CreateScalarComponent(m_propertyContainer, "/input/trigger/value", &m_triggerValue, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/system/click", &m_systemClick);
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/grip/click", &m_buttons["grip"]);
    vr::VRDriverInput()->CreateScalarComponent(m_propertyContainer, "/input/joystick/x", &m_joystickX, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateScalarComponent(m_propertyContainer, "/input/joystick/y", &m_joystickY, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    // Touch-style face buttons: X/Y live on the left hand, A/B on the right
    if(m_leftHand)
    {
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/x/click", &m_buttons["x"]);
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/y/click", &m_buttons["y"]);
    }
    else
    {
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/a/click", &m_buttons["a"]);
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/b/click", &m_buttons["b"]);
    }

    m_pose.deviceIsConnected = true;
    m_pose.poseIsValid = true;
    m_pose.result = vr::TrackingResult_Running_OK;

    return vr::VRInitError_None;
}

void CControllerDevice::Deactivate()
{
    m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_propertyContainer = vr::k_ulInvalidPropertyContainer;
    m_triggerClick = vr::k_ulInvalidInputComponentHandle;
    m_triggerValue = vr::k_ulInvalidInputComponentHandle;
    m_systemClick = vr::k_ulInvalidInputComponentHandle;
    m_joystickX = vr::k_ulInvalidInputComponentHandle;
    m_joystickY = vr::k_ulInvalidInputComponentHandle;
    m_buttons.clear();
}

void CControllerDevice::EnterStandby()
{
}

void* CControllerDevice::GetComponent(const char *pchComponentNameAndVersion)
{
    return nullptr;
}

void CControllerDevice::DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if(unResponseBufferSize > 0U) pchResponseBuffer[0U] = '\0';

    // Any running application can reach this through DriverDebugRequest, so
    // malformed or degenerate input must not corrupt the state
    std::stringstream l_message(pchRequest);
    std::string l_messageType;
    l_message >> l_messageType;
    if(!l_messageType.compare("offset"))
    {
        glm::vec3 l_offsetPos(0.f);
        glm::quat l_offsetRot(1.f, 0.f, 0.f, 0.f);
        for(int i = 0; i < 3; i++) l_message >> l_offsetPos[i];
        for(int i = 0; i < 4; i++) l_message >> l_offsetRot[i];
        if(l_message.fail()) return;
        SetOffset(l_offsetPos, l_offsetRot);
    }
    else if(!l_messageType.compare("trigger") || !l_messageType.compare("system"))
    {
        int l_state = 0;
        l_message >> l_state;
        if(l_message.fail()) return;
        SetButton(l_messageType, (l_state != 0));
    }
    else if(!l_messageType.compare("button")) // "button <grip|a|b|x|y> <0|1>"
    {
        std::string l_button;
        int l_state = 0;
        l_message >> l_button >> l_state;
        if(l_message.fail()) return;
        SetButton(l_button, (l_state != 0));
    }
    else if(!l_messageType.compare("joystick")) // "joystick <x> <y>", both -1..1
    {
        float l_x = 0.f;
        float l_y = 0.f;
        l_message >> l_x >> l_y;
        if(l_message.fail()) return;
        SetJoystick(l_x, l_y);
    }
}

vr::DriverPose_t CControllerDevice::GetPose()
{
    return m_pose;
}

// CControllerDevice
const std::string& CControllerDevice::GetSerial() const
{
    return m_serial;
}

bool CControllerDevice::SetButton(const std::string &f_name, bool f_state)
{
    bool l_result = false;
    if(!f_name.compare("trigger"))
    {
        if(m_triggerClick != vr::k_ulInvalidInputComponentHandle)
        {
            vr::VRDriverInput()->UpdateBooleanComponent(m_triggerClick, f_state, 0.0);
            vr::VRDriverInput()->UpdateScalarComponent(m_triggerValue, f_state ? 1.f : 0.f, 0.0);
            l_result = true;
        }
    }
    else if(!f_name.compare("system"))
    {
        if(m_systemClick != vr::k_ulInvalidInputComponentHandle)
        {
            vr::VRDriverInput()->UpdateBooleanComponent(m_systemClick, f_state, 0.0);
            l_result = true;
        }
    }
    else
    {
        auto l_search = m_buttons.find(f_name);
        if((l_search != m_buttons.end()) && (l_search->second != vr::k_ulInvalidInputComponentHandle))
        {
            vr::VRDriverInput()->UpdateBooleanComponent(l_search->second, f_state, 0.0);
            l_result = true;
        }
    }
    return l_result;
}

bool CControllerDevice::SetTriggerValue(float f_value)
{
    bool l_result = false;
    if(m_triggerClick != vr::k_ulInvalidInputComponentHandle)
    {
        const float l_value = glm::clamp(f_value, 0.f, 1.f);
        vr::VRDriverInput()->UpdateScalarComponent(m_triggerValue, l_value, 0.0);
        vr::VRDriverInput()->UpdateBooleanComponent(m_triggerClick, (l_value >= 0.9f), 0.0);
        l_result = true;
    }
    return l_result;
}

bool CControllerDevice::SetJoystick(float f_x, float f_y)
{
    bool l_result = false;
    if((m_joystickX != vr::k_ulInvalidInputComponentHandle) && (m_joystickY != vr::k_ulInvalidInputComponentHandle))
    {
        vr::VRDriverInput()->UpdateScalarComponent(m_joystickX, glm::clamp(f_x, -1.f, 1.f), 0.0);
        vr::VRDriverInput()->UpdateScalarComponent(m_joystickY, glm::clamp(f_y, -1.f, 1.f), 0.0);
        l_result = true;
    }
    return l_result;
}

bool CControllerDevice::SetOffset(const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    if(glm::length(f_rot) < 0.001f) return false;
    std::lock_guard<std::mutex> l_lock(m_offsetLock);
    m_offsetPosition = f_pos;
    m_offsetRotation = glm::normalize(f_rot);
    return true;
}

void CControllerDevice::GetOffset(glm::vec3 &f_pos, glm::quat &f_rot) const
{
    std::lock_guard<std::mutex> l_lock(m_offsetLock);
    f_pos = m_offsetPosition;
    f_rot = m_offsetRotation;
}

void CControllerDevice::UpdateWorldTransform(const glm::vec3 &f_headPos, const glm::quat &f_headRot)
{
    glm::vec3 l_offsetPos;
    glm::quat l_offsetRot;
    GetOffset(l_offsetPos, l_offsetRot);
    glm::vec3 l_position = f_headPos + f_headRot*l_offsetPos;
    glm::quat l_rotation = f_headRot*l_offsetRot;

    m_pose.vecPosition[0U] = l_position.x;
    m_pose.vecPosition[1U] = l_position.y;
    m_pose.vecPosition[2U] = l_position.z;
    m_pose.qRotation.x = l_rotation.x;
    m_pose.qRotation.y = l_rotation.y;
    m_pose.qRotation.z = l_rotation.z;
    m_pose.qRotation.w = l_rotation.w;
}

void CControllerDevice::RunFrame()
{
    if(m_trackedDevice != vr::k_unTrackedDeviceIndexInvalid)
    {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_trackedDevice, m_pose, sizeof(vr::DriverPose_t));
    }
}
