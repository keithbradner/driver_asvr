#pragma once

#include <memory>

#include "DriverSettings.h"

class CVirtualHmd;
class CControllerDevice;
#ifdef _WIN32
class CDisplayRedirect;
#else
class CDirectModeOutput;
#endif
class CControlServer;

class CServerDriver final : public vr::IServerTrackedDeviceProvider
{
public:
    CServerDriver();
    ~CServerDriver();

private:
    // vr::IServerTrackedDeviceProvider
    vr::EVRInitError Init(vr::IVRDriverContext *pDriverContext);
    void Cleanup();
    const char * const *GetInterfaceVersions();
    void RunFrame();
    bool ShouldBlockStandbyMode();
    void EnterStandby();
    void LeaveStandby();

    void LaunchMonitor() const;

    // Control pipe command execution; runs on the vrserver thread via
    // CControlServer::RunPendingCommands
    std::string ExecuteControlCommand(const std::string &f_line);
    std::string ExecuteHmdCommand(const std::string &f_type, std::stringstream &f_args);
    std::string ExecuteControllerCommand(std::stringstream &f_args);
    void ProcessScheduledReleases();

    DriverSettings m_settings;
    std::unique_ptr<CVirtualHmd> m_hmd;
    std::unique_ptr<CControllerDevice> m_controllerRight;
    std::unique_ptr<CControllerDevice> m_controllerLeft;
#ifdef _WIN32
    std::unique_ptr<CDisplayRedirect> m_displayRedirect;
#else
    std::unique_ptr<CDirectModeOutput> m_directModeOutput;
#endif
    std::unique_ptr<CControlServer> m_controlServer;

    struct ScheduledRelease
    {
        bool m_leftHand;
        std::string m_button;
        std::chrono::steady_clock::time_point m_at;
    };
    std::vector<ScheduledRelease> m_scheduledReleases;

    bool m_primaryInstance = false;
};
