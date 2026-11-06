#pragma once

#include <string>
#include <filesystem>

// OS-level helpers for the monitor
namespace CPlatform
{
    std::filesystem::path GetExecutableDirectory();
    std::filesystem::path GetOpenVRPathsFile();
    // Per-user folder for the monitor's own files (UI state); created on demand
    std::filesystem::path GetStateDirectory();
    // %TEMP%\asvr_monitor.log on Windows, ~/.local/state/asvr/asvr_monitor.log on Linux
    std::filesystem::path GetLogFile();
    // Appends a timestamped line to GetLogFile(); the file starts fresh every run
    void Log(const char *f_text);
    void Log(const std::string &f_text);

    bool IsProcessRunning(const char *f_name); // f_name without extension, e.g. "vrserver"
    unsigned long FindProcessId(const char *f_name); // 0 when no such process

    bool ReadFileUtf8(const std::filesystem::path &f_path, std::string &f_out);
    bool WriteFileUtf8(const std::filesystem::path &f_path, const std::string &f_data); // atomic: .tmp + rename, no BOM

    std::filesystem::path NormalizeDriverPath(const std::filesystem::path &f_path);
    bool PathsEqual(const std::string &f_a, const std::string &f_b);

    // Runs a program detached from this process: no console, not a child we
    // would have to reap, and it outlives us
    bool LaunchDetached(const std::filesystem::path &f_executable, const std::filesystem::path &f_workingDirectory);
    bool OpenUrl(const std::string &f_url);
    // SteamVR's start-up launcher inside a runtime folder from openvrpaths.vrpath
    std::filesystem::path GetSteamVRLauncher(const std::filesystem::path &f_runtime);

    // One monitor window per user: the driver launches a copy whenever SteamVR
    // starts it, and the user may double-click the binary while one is open
    bool AcquireSingleInstance(); // false when another instance already runs
    void ReleaseSingleInstance();
    void RaiseExistingInstance(); // best effort: bring the other instance's window forward
    // A copy the user started (not the driver) tells the running instance,
    // which then outlives SteamVR like any user-opened window
    void SignalExistingInstance();
    bool PollUserLaunchSignal(); // running instance: true once per signal
}
