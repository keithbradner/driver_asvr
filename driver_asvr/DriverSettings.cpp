#include "stdafx.h"
#include "DriverSettings.h"

// Section in default.vrsettings / steamvr.vrsettings; user overrides written by
// the Config tool (and the pose saved on shutdown) land in steamvr.vrsettings
const char g_settingsSection[] = "driver_asvr";

static int32_t ReadInt(vr::IVRSettings *f_settings, const char *f_key, int32_t f_default)
{
    vr::EVRSettingsError l_error = vr::VRSettingsError_None;
    const int32_t l_value = f_settings->GetInt32(g_settingsSection, f_key, &l_error);
    return (l_error == vr::VRSettingsError_None) ? l_value : f_default;
}

static float ReadFloat(vr::IVRSettings *f_settings, const char *f_key, float f_default)
{
    vr::EVRSettingsError l_error = vr::VRSettingsError_None;
    const float l_value = f_settings->GetFloat(g_settingsSection, f_key, &l_error);
    return (l_error == vr::VRSettingsError_None) ? l_value : f_default;
}

// Comma-separated role names, trimmed and lower-cased; key absent -> the default
// list, "" or "none" -> no trackers
static void ReadTrackers(vr::IVRSettings *f_settings, std::vector<std::string> &f_trackers)
{
    char l_buffer[256] = { 0 };
    vr::EVRSettingsError l_error = vr::VRSettingsError_None;
    f_settings->GetString(g_settingsSection, "trackers", l_buffer, sizeof(l_buffer), &l_error);
    if(l_error != vr::VRSettingsError_None) return;

    f_trackers.clear();
    std::stringstream l_list(l_buffer);
    std::string l_item;
    while(std::getline(l_list, l_item, ','))
    {
        const size_t l_begin = l_item.find_first_not_of(" \t");
        if(l_begin == std::string::npos) continue;
        l_item = l_item.substr(l_begin, l_item.find_last_not_of(" \t") - l_begin + 1U);
        for(char &l_char : l_item) l_char = static_cast<char>(tolower(static_cast<unsigned char>(l_char)));
        if(l_item.compare("none") != 0) f_trackers.push_back(l_item);
    }
}

DriverSettings LoadDriverSettings()
{
    DriverSettings l_result;

    vr::IVRSettings *l_settings = vr::VRSettings();
    if(!l_settings) return l_result;

    ReadTrackers(l_settings, l_result.trackers);

    const int32_t l_width = ReadInt(l_settings, "resolutionWidth", static_cast<int32_t>(l_result.resolution.x));
    const int32_t l_height = ReadInt(l_settings, "resolutionHeight", static_cast<int32_t>(l_result.resolution.y));
    if(l_width > 1) l_result.resolution.x = static_cast<unsigned int>(l_width);
    if(l_height > 1) l_result.resolution.y = static_cast<unsigned int>(l_height);

    const float l_fps = ReadFloat(l_settings, "fps", l_result.fps);
    if(l_fps >= 1.f) l_result.fps = l_fps;

    l_result.position.x = ReadFloat(l_settings, "positionX", l_result.position.x);
    l_result.position.y = ReadFloat(l_settings, "positionY", l_result.position.y);
    l_result.position.z = ReadFloat(l_settings, "positionZ", l_result.position.z);

    glm::quat l_rotation;
    l_rotation.x = ReadFloat(l_settings, "rotationX", l_result.rotation.x);
    l_rotation.y = ReadFloat(l_settings, "rotationY", l_result.rotation.y);
    l_rotation.z = ReadFloat(l_settings, "rotationZ", l_result.rotation.z);
    l_rotation.w = ReadFloat(l_settings, "rotationW", l_result.rotation.w);
    if(glm::length(l_rotation) >= 0.001f) l_result.rotation = glm::normalize(l_rotation);

    return l_result;
}

bool SaveDriverPose(const glm::vec3 &f_pos, const glm::quat &f_rot)
{
    vr::IVRSettings *l_settings = vr::VRSettings();
    if(!l_settings) return false;

    bool l_saved = true;
    const std::pair<const char*, float> l_values[]
    {
        { "positionX", f_pos.x }, { "positionY", f_pos.y }, { "positionZ", f_pos.z },
        { "rotationX", f_rot.x }, { "rotationY", f_rot.y }, { "rotationZ", f_rot.z }, { "rotationW", f_rot.w }
    };
    for(const auto &l_entry : l_values)
    {
        vr::EVRSettingsError l_error = vr::VRSettingsError_None;
        l_settings->SetFloat(g_settingsSection, l_entry.first, l_entry.second, &l_error);
        if(l_error != vr::VRSettingsError_None) l_saved = false;
    }
    return l_saved;
}
