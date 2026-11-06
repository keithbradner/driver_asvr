#pragma once

#include <string>
#include "openvr.h"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

// The window's connection to a running SteamVR: a Background application
// (never starts vrserver itself), the virtual devices' indices, and the
// DebugRequest text vocabulary the driver's devices understand.
class CVRLink final
{
public:
    ~CVRLink();

    // VR_Init as a Background app; fails (without side effects) while SteamVR
    // is not running or still starting
    bool Connect(vr::EVRInitError &f_error);
    void Disconnect();
    bool IsConnected() const { return m_vrSystem != nullptr; }

    // Drains SteamVR's event queue; false once SteamVR asked us to quit (the
    // link is already closed by then)
    bool PollEvents();

    // vrserver keeps a single HMD; when another headset's driver registered
    // first, index 0 is that headset and nothing we send reaches our driver
    bool IsForeignHmd() const { return m_foreignHmd; }
    const std::string& GetHmdSerial() const { return m_hmdSerial; }
    bool HasController(bool f_left) const;

    bool ReadHmdPose(glm::vec3 &f_pos, glm::quat &f_rot) const;

    void SendHmdPose(const glm::vec3 &f_pos, const glm::quat &f_rot);
    void SendControllerOffset(bool f_left, const glm::vec3 &f_pos, const glm::quat &f_rot);
    void SendControllerTrigger(bool f_left, bool f_state);
    void SendControllerSystem(bool f_left, bool f_state);
    void SendControllerButton(bool f_left, const char *f_button, bool f_state);
    void SendControllerJoystick(bool f_left, float f_x, float f_y);

private:
    void FindControllerDevices();
    void CheckHmdOwnership();
    void SendDebugRequest(vr::TrackedDeviceIndex_t f_device, const std::string &f_message);

    vr::IVRSystem *m_vrSystem = nullptr;
    vr::TrackedDeviceIndex_t m_controllerDevice = vr::k_unTrackedDeviceIndexInvalid; // virtual right controller
    vr::TrackedDeviceIndex_t m_leftControllerDevice = vr::k_unTrackedDeviceIndexInvalid; // virtual left controller
    bool m_foreignHmd = false;
    std::string m_hmdSerial; // whoever owns index 0
};
