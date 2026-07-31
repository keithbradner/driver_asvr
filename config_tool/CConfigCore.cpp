#include "CConfigCore.h"
#include "CPlatform.h"

CConfigCore::CConfigCore()
{
    m_driverFilesFound = false;
    m_driverRegistered = false;
    m_vrserverRunning = false;
    m_lastMessageIsError = false;
}

CConfigCore::~CConfigCore()
{
}

void CConfigCore::SetMessage(const std::string &f_text, bool f_error)
{
    m_lastMessage.assign(f_text);
    m_lastMessageIsError = f_error;
}

void CConfigCore::AppendRestartHint()
{
    if(m_vrserverRunning) m_lastMessage.append(" (restart SteamVR to apply)");
}

void CConfigCore::Initialize()
{
    m_driverPath = CPlatform::NormalizeDriverPath(CPlatform::GetExecutableDirectory() / "asvr");
    m_driverPathString = m_driverPath.string();
    RefreshStatus();
    if(m_openvrPaths.IsLoaded()) m_vrSettings.Load(m_openvrPaths.GetConfigPath());
    m_lastMessage.clear();
    m_lastMessageIsError = false;
}

void CConfigCore::RefreshStatus()
{
    std::error_code l_error;
    m_driverFilesFound = std::filesystem::exists(m_driverPath / "driver.vrdrivermanifest", l_error);
    m_openvrPaths.Load();
    m_driverRegistered = m_openvrPaths.IsDriverRegistered(m_driverPathString);
    m_vrserverRunning = CPlatform::IsProcessRunning("vrserver");
}

bool CConfigCore::Install()
{
    RefreshStatus();
    if(!m_driverFilesFound)
    {
        SetMessage("ERROR: driver files not found at " + m_driverPathString, true);
        return false;
    }
    if(!m_openvrPaths.IsLoaded())
    {
        SetMessage("ERROR: " + m_openvrPaths.GetError(), true);
        return false;
    }

    m_openvrPaths.AddDriver(m_driverPathString);
    if(!m_openvrPaths.Save())
    {
        SetMessage("ERROR: " + m_openvrPaths.GetError(), true);
        return false;
    }

    if(!m_vrSettings.Load(m_openvrPaths.GetConfigPath()))
    {
        SetMessage("Registered, but SteamVR settings were not updated: " + m_vrSettings.GetError(), true);
        return false;
    }
    m_vrSettings.ApplyRecommended();
    if(!m_vrSettings.Save())
    {
        SetMessage("Registered, but SteamVR settings were not updated: " + m_vrSettings.GetError(), true);
        return false;
    }

    RefreshStatus();
    SetMessage("Installed: driver registered and SteamVR settings applied", false);
    AppendRestartHint();
    return true;
}

bool CConfigCore::Uninstall(bool f_restoreBackup)
{
    RefreshStatus();
    if(!m_openvrPaths.IsLoaded())
    {
        SetMessage("ERROR: " + m_openvrPaths.GetError(), true);
        return false;
    }

    m_openvrPaths.RemoveDriver(m_driverPathString);
    if(!m_openvrPaths.Save())
    {
        SetMessage("ERROR: " + m_openvrPaths.GetError(), true);
        return false;
    }

    std::string l_message("Uninstalled: driver unregistered");
    if(f_restoreBackup)
    {
        if(m_vrSettings.RestoreBackup()) l_message.append(", settings backup restored");
        else l_message.append(", but the settings backup could not be restored");
        m_vrSettings.Load(m_openvrPaths.GetConfigPath());
    }

    // A forcedDriver entry pointing at the now-unregistered driver would break
    // SteamVR startup; clear it whichever path got us here
    if(m_vrSettings.IsLoaded() && m_vrSettings.GetForcedDriver())
    {
        m_vrSettings.SetForcedDriver(false);
        if(m_vrSettings.Save()) l_message.append(", forcedDriver cleared");
    }

    RefreshStatus();
    SetMessage(l_message, false);
    AppendRestartHint();
    return true;
}

bool CConfigCore::SetDriverEnabled(bool f_state)
{
    if(!m_vrSettings.IsLoaded())
    {
        SetMessage("ERROR: " + (m_vrSettings.GetError().empty() ? std::string("SteamVR settings unavailable") : m_vrSettings.GetError()), true);
        return false;
    }
    m_vrSettings.SetDriverEnabled(f_state);
    if(!m_vrSettings.Save())
    {
        SetMessage("ERROR: " + m_vrSettings.GetError(), true);
        return false;
    }
    SetMessage(f_state ? "Driver enabled" : "Driver disabled", false);
    AppendRestartHint();
    return true;
}

bool CConfigCore::ApplyVRSettings()
{
    if(!m_vrSettings.IsLoaded())
    {
        SetMessage("ERROR: " + (m_vrSettings.GetError().empty() ? std::string("SteamVR settings unavailable") : m_vrSettings.GetError()), true);
        return false;
    }
    if(!m_vrSettings.Save())
    {
        SetMessage("ERROR: " + m_vrSettings.GetError(), true);
        return false;
    }
    SetMessage("SteamVR settings saved", false);
    AppendRestartHint();
    return true;
}

bool CConfigCore::ReloadVRSettings()
{
    m_openvrPaths.Load();
    if(!m_vrSettings.Load(m_openvrPaths.GetConfigPath()))
    {
        SetMessage("ERROR: " + m_vrSettings.GetError(), true);
        return false;
    }
    SetMessage("SteamVR settings reloaded", false);
    return true;
}

// Driver settings live in the driver_asvr section of steamvr.vrsettings,
// so saving/reloading them is the same file operation as the SteamVR keys
bool CConfigCore::SaveDriverSettings()
{
    if(!m_vrSettings.IsLoaded())
    {
        SetMessage("ERROR: " + (m_vrSettings.GetError().empty() ? std::string("SteamVR settings unavailable") : m_vrSettings.GetError()), true);
        return false;
    }
    if(!m_vrSettings.Save())
    {
        SetMessage("ERROR: " + m_vrSettings.GetError(), true);
        return false;
    }
    SetMessage("Driver settings saved", false);
    AppendRestartHint();
    return true;
}

bool CConfigCore::ReloadDriverSettings()
{
    m_openvrPaths.Load();
    if(!m_vrSettings.Load(m_openvrPaths.GetConfigPath()))
    {
        SetMessage("ERROR: " + m_vrSettings.GetError(), true);
        return false;
    }
    SetMessage("Driver settings reloaded", false);
    return true;
}

const std::string& CConfigCore::GetDriverPath() const
{
    return m_driverPathString;
}

bool CConfigCore::AreDriverFilesFound() const
{
    return m_driverFilesFound;
}

bool CConfigCore::IsDriverRegistered() const
{
    return m_driverRegistered;
}

bool CConfigCore::IsVRServerRunning() const
{
    return m_vrserverRunning;
}

COpenVRPaths& CConfigCore::GetOpenVRPaths()
{
    return m_openvrPaths;
}

CVRSettings& CConfigCore::GetVRSettings()
{
    return m_vrSettings;
}

const std::string& CConfigCore::GetLastMessage() const
{
    return m_lastMessage;
}

bool CConfigCore::IsLastMessageError() const
{
    return m_lastMessageIsError;
}
