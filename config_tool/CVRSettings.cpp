#include "CVRSettings.h"
#include "CPlatform.h"

CVRSettings::CVRSettings()
{
    m_loaded = false;
    m_existed = false;
}

CVRSettings::~CVRSettings()
{
}

bool CVRSettings::Load(const std::filesystem::path &f_configDir)
{
    m_loaded = false;
    m_existed = false;
    m_error.clear();
    m_json = nlohmann::ordered_json::object();

    if(f_configDir.empty())
    {
        m_error.assign("SteamVR config folder unknown - run SteamVR once");
        return false;
    }
    m_file = f_configDir / "steamvr.vrsettings";

    std::string l_content;
    if(CPlatform::ReadFileUtf8(m_file, l_content))
    {
        m_json = nlohmann::ordered_json::parse(l_content, nullptr, false);
        if(m_json.is_discarded())
        {
            m_json = nlohmann::ordered_json::object();
            m_error.assign("steamvr.vrsettings is not valid JSON, refusing to touch it");
            return false;
        }
        m_existed = true;
    }
    // Missing file is fine: SteamVR recreates it, and so can we

    m_loaded = true;
    return true;
}

bool CVRSettings::Save()
{
    if(!m_loaded) return false;

    if(m_existed)
    {
        // Snapshot only once: the backup is the pre-install state that uninstall restores
        std::error_code l_error;
        std::filesystem::path l_backup(m_file);
        l_backup += ".driver_asvr.bak";
        if(!std::filesystem::exists(l_backup, l_error))
        {
            std::filesystem::copy_file(m_file, l_backup, std::filesystem::copy_options::overwrite_existing, l_error);
        }
    }

    std::string l_content = m_json.dump(3);
    l_content.push_back('\n');
    if(!CPlatform::WriteFileUtf8(m_file, l_content))
    {
        m_error.assign("Failed to write steamvr.vrsettings");
        return false;
    }
    m_existed = true;
    return true;
}

bool CVRSettings::RestoreBackup()
{
    std::filesystem::path l_backup(m_file);
    l_backup += ".driver_asvr.bak";
    std::error_code l_error;
    std::filesystem::copy_file(l_backup, m_file, std::filesystem::copy_options::overwrite_existing, l_error);
    if(l_error) return false;
    std::filesystem::remove(l_backup, l_error); // consumed; the next install snapshots fresh
    return true;
}

bool CVRSettings::HasBackup() const
{
    if(m_file.empty()) return false;
    std::filesystem::path l_backup(m_file);
    l_backup += ".driver_asvr.bak";
    std::error_code l_error;
    return std::filesystem::exists(l_backup, l_error);
}

bool CVRSettings::IsLoaded() const
{
    return m_loaded;
}

const std::string& CVRSettings::GetError() const
{
    return m_error;
}

const std::filesystem::path& CVRSettings::GetFile() const
{
    return m_file;
}

bool CVRSettings::GetBool(const char *f_section, const char *f_key, bool f_default) const
{
    if(m_json.contains(f_section) && m_json[f_section].contains(f_key) && m_json[f_section][f_key].is_boolean())
    {
        return m_json[f_section][f_key].get<bool>();
    }
    return f_default;
}

int CVRSettings::GetInt(const char *f_section, const char *f_key, int f_default) const
{
    if(m_json.contains(f_section) && m_json[f_section].contains(f_key) && m_json[f_section][f_key].is_number())
    {
        return m_json[f_section][f_key].get<int>();
    }
    return f_default;
}

float CVRSettings::GetFloat(const char *f_section, const char *f_key, float f_default) const
{
    if(m_json.contains(f_section) && m_json[f_section].contains(f_key) && m_json[f_section][f_key].is_number())
    {
        return m_json[f_section][f_key].get<float>();
    }
    return f_default;
}

std::string CVRSettings::GetString(const char *f_section, const char *f_key, const char *f_default) const
{
    if(m_json.contains(f_section) && m_json[f_section].contains(f_key) && m_json[f_section][f_key].is_string())
    {
        return m_json[f_section][f_key].get<std::string>();
    }
    return f_default;
}

void CVRSettings::Set(const char *f_section, const char *f_key, const nlohmann::ordered_json &f_value)
{
    if(!m_json.contains(f_section) || !m_json[f_section].is_object()) m_json[f_section] = nlohmann::ordered_json::object();
    m_json[f_section][f_key] = f_value;
}

void CVRSettings::RemoveKey(const char *f_section, const char *f_key)
{
    if(m_json.contains(f_section) && m_json[f_section].is_object()) m_json[f_section].erase(f_key);
}

