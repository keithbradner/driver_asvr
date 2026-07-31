#pragma once

#include <string>
#include <filesystem>

// OS-level helpers for the Config tool
namespace CPlatform
{
    std::filesystem::path GetExecutableDirectory();
    std::filesystem::path GetOpenVRPathsFile();
    bool IsProcessRunning(const char *f_name); // f_name without extension, e.g. "vrserver"

    bool ReadFileUtf8(const std::filesystem::path &f_path, std::string &f_out);
    bool WriteFileUtf8(const std::filesystem::path &f_path, const std::string &f_data); // atomic: .tmp + rename, no BOM

    std::filesystem::path NormalizeDriverPath(const std::filesystem::path &f_path);
    bool PathsEqual(const std::string &f_a, const std::string &f_b);
}
