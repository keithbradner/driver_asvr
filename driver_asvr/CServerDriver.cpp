#include "stdafx.h"

#include "CServerDriver.h"
#include "CVirtualHmd.h"
#include "CControllerDevice.h"
#ifdef _WIN32
#include "CDisplayRedirect.h"
#else
#include "linux/CDirectModeOutput.h"
#endif
#include "CControlServer.h"
#include "Platform.h"
#include "../shared/ControlProtocol.h"

static void AppendPose(std::stringstream &f_stream, const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    f_stream << f_pos.x << ' ' << f_pos.y << ' ' << f_pos.z << ' '
        << f_rot.x << ' ' << f_rot.y << ' ' << f_rot.z << ' ' << f_rot.w;
}

// Defined here so the unique_ptr members see complete types
CServerDriver::CServerDriver() = default;
CServerDriver::~CServerDriver() = default;

vr::EVRInitError CServerDriver::Init(vr::IVRDriverContext *pDriverContext)
{
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    // A driver with a DisplayRedirect device can be instantiated by more than one
    // SteamVR process; only the first (vrserver) spawns the monitor and saves config
    m_primaryInstance = Platform::AcquirePrimaryGuard();

    m_settings = LoadDriverSettings();

    m_hmd.reset(new CVirtualHmd(m_settings.resolution, m_settings.fps));
    m_hmd->SetPoseTransform(m_settings.position, m_settings.rotation);

#ifdef _WIN32
    vr::VRServerDriverHost()->TrackedDeviceAdded(m_hmd->GetSerial().c_str(), vr::TrackedDeviceClass_HMD, m_hmd.get());

    m_displayRedirect.reset(new CDisplayRedirect(m_settings.fps));
    vr::VRServerDriverHost()->TrackedDeviceAdded(m_displayRedirect->GetSerial().c_str(), vr::TrackedDeviceClass_DisplayRedirect, m_displayRedirect.get());
#else
    // The direct-mode component must be reachable through the HMD's GetComponent
    // before Activate, so it is wired up ahead of TrackedDeviceAdded
    m_directModeOutput.reset(new CDirectModeOutput(m_settings.resolution.x, m_settings.resolution.y, m_settings.fps));
    m_hmd->SetDirectModeOutput(m_directModeOutput.get());
    vr::VRServerDriverHost()->TrackedDeviceAdded(m_hmd->GetSerial().c_str(), vr::TrackedDeviceClass_HMD, m_hmd.get());
#endif

    m_controllerRight.reset(new CControllerDevice(false));
    vr::VRServerDriverHost()->TrackedDeviceAdded(m_controllerRight->GetSerial().c_str(), vr::TrackedDeviceClass_Controller, m_controllerRight.get());

    m_controllerLeft.reset(new CControllerDevice(true));
    vr::VRServerDriverHost()->TrackedDeviceAdded(m_controllerLeft->GetSerial().c_str(), vr::TrackedDeviceClass_Controller, m_controllerLeft.get());

    if(m_primaryInstance)
    {
        LaunchMonitor();

        m_controlServer.reset(new CControlServer([this](const std::string &f_line) { return ExecuteControlCommand(f_line); }));
        if(!m_controlServer->Start()) vr::VRDriverLog()->Log("CServerDriver: failed to start the control pipe server");
    }

    return vr::VRInitError_None;
}

// The monitor binary ships next to the driver module; it watches for the
// VREvent_Quit and exits on its own when SteamVR shuts down
void CServerDriver::LaunchMonitor() const
{
    const std::string l_directory = Platform::GetModuleDirectory();
    if(l_directory.empty()) return;
    Platform::LaunchMonitor(l_directory);
}

