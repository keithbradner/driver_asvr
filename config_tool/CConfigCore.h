#pragma once

#include <string>
#include <filesystem>

#include "COpenVRPaths.h"
#include "CVRSettings.h"

// All the logic behind the buttons; no UI code in here
class CConfigCore final
{
    std::filesystem::path m_driverPath; // <exe_dir>/asvr
    std::string m_driverPathString;
    bool m_driverFilesFound;

    COpenVRPaths m_openvrPaths;
    CVRSettings m_vrSettings; // holds both the SteamVR keys and the driver_asvr section

    bool m_driverRegistered;
    bool m_vrserverRunning;

    std::string m_lastMessage;
    bool m_lastMessageIsError;

    void SetMessage(const std::string &f_text, bool f_error);
    void AppendRestartHint();
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

    const std::string& GetDriverPath() const;
    bool AreDriverFilesFound() const;
    bool IsDriverRegistered() const;
    bool IsVRServerRunning() const;

    COpenVRPaths& GetOpenVRPaths();
    CVRSettings& GetVRSettings();

    const std::string& GetLastMessage() const;
    bool IsLastMessageError() const;
};
