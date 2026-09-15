#include "stdafx.h"

#include "CVirtualHmd.h"

// DriverPose_t has no usable default state; start disconnected, at the origin,
// with identity transforms on every quaternion field
static vr::DriverPose_t MakeInitialPose()
{
    vr::DriverPose_t l_pose = {};
    const vr::HmdQuaternion_t l_identity = { 1.0, 0.0, 0.0, 0.0 };
    l_pose.qWorldFromDriverRotation = l_identity;
    l_pose.qDriverFromHeadRotation = l_identity;
    l_pose.qRotation = l_identity;
    l_pose.result = vr::TrackingResult_Uninitialized;
    return l_pose;
}

// Extracts f_count whitespace-separated floats; false when any is missing or malformed
static bool ExtractFloats(std::stringstream &f_stream, float *f_out, int f_count)
{
    for(int i = 0; i < f_count; i++) f_stream >> f_out[i];
    return !f_stream.fail();
}

CVirtualHmd::CVirtualHmd(const glm::uvec2 &f_resolution, float f_fps)
{
    m_resolution = f_resolution;
    m_fps = f_fps;
    m_pose = MakeInitialPose();
}

const std::string& CVirtualHmd::GetSerial() const
{
    return m_serial;
}

void CVirtualHmd::SetPoseTransform(const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    std::lock_guard<std::mutex> l_lock(m_poseLock);
    for(int i = 0; i < 3; i++) m_pose.vecPosition[i] = f_pos[i];
    m_pose.qRotation = { f_rot.w, f_rot.x, f_rot.y, f_rot.z };
}

void CVirtualHmd::GetPoseTransform(glm::vec3 &f_pos, glm::quat &f_rot) const
{
    std::lock_guard<std::mutex> l_lock(m_poseLock);
    f_pos = glm::vec3(static_cast<float>(m_pose.vecPosition[0]), static_cast<float>(m_pose.vecPosition[1]), static_cast<float>(m_pose.vecPosition[2]));
    f_rot = glm::quat(static_cast<float>(m_pose.qRotation.w), static_cast<float>(m_pose.qRotation.x), static_cast<float>(m_pose.qRotation.y), static_cast<float>(m_pose.qRotation.z));
}

void CVirtualHmd::RunFrame()
{
    if(m_trackedDevice != vr::k_unTrackedDeviceIndexInvalid)
    {
        vr::DriverPose_t l_pose;
        {
            std::lock_guard<std::mutex> l_lock(m_poseLock);
            l_pose = m_pose;
        }
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_trackedDevice, l_pose, sizeof(vr::DriverPose_t));
    }
}

// vr::ITrackedDeviceServerDriver
vr::EVRInitError CVirtualHmd::Activate(uint32_t unObjectId)
{
    m_trackedDevice = unObjectId;
    m_propertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_trackedDevice);

    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_SerialNumber_String, m_serial.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ModelNumber_String, m_serial.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_RenderModelName_String, m_serial.c_str());
    // Average adult IPD (~63 mm) so apps see a normal stereo separation instead
    // of two identical eyes
    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_UserIpdMeters_Float, 0.063f);
    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.f);
    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_DisplayFrequency_Float, m_fps);
    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_SecondsFromVsyncToPhotons_Float, 1.f / m_fps);
    vr::VRProperties()->SetUint64Property(m_propertyContainer, vr::Prop_CurrentUniverseId_Uint64, 2);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_IsOnDesktop_Bool, false);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_HasDisplayComponent_Bool, true);
    // Report a proximity sensor that always says "worn", otherwise vrserver decides the
    // static-posed HMD is idle after ~15 seconds and puts it in standby (dead compositor view)
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_ContainsProximitySensor_Bool, true);
    vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/proximity", &m_proximity);
    vr::VRDriverInput()->UpdateBooleanComponent(m_proximity, true, 0.0);
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_NamedIconPathDeviceReady_String, "{asvr}/icons/headset_status_ready.png");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_NamedIconPathDeviceOff_String, "{asvr}/icons/headset_status_off.png");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_NamedIconPathDeviceSearching_String, "{asvr}/icons/headset_status_searching.png");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_NamedIconPathDeviceStandby_String, "{asvr}/icons/headset_status_standby.png");

    {
        std::lock_guard<std::mutex> l_lock(m_poseLock);
        m_pose.deviceIsConnected = true;
        m_pose.poseIsValid = true;
        m_pose.result = vr::TrackingResult_Running_OK;
    }

    return vr::VRInitError_None;
}

