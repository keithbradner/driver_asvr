#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include "COpenVRPaths.h"
#include "CVRSettings.h"

// All the logic behind the settings panel's buttons and the start screen; no
// UI code in here
class CConfigCore final
{
    std::filesystem::path m_driverPath; // the folder holding driver.vrdrivermanifest
    std::string m_driverPathString;
    bool m_driverFilesFound;

    COpenVRPaths m_openvrPaths;
    CVRSettings m_vrSettings; // holds both the SteamVR keys and the driver_asvr section
    std::vector<std::string> m_runtimePaths; // from openvrpaths.vrpath, refreshed with the status

    bool m_driverRegistered;
    bool m_vrserverRunning;
    unsigned long m_vrserverPid; // 0 while not running; a change means SteamVR was restarted

    std::string m_lastMessage;
    bool m_lastMessageIsError;

    void SetMessage(const std::string &f_text, bool f_error);
    void AppendRestartHint();
    static std::filesystem::path ResolveDriverPath();
public:
    CConfigCore();
    ~CConfigCore();

    void Initialize();
    void RefreshStatus();

    bool Install();
    bool Uninstall(bool f_restoreBackup);
    bool SetDriverEnabled(bool f_state); // writes + saves immediately
    bool ApplyVRSettings();
    bool ReloadVRSettings();
    bool SaveDriverSettings();
    bool ReloadDriverSettings();

    // Launches SteamVR from one of the runtime folders in openvrpaths.vrpath
    // (bin/win64/vrstartup.exe, bin/vrstartup.sh), or through Steam itself
    bool StartSteamVR(const std::string &f_runtimePath);
    bool StartSteamVRViaSteam();

    const std::string& GetDriverPath() const;
    bool AreDriverFilesFound() const;
    bool IsDriverRegistered() const;
    bool IsVRServerRunning() const;
    unsigned long GetVRServerPid() const;
    const std::vector<std::string>& GetRuntimePaths() const;

    COpenVRPaths& GetOpenVRPaths();
    CVRSettings& GetVRSettings();

    const std::string& GetLastMessage() const;
    bool IsLastMessageError() const;
    void ClearMessage();
};