bool CVRSettings::GetActivateMultipleDrivers() const { return GetBool("steamvr", "activateMultipleDrivers", false); }
bool CVRSettings::GetAllowDisplayLockedMode() const { return GetBool("steamvr", "allowDisplayLockedMode", false); }
bool CVRSettings::GetDisableAsync() const { return GetBool("steamvr", "disableAsync", false); }
bool CVRSettings::GetDoNotFadeToGrid() const { return GetBool("steamvr", "doNotFadeToGrid", false); }
bool CVRSettings::GetPauseCompositorOnStandby() const { return GetBool("power", "pauseCompositorOnStandby", true); }
int CVRSettings::GetTurnOffScreensTimeout() const { return GetInt("power", "turnOffScreensTimeout", 30); }

void CVRSettings::SetActivateMultipleDrivers(bool f_state) { Set("steamvr", "activateMultipleDrivers", f_state); }
void CVRSettings::SetAllowDisplayLockedMode(bool f_state) { Set("steamvr", "allowDisplayLockedMode", f_state); }
void CVRSettings::SetDisableAsync(bool f_state) { Set("steamvr", "disableAsync", f_state); }
void CVRSettings::SetDoNotFadeToGrid(bool f_state) { Set("steamvr", "doNotFadeToGrid", f_state); }
void CVRSettings::SetPauseCompositorOnStandby(bool f_state) { Set("power", "pauseCompositorOnStandby", f_state); }
void CVRSettings::SetTurnOffScreensTimeout(int f_seconds) { Set("power", "turnOffScreensTimeout", f_seconds); }

void CVRSettings::ApplyRecommended()
{
    // Load alongside any real HMD's driver
    SetActivateMultipleDrivers(true);
    // RDP/headless: the session counts as "locked" and the compositor refuses to
    // composite app frames without this
    SetAllowDisplayLockedMode(true);
    // Async reprojection breaks app frame pacing without real display vsync
    SetDisableAsync(true);
    SetDoNotFadeToGrid(true);
    // The virtual HMD generates no "user activity"; stock timeouts blank the view
    SetTurnOffScreensTimeout(500000);
    SetPauseCompositorOnStandby(false);
}

// "driver_asvr" section - must stay in sync with the driver's DriverSettings
// and resources/settings/default.vrsettings
const char g_driverSection[] = "driver_asvr";

// SteamVR's standard per-driver switch: vrserver skips loading the driver
// entirely when enable == false, no unregistration needed
bool CVRSettings::GetDriverEnabled() const { return GetBool(g_driverSection, "enable", true); }
void CVRSettings::SetDriverEnabled(bool f_state) { Set(g_driverSection, "enable", f_state); }

bool CVRSettings::GetForcedDriver() const { return (GetString("steamvr", "forcedDriver", "") == "asvr"); }

void CVRSettings::SetForcedDriver(bool f_state)
{
    // Absent key is SteamVR's default (no forcing); never leave a stale value
    // pointing at this driver when the user turns it off
    if(f_state) Set("steamvr", "forcedDriver", "asvr");
    else if(GetForcedDriver()) RemoveKey("steamvr", "forcedDriver");
}

int CVRSettings::GetDriverKey(const char *f_key, int f_default) const { return GetInt(g_driverSection, f_key, f_default); }
void CVRSettings::SetDriverKey(const char *f_key, int f_value) { Set(g_driverSection, f_key, f_value); }

int CVRSettings::GetDriverWidth() const { return GetInt(g_driverSection, "resolutionWidth", 1024); }
int CVRSettings::GetDriverHeight() const { return GetInt(g_driverSection, "resolutionHeight", 768); }
float CVRSettings::GetDriverFPS() const { return GetFloat(g_driverSection, "fps", 60.f); }

bool CVRSettings::IsDriverPoseZero() const
{
    return (GetFloat(g_driverSection, "positionX", 0.f) == 0.f) &&
        (GetFloat(g_driverSection, "positionY", 0.f) == 0.f) &&
        (GetFloat(g_driverSection, "positionZ", 0.f) == 0.f) &&
        (GetFloat(g_driverSection, "rotationX", 0.f) == 0.f) &&
        (GetFloat(g_driverSection, "rotationY", 0.f) == 0.f) &&
        (GetFloat(g_driverSection, "rotationZ", 0.f) == 0.f) &&
        (GetFloat(g_driverSection, "rotationW", 1.f) == 1.f);
}

void CVRSettings::SetDriverResolution(int f_width, int f_height)
{
    Set(g_driverSection, "resolutionWidth", f_width);
    Set(g_driverSection, "resolutionHeight", f_height);
}

void CVRSettings::SetDriverFPS(float f_fps) { Set(g_driverSection, "fps", f_fps); }

void CVRSettings::ResetDriverPose()
{
    // Absent keys fall back to default.vrsettings (zero pose); the driver writes
    // fresh overrides on its next clean shutdown anyway
    const char *l_keys[] = { "positionX", "positionY", "positionZ", "rotationX", "rotationY", "rotationZ", "rotationW" };
    for(const char *l_key : l_keys) RemoveKey(g_driverSection, l_key);
}
