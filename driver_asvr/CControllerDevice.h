#pragma once

class CControllerDevice final : public vr::ITrackedDeviceServerDriver
{
    vr::TrackedDeviceIndex_t m_trackedDevice;
    vr::PropertyContainerHandle_t m_propertyContainer;
    vr::DriverPose_t m_pose;
    bool m_leftHand;

    vr::VRInputComponentHandle_t m_triggerClick;
    vr::VRInputComponentHandle_t m_triggerValue;
    vr::VRInputComponentHandle_t m_systemClick;
    vr::VRInputComponentHandle_t m_joystickX;
    vr::VRInputComponentHandle_t m_joystickY;
    std::map<std::string, vr::VRInputComponentHandle_t> m_buttons; // grip + hand-specific a/b or x/y

    std::string m_serial;
    // offset via DebugRequest can arrive off the vrserver thread
    mutable std::mutex m_offsetLock;
    glm::vec3 m_offsetPosition;
    glm::quat m_offsetRotation;

    // vr::ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId);
    void Deactivate();
    void EnterStandby();
    void *GetComponent(const char *pchComponentNameAndVersion);
    void DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize);
    vr::DriverPose_t GetPose();
public:
    explicit CControllerDevice(bool f_leftHand);
    ~CControllerDevice();

    const std::string& GetSerial() const;

    // Input mutation, shared by DebugRequest and the control pipe
    bool SetButton(const std::string &f_name, bool f_state); // trigger|system|grip|a|b|x|y
    bool SetTriggerValue(float f_value); // 0..1, asserts click at >= 0.9
    bool SetJoystick(float f_x, float f_y);
    bool SetOffset(const glm::vec3 &f_pos, const glm::quat &f_rot);
    void GetOffset(glm::vec3 &f_pos, glm::quat &f_rot) const;

    void UpdateWorldTransform(const glm::vec3 &f_headPos, const glm::quat &f_headRot);
    void RunFrame();
};
