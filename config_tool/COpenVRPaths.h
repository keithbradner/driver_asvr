#pragma once

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

// Reads and edits openvrpaths.vrpath. Never creates the file: it only exists
// once SteamVR has run, and fabricating it would confuse the runtime.
class COpenVRPaths final
{
    std::filesystem::path m_file;
    nlohmann::ordered_json m_json;
    bool m_loaded;
    std::string m_error;
public:
    COpenVRPaths();
    ~COpenVRPaths();

    bool Load();
    bool Save(); // backs up to openvrpaths.vrpath.driver_asvr.bak first

    bool IsLoaded() const;
    const std::string& GetError() const;
    const std::filesystem::path& GetFile() const;

    std::string GetRuntimePath() const;
    std::string GetConfigPath() const;

    bool IsDriverRegistered(const std::string &f_path) const;
    void AddDriver(const std::string &f_path);
    void RemoveDriver(const std::string &f_path);
};
