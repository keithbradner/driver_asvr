#include "COpenVRPaths.h"
#include "CPlatform.h"

COpenVRPaths::COpenVRPaths()
{
    m_loaded = false;
}

COpenVRPaths::~COpenVRPaths()
{
}

bool COpenVRPaths::Load()
{
    m_loaded = false;
    m_error.clear();
    m_json = nlohmann::ordered_json::object();

    m_file = CPlatform::GetOpenVRPathsFile();
    if(m_file.empty())
    {
        m_error.assign("Could not resolve the OpenVR config location");
        return false;
    }

    std::string l_content;
    if(!CPlatform::ReadFileUtf8(m_file, l_content))
    {
        m_error.assign("openvrpaths.vrpath not found - install SteamVR and run it once");
        return false;
    }

    m_json = nlohmann::ordered_json::parse(l_content, nullptr, false);
    if(m_json.is_discarded())
    {
        m_json = nlohmann::ordered_json::object();
        m_error.assign("openvrpaths.vrpath is not valid JSON, refusing to touch it");
        return false;
    }

    m_loaded = true;
    return true;
}

bool COpenVRPaths::Save()
{
    if(!m_loaded) return false;

    std::error_code l_error;
    std::filesystem::path l_backup(m_file);
    l_backup += ".driver_asvr.bak";
    std::filesystem::copy_file(m_file, l_backup, std::filesystem::copy_options::overwrite_existing, l_error);

    std::string l_content = m_json.dump(1, '\t');
    l_content.push_back('\n');
    if(!CPlatform::WriteFileUtf8(m_file, l_content))
    {
        m_error.assign("Failed to write openvrpaths.vrpath");
        return false;
    }
    return true;
}

bool COpenVRPaths::IsLoaded() const
{
    return m_loaded;
}

const std::string& COpenVRPaths::GetError() const
{
    return m_error;
}

const std::filesystem::path& COpenVRPaths::GetFile() const
{
    return m_file;
}

std::string COpenVRPaths::GetRuntimePath() const
{
    std::string l_result;
    if(m_loaded && m_json.contains("runtime") && m_json["runtime"].is_array() && !m_json["runtime"].empty() && m_json["runtime"][0U].is_string())
    {
        l_result = m_json["runtime"][0U].get<std::string>();
    }
    return l_result;
}

std::string COpenVRPaths::GetConfigPath() const
{
    std::string l_result;
    if(m_loaded && m_json.contains("config") && m_json["config"].is_array() && !m_json["config"].empty() && m_json["config"][0U].is_string())
    {
        l_result = m_json["config"][0U].get<std::string>();
    }
    return l_result;
}

bool COpenVRPaths::IsDriverRegistered(const std::string &f_path) const
{
    if(!m_loaded || !m_json.contains("external_drivers") || !m_json["external_drivers"].is_array()) return false;
    for(const auto &l_entry : m_json["external_drivers"])
    {
        if(l_entry.is_string() && CPlatform::PathsEqual(l_entry.get<std::string>(), f_path)) return true;
    }
    return false;
}

void COpenVRPaths::AddDriver(const std::string &f_path)
{
    if(!m_loaded) return;
    if(!m_json.contains("external_drivers") || !m_json["external_drivers"].is_array()) m_json["external_drivers"] = nlohmann::ordered_json::array();
    if(!IsDriverRegistered(f_path)) m_json["external_drivers"].push_back(f_path);
}

void COpenVRPaths::RemoveDriver(const std::string &f_path)
{
    if(!m_loaded || !m_json.contains("external_drivers") || !m_json["external_drivers"].is_array()) return;
    auto &l_drivers = m_json["external_drivers"];
    for(auto l_iter = l_drivers.begin(); l_iter != l_drivers.end();)
    {
        if(l_iter->is_string() && CPlatform::PathsEqual(l_iter->get<std::string>(), f_path)) l_iter = l_drivers.erase(l_iter);
        else ++l_iter;
    }
    // SteamVR writes "external_drivers": null when empty
    if(l_drivers.empty()) m_json["external_drivers"] = nullptr;
}