void CServerDriver::Cleanup()
{
    // The pipe thread must be gone before the devices are torn down below
    m_controlServer.reset();

    if(m_primaryInstance && m_hmd)
    {
        // Secondary instances never receive pose updates - saving from them would
        // clobber the persisted pose with the initial one
        glm::vec3 l_hmdPos;
        glm::quat l_hmdRot;
        m_hmd->GetPoseTransform(l_hmdPos, l_hmdRot);
        if(!SaveDriverPose(l_hmdPos, l_hmdRot)) vr::VRDriverLog()->Log("CServerDriver: failed to persist the pose to vrsettings");
    }

    m_controllerRight.reset();
    m_controllerLeft.reset();
#ifdef _WIN32
    m_displayRedirect.reset();
#endif
    m_hmd.reset();
#ifndef _WIN32
    // After the HMD that hands out the component pointer
    m_directModeOutput.reset();
#endif

    Platform::ReleasePrimaryGuard();
    m_primaryInstance = false;

    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* CServerDriver::GetInterfaceVersions()
{
    return vr::k_InterfaceVersions;
}

bool CServerDriver::ShouldBlockStandbyMode()
{
    // The virtual HMD's pose only changes on user input, so vrserver quickly considers it
    // idle and puts it in standby, which stops the compositor (red screen / frozen view).
    return true;
}

void CServerDriver::EnterStandby()
{
}

void CServerDriver::LeaveStandby()
{
}

void CServerDriver::RunFrame()
{
    // Drain driver events; nothing is event-driven yet (no haptic components),
    // but the queue must not be left to accumulate
    vr::VREvent_t l_event;
    while(vr::VRServerDriverHost()->PollNextEvent(&l_event, sizeof(vr::VREvent_t)))
    {
    }

    m_hmd->RunFrame();

    glm::vec3 l_headPos;
    glm::quat l_headRot;
    m_hmd->GetPoseTransform(l_headPos, l_headRot);
    m_controllerRight->UpdateWorldTransform(l_headPos, l_headRot);
    m_controllerRight->RunFrame();
    m_controllerLeft->UpdateWorldTransform(l_headPos, l_headRot);
    m_controllerLeft->RunFrame();

    // Pose changes from control commands are published on the next RunFrame,
    // one frame (~16ms) after the reply - acceptable for scripted control
    if(m_controlServer)
    {
        m_controlServer->RunPendingCommands();
        ProcessScheduledReleases();
    }
}

void CServerDriver::ProcessScheduledReleases()
{
    const auto l_now = std::chrono::steady_clock::now();
    for(auto l_iter = m_scheduledReleases.begin(); l_iter != m_scheduledReleases.end();)
    {
        if(l_now >= l_iter->m_at)
        {
            (l_iter->m_leftHand ? m_controllerLeft : m_controllerRight)->SetButton(l_iter->m_button, false);
            l_iter = m_scheduledReleases.erase(l_iter);
        }
        else ++l_iter;
    }
}

std::string CServerDriver::ExecuteControlCommand(const std::string &f_line)
{
    std::stringstream l_message(f_line);
    std::string l_type;
    l_message >> l_type;

    std::string l_reply("err unknown command");
    if(!l_type.compare("ping"))
    {
        std::stringstream l_response;
        l_response << "ok asvr " << g_controlProtocolVersion;
        l_reply = l_response.str();
    }
    else if(!l_type.compare("status"))
    {
        glm::vec3 l_pos;
        glm::quat l_rot;
        std::stringstream l_response;
        l_response << "ok hmd ";
        m_hmd->GetPoseTransform(l_pos, l_rot);
        AppendPose(l_response, l_pos, l_rot);
        l_response << " left ";
        m_controllerLeft->GetOffset(l_pos, l_rot);
        AppendPose(l_response, l_pos, l_rot);
        l_response << " right ";
        m_controllerRight->GetOffset(l_pos, l_rot);
        AppendPose(l_response, l_pos, l_rot);
        l_reply = l_response.str();
    }
    else if(!l_type.compare(0U, 4U, "hmd_")) l_reply = ExecuteHmdCommand(l_type, l_message);
    else if(!l_type.compare("ctrl")) l_reply = ExecuteControllerCommand(l_message);
    return l_reply;
}

std::string CServerDriver::ExecuteHmdCommand(const std::string &f_type, std::stringstream &f_args)
{
    glm::vec3 l_pos;
    glm::quat l_rot;
    m_hmd->GetPoseTransform(l_pos, l_rot);

    if(!f_type.compare("hmd_pose"))
    {
        glm::vec3 l_newPos(0.f);
        glm::quat l_newRot(1.f, 0.f, 0.f, 0.f);
        for(int i = 0; i < 3; i++) f_args >> l_newPos[i];
        for(int i = 0; i < 4; i++) f_args >> l_newRot[i];
        if(f_args.fail()) return "err bad args";
        if(glm::length(l_newRot) < 0.001f) return "err bad quaternion";
        m_hmd->SetPoseTransform(l_newPos, glm::normalize(l_newRot));
        return "ok";
    }
    if(!f_type.compare("hmd_rotate"))
    {
        float l_yaw = 0.f;
        float l_pitch = 0.f;
        f_args >> l_yaw >> l_pitch;
        if(f_args.fail()) return "err bad args";
        float l_roll = 0.f;
        f_args >> l_roll;
        // Yaw about world up (keeps the horizon level), pitch/roll about local axes
        glm::quat l_newRot = glm::angleAxis(glm::radians(-l_yaw), glm::vec3(0.f, 1.f, 0.f)) * l_rot
            * glm::angleAxis(glm::radians(l_pitch), glm::vec3(1.f, 0.f, 0.f))
            * glm::angleAxis(glm::radians(-l_roll), glm::vec3(0.f, 0.f, 1.f));
        m_hmd->SetPoseTransform(l_pos, glm::normalize(l_newRot));
        return "ok";
    }
    if(!f_type.compare("hmd_translate"))
    {
        glm::vec3 l_move(0.f);
        for(int i = 0; i < 3; i++) f_args >> l_move[i];
        if(f_args.fail()) return "err bad args";
        std::string l_frame("local");
        f_args >> l_frame;
        if(!l_frame.compare("local")) l_move = l_rot * l_move;
        else if(l_frame.compare("world")) return "err bad args";
        m_hmd->SetPoseTransform(l_pos + l_move, l_rot);
        return "ok";
    }
    return "err unknown command";
}

std::string CServerDriver::ExecuteControllerCommand(std::stringstream &f_args)
{
    std::string l_hand;
    std::string l_action;
    f_args >> l_hand >> l_action;
    bool l_leftHand;
    if(!l_hand.compare("left")) l_leftHand = true;
    else if(!l_hand.compare("right")) l_leftHand = false;
    else return "err bad args";
    CControllerDevice *l_controller = (l_leftHand ? m_controllerLeft : m_controllerRight).get();

    if(!l_action.compare("offset"))
    {
        glm::vec3 l_offsetPos(0.f);
        glm::quat l_offsetRot(1.f, 0.f, 0.f, 0.f);
        for(int i = 0; i < 3; i++) f_args >> l_offsetPos[i];
        for(int i = 0; i < 4; i++) f_args >> l_offsetRot[i];
        if(f_args.fail()) return "err bad args";
        if(!l_controller->SetOffset(l_offsetPos, l_offsetRot)) return "err bad quaternion";
        return "ok";
    }
    if(!l_action.compare("aim"))
    {
        float l_yaw = 0.f;
        float l_pitch = 0.f;
        f_args >> l_yaw >> l_pitch;
        if(f_args.fail()) return "err bad args";
        glm::vec3 l_offsetPos;
        glm::quat l_offsetRot;
        l_controller->GetOffset(l_offsetPos, l_offsetRot);
        l_offsetRot = glm::angleAxis(glm::radians(-l_yaw), glm::vec3(0.f, 1.f, 0.f))
            * glm::angleAxis(glm::radians(l_pitch), glm::vec3(1.f, 0.f, 0.f));
        l_controller->SetOffset(l_offsetPos, l_offsetRot);
        return "ok";
    }
    if(!l_action.compare("button"))
    {
        std::string l_button;
        std::string l_operation;
        f_args >> l_button >> l_operation;
        if(f_args.fail()) return "err bad args";
        const bool l_click = !l_operation.compare("click");
        bool l_state;
        if(!l_operation.compare("press") || l_click) l_state = true;
        else if(!l_operation.compare("release")) l_state = false;
        else return "err bad args";
        if(!l_controller->SetButton(l_button, l_state)) return "err unknown button or controller not active";
        if(l_click)
        {
            ScheduledRelease l_release;
            l_release.m_leftHand = l_leftHand;
            l_release.m_button = l_button;
            l_release.m_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(g_clickReleaseDelayMs);
            m_scheduledReleases.push_back(l_release);
        }
        return "ok";
    }
    if(!l_action.compare("trigger"))
    {
        float l_value = 0.f;
        f_args >> l_value;
        if(f_args.fail()) return "err bad args";
        if(!l_controller->SetTriggerValue(l_value)) return "err controller not active";
        return "ok";
    }
    if(!l_action.compare("joystick"))
    {
        float l_x = 0.f;
        float l_y = 0.f;
        f_args >> l_x >> l_y;
        if(f_args.fail()) return "err bad args";
        if(!l_controller->SetJoystick(l_x, l_y)) return "err controller not active";
        return "ok";
    }
    return "err unknown command";
}
