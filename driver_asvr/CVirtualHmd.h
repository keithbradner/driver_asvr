#pragma once

// The virtual headset: a tracked HMD device plus the IVRDisplayComponent that
// describes the (non-existent) panel geometry to the compositor. Frames never
// go through a real display; CDisplayRedirect receives them instead.
class CVirtualHmd final : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent
{
public:
    CVirtualHmd(const glm::uvec2 &f_resolution, float f_fps);

    const std::string& GetSerial() const;

    void SetPoseTransform(const glm::vec3 &f_pos, const glm::quat &f_rot);
    void GetPoseTransform(glm::vec3 &f_pos, glm::quat &f_rot) const;

    void RunFrame();

private:
    // vr::ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId);
    void Deactivate();
    void EnterStandby();
    void *GetComponent(const char *pchComponentNameAndVersion);
    void DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize);
    vr::DriverPose_t GetPose();

    // vr::IVRDisplayComponent
    void GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight);
    bool IsDisplayOnDesktop();
    bool IsDisplayRealDisplay();
    void GetRecommendedRenderTargetSize(uint32_t *pnWidth, uint32_t *pnHeight);
    void GetEyeOutputViewport(vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight);
    void GetProjectionRaw(vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom);
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eEye, float fU, float fV);
    bool ComputeInverseDistortion(vr::HmdVector2_t *pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV);

    std::string m_serial = "asvr_hmd_0001";
    glm::uvec2 m_resolution;
    float m_fps;

    vr::TrackedDeviceIndex_t m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    vr::PropertyContainerHandle_t m_propertyContainer = vr::k_ulInvalidPropertyContainer;
    vr::VRInputComponentHandle_t m_proximity = vr::k_ulInvalidInputComponentHandle;
    // setpose via DebugRequest can arrive off the RunFrame thread
    mutable std::mutex m_poseLock;
    vr::DriverPose_t m_pose;
};
