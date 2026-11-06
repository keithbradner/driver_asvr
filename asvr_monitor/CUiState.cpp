#include "CUiState.h"
#include "CPlatform.h"

#include <nlohmann/json.hpp>

static std::filesystem::path StateFile()
{
    const std::filesystem::path l_directory = CPlatform::GetStateDirectory();
    if(l_directory.empty()) return std::filesystem::path();
    return l_directory / "asvr_monitor.json";
}

UiState LoadUiState()
{
    UiState l_state;
    const std::filesystem::path l_file = StateFile();
    std::string l_content;
    if(l_file.empty() || !CPlatform::ReadFileUtf8(l_file, l_content)) return l_state;

    const nlohmann::json l_json = nlohmann::json::parse(l_content, nullptr, false);
    if(!l_json.is_object()) return l_state; // unreadable file: defaults, and the next save repairs it
    // Only values of the expected type count; a hand-edited "true" string or
    // the like falls back to the default instead of throwing
    auto l_readBool = [&l_json](const char *f_key, bool &f_out)
    {
        const auto l_found = l_json.find(f_key);
        if((l_found != l_json.end()) && l_found->is_boolean()) f_out = l_found->get<bool>();
    };
    auto l_readInt = [&l_json](const char *f_key, int &f_out)
    {
        const auto l_found = l_json.find(f_key);
        if((l_found != l_json.end()) && l_found->is_number_integer()) f_out = l_found->get<int>();
    };
    l_readBool("settingsOpen", l_state.m_settingsOpen);
    l_readBool("bindingsOpen", l_state.m_bindingsOpen);
    l_readInt("windowWidth", l_state.m_windowWidth);
    l_readInt("windowHeight", l_state.m_windowHeight);
    return l_state;
}

void SaveUiState(const UiState &f_state)
{
    const std::filesystem::path l_file = StateFile();
    if(l_file.empty()) return;
    nlohmann::json l_json;
    l_json["settingsOpen"] = f_state.m_settingsOpen;
    l_json["bindingsOpen"] = f_state.m_bindingsOpen;
    l_json["windowWidth"] = f_state.m_windowWidth;
    l_json["windowHeight"] = f_state.m_windowHeight;
    CPlatform::WriteFileUtf8(l_file, l_json.dump(2) + "\n");
}
