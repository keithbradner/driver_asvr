#pragma once

// A virtual Vive-style tracker (TrackedDeviceClass_GenericTracker) for one SteamVR
// tracker role. SteamVR takes the role from the "trackers" settings section
// (/devices/asvr/<serial> -> TrackerRole_*), which Activate writes when the key is
// absent, and its OpenXR runtime then presents the device under
// /user/vive_tracker_htcx/role/<role>. The pose is a head-relative offset like the
// controllers', or an absolute pose set through the control pipe.
class CTrackerDevice final : public vr::ITrackedDeviceServerDriver
{
    vr::TrackedDeviceIndex_t m_trackedDevice;
    vr::PropertyContainerHandle_t m_propertyContainer;
    vr::DriverPose_t m_pose;
    std::string m_role; // waist, chest, left_foot, ... (see KnownRoles)
    std::string m_serial;
    // The inputs a Vive tracker declares ({htc}/input/vive_tracker_profile.json), never pressed: SteamVR binds a
    // tracker's profile per app and its OpenXR layer only completes the app's input setup (and focuses the session)
    // when the declared inputs exist
    vr::VRInputComponentHandle_t m_power;
    vr::VRInputComponentHandle_t m_triggerClick;
    vr::VRInputComponentHandle_t m_triggerValue;
    vr::VRInputComponentHandle_t m_grip;
    vr::VRInputComponentHandle_t m_menu;
    vr::VRInputComponentHandle_t m_thumb;
    vr::VRInputComponentHandle_t m_haptic;

    // Pose commands can arrive off the vrserver thread (DebugRequest)
    mutable std::mutex m_lock;
    glm::vec3 m_position;
    glm::quat m_rotation;
    bool m_absolute; // position/rotation are world, not head-relative
    bool m_tracking;

    // vr::ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId);
    void Deactivate();
    void EnterStandby();
    void *GetComponent(const char *pchComponentNameAndVersion);
    void DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize);
    vr::DriverPose_t GetPose();
public:
    explicit CTrackerDevice(const std::string &f_role);
    ~CTrackerDevice();

    // Role names accepted in settings and commands, with SteamVR's TrackerRole_* value for each
    static const std::vector<std::pair<std::string, std::string>>& KnownRoles();
    static bool IsKnownRole(const std::string &f_role);

    const std::string& GetRole() const;
    const std::string& GetSerial() const;

    bool SetOffset(const glm::vec3 &f_pos, const glm::quat &f_rot);   // head-relative
    bool SetAbsolute(const glm::vec3 &f_pos, const glm::quat &f_rot); // world
    void SetTracking(bool f_state);
    void Describe(std::stringstream &f_stream) const; // "<role> offset|world x y z qx qy qz qw tracking|lost"

    void UpdateWorldTransform(const glm::vec3 &f_headPos, const glm::quat &f_headRot);
    void RunFrame();
};