void CVirtualHmd::Deactivate()
{
    m_trackedDevice = vr::k_unTrackedDeviceIndexInvalid;
    m_propertyContainer = vr::k_ulInvalidPropertyContainer;
}

void CVirtualHmd::EnterStandby()
{
}

void* CVirtualHmd::GetComponent(const char *pchComponentNameAndVersion)
{
    void *l_result = nullptr;
    // vrserver never asks for ITrackedDeviceServerDriver here; it has that pointer from TrackedDeviceAdded
    if(strcmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version) == 0) l_result = static_cast<vr::IVRDisplayComponent*>(this);

    vr::IVRDriverLog *l_log = vr::VRDriverLog();
    if(l_log)
    {
        char l_line[192];
        snprintf(l_line, sizeof(l_line), "CVirtualHmd: GetComponent('%s') -> %s", pchComponentNameAndVersion, l_result ? "provided" : "null");
        l_log->Log(l_line);
    }
    return l_result;
}

void CVirtualHmd::DebugRequest(const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if(unResponseBufferSize > 0U) pchResponseBuffer[0U] = '\0';

    // Any running application can reach this through DriverDebugRequest, so
    // malformed or degenerate input must not corrupt the live pose
    std::stringstream l_request(pchRequest);
    std::string l_command;
    l_request >> l_command;

    // "setpose x y z qx qy qz qw" - absolute pose, sent by asvr_monitor
    if(l_command == "setpose")
    {
        float l_values[7];
        if(!ExtractFloats(l_request, l_values, 7)) return;

        glm::quat l_rotation(l_values[6], l_values[3], l_values[4], l_values[5]); // ctor takes w first
        if(glm::length(l_rotation) < 0.001f) return;
        l_rotation = glm::normalize(l_rotation);

        std::lock_guard<std::mutex> l_lock(m_poseLock);
        for(int i = 0; i < 3; i++) m_pose.vecPosition[i] = l_values[i];
        m_pose.qRotation = { l_rotation.w, l_rotation.x, l_rotation.y, l_rotation.z };
    }
}

vr::DriverPose_t CVirtualHmd::GetPose()
{
    std::lock_guard<std::mutex> l_lock(m_poseLock);
    return m_pose;
}

// vr::IVRDisplayComponent
void CVirtualHmd::GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight)
{
    *pnX = 0;
    *pnY = 0;
    GetRecommendedRenderTargetSize(pnWidth, pnHeight);
}

bool CVirtualHmd::IsDisplayOnDesktop()
{
    // The compositor must never open an on-desktop window; frames go through
    // the IVRVirtualDisplay device instead (the only path that works headless)
    return false;
}

bool CVirtualHmd::IsDisplayRealDisplay()
{
    return false;
}

void CVirtualHmd::GetRecommendedRenderTargetSize(uint32_t *pnWidth, uint32_t *pnHeight)
{
    *pnWidth = m_resolution.x;
    *pnHeight = m_resolution.y;
}

void CVirtualHmd::GetEyeOutputViewport(vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight)
{
    // Half of the window per eye; zero-sized viewports break vrcompositor (red screen).
    // The monitor and asvr_ctl screenshots show the left eye only.
    const uint32_t l_halfWidth = m_resolution.x / 2U;
    *pnX = (eEye == vr::Eye_Left) ? 0U : l_halfWidth;
    *pnY = 0U;
    *pnWidth = l_halfWidth;
    *pnHeight = m_resolution.y;
}

void CVirtualHmd::GetProjectionRaw(vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom)
{
    // Symmetric square frustum, tangent 1 on every edge (90 degree FOV)
    *pfLeft = *pfTop = -1.f;
    *pfRight = *pfBottom = 1.f;
}

vr::DistortionCoordinates_t CVirtualHmd::ComputeDistortion(vr::EVREye eEye, float fU, float fV)
{
    // Flat panel, no lens: every channel samples straight through
    vr::DistortionCoordinates_t l_coords = {};
    l_coords.rfRed[0] = l_coords.rfGreen[0] = l_coords.rfBlue[0] = fU;
    l_coords.rfRed[1] = l_coords.rfGreen[1] = l_coords.rfBlue[1] = fV;
    return l_coords;
}

bool CVirtualHmd::ComputeInverseDistortion(vr::HmdVector2_t *pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV)
{
    // No distortion, inverse is identity
    if(pResult)
    {
        pResult->v[0] = fU;
        pResult->v[1] = fV;
    }
    return true;
}
