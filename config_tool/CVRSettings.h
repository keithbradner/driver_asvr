#pragma once

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

// Reads and edits steamvr.vrsettings, touching only the keys this driver cares
// about; everything else round-trips untouched (ordered_json keeps key order).
// The driver's own settings live in the "driver_asvr" section of the same
// file (as user overrides on top of the driver's default.vrsettings).
class CVRSettings final
{
    std::filesystem::path m_file;
    nlohmann::ordered_json m_json;
    bool m_loaded;
    bool m_existed;
    std::string m_error;

    bool GetBool(const char *f_section, const char *f_key, bool f_default) const;
    int GetInt(const char *f_section, const char *f_key, int f_default) const;
    float GetFloat(const char *f_section, const char *f_key, float f_default) const;
    std::string GetString(const char *f_section, const char *f_key, const char *f_default) const;
    void Set(const char *f_section, const char *f_key, const nlohmann::ordered_json &f_value);
    void RemoveKey(const char *f_section, const char *f_key);
public:
    CVRSettings();
    ~CVRSettings();

    bool Load(const std::filesystem::path &f_configDir); // missing file -> starts empty
    bool Save(); // backs up to steamvr.vrsettings.driver_asvr.bak first
    bool RestoreBackup();
    bool HasBackup() const;

    bool IsLoaded() const;
    const std::string& GetError() const;
    const std::filesystem::path& GetFile() const;

    bool GetActivateMultipleDrivers() const;
    bool GetAllowDisplayLockedMode() const;
    bool GetDisableAsync() const;
    bool GetDoNotFadeToGrid() const;
    bool GetPauseCompositorOnStandby() const;
    int GetTurnOffScreensTimeout() const;

    void SetActivateMultipleDrivers(bool f_state);
    void SetAllowDisplayLockedMode(bool f_state);
    void SetDisableAsync(bool f_state);
    void SetDoNotFadeToGrid(bool f_state);
    void SetPauseCompositorOnStandby(bool f_state);
    void SetTurnOffScreensTimeout(int f_seconds);

    void ApplyRecommended(); // everything Install needs in one call

    // "driver_asvr" section (defaults mirror the driver's default.vrsettings)
    bool GetDriverEnabled() const; // SteamVR's standard per-driver "enable" key
    void SetDriverEnabled(bool f_state);
    bool GetForcedDriver() const; // steamvr.forcedDriver == "asvr"
    void SetForcedDriver(bool f_state);
    int GetDriverKey(const char *f_key, int f_default) const; // monitor key bindings
    void SetDriverKey(const char *f_key, int f_value);
    int GetDriverWidth() const;
    int GetDriverHeight() const;
    float GetDriverFPS() const;
    bool IsDriverPoseZero() const;

    void SetDriverResolution(int f_width, int f_height);
    void SetDriverFPS(float f_fps);
    void ResetDriverPose(); // drops the overrides, falling back to the zero pose
};
