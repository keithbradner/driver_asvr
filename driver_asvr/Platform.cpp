#include "stdafx.h"

#include "Platform.h"

#ifndef _WIN32
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

extern std::string g_modulePath;

std::string Platform::GetModuleDirectory()
{
    const size_t l_dirEnd = g_modulePath.find_last_of("/\\");
    if(l_dirEnd == std::string::npos) return std::string();
    return g_modulePath.substr(0U, l_dirEnd);
}

static void LogLine(const std::string &f_line)
{
    vr::IVRDriverLog *l_log = vr::VRDriverLog();
    if(l_log) l_log->Log(f_line.c_str());
}

#ifdef _WIN32

static bool IsExecutableFile(const std::string &f_path)
{
    const DWORD l_attributes = GetFileAttributesA(f_path.c_str());
    return (l_attributes != INVALID_FILE_ATTRIBUTES) && ((l_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U);
}

// Collapses the "..\..\.." of the package-root candidate so the log line and
// the monitor's working directory are plain paths
static std::string NormalizePath(const std::string &f_path)
{
    char l_buffer[2048];
    const DWORD l_length = GetFullPathNameA(f_path.c_str(), sizeof(l_buffer), l_buffer, NULL);
    if((l_length == 0U) || (l_length >= sizeof(l_buffer))) return f_path;
    return std::string(l_buffer, l_length);
}

void Platform::LaunchMonitor(const std::string &f_directory)
{
    const std::string l_candidates[2] = { f_directory + "\\asvr_monitor.exe", f_directory + "\\..\\..\\..\\asvr_monitor.exe" };
    std::string l_monitorExe;
    for(const std::string &l_candidate : l_candidates)
    {
        if(IsExecutableFile(l_candidate))
        {
            l_monitorExe = NormalizePath(l_candidate);
            break;
        }
    }
    if(l_monitorExe.empty())
    {
        LogLine("Platform: asvr_monitor.exe not found, looked at " + l_candidates[0] + " and " + l_candidates[1]);
        return;
    }
    const std::string l_workingDirectory = l_monitorExe.substr(0U, l_monitorExe.find_last_of("\\/"));

    // CreateProcess may write into the command line buffer, so it cannot be a literal
    std::string l_commandLine = "\"" + l_monitorExe + "\" --launched-by-driver";
    std::vector<char> l_mutableCommandLine(l_commandLine.begin(), l_commandLine.end());
    l_mutableCommandLine.push_back('\0');

    STARTUPINFOA l_startup = {};
    l_startup.cb = sizeof(l_startup);
    PROCESS_INFORMATION l_process = {};
    if(CreateProcessA(l_monitorExe.c_str(), l_mutableCommandLine.data(), NULL, NULL, FALSE, 0U, NULL, l_workingDirectory.c_str(), &l_startup, &l_process))
    {
        CloseHandle(l_process.hThread);
        CloseHandle(l_process.hProcess);
        LogLine("Platform: launched " + l_monitorExe);
    }
    else
    {
        char l_error[32];
        snprintf(l_error, sizeof(l_error), "%lu", GetLastError());
        LogLine("Platform: failed to launch " + l_monitorExe + ", error=" + l_error);
    }
}

#else

static bool IsExecutableFile(const std::string &f_path)
{
    return access(f_path.c_str(), X_OK) == 0;
}

static std::string NormalizePath(const std::string &f_path)
{
    char l_buffer[PATH_MAX];
    if(!realpath(f_path.c_str(), l_buffer)) return f_path;
    return std::string(l_buffer);
}

void Platform::LaunchMonitor(const std::string &f_directory)
{
    const std::string l_candidates[2] = { f_directory + "/asvr_monitor", f_directory + "/../../../asvr_monitor" };
    std::string l_monitorExe;
    for(const std::string &l_candidate : l_candidates)
    {
        if(IsExecutableFile(l_candidate))
        {
            l_monitorExe = NormalizePath(l_candidate);
            break;
        }
    }
    if(l_monitorExe.empty())
    {
        LogLine("Platform: asvr_monitor not found, looked at " + l_candidates[0] + " and " + l_candidates[1]);
        return;
    }
    const std::string l_workingDirectory = l_monitorExe.substr(0U, l_monitorExe.find_last_of('/'));

    // Double-fork so the monitor is reparented to init: vrserver never gets a
    // SIGCHLD from it and no zombie lingers when the monitor exits on its own
    const pid_t l_child = fork();
    if(l_child == 0)
    {
        setsid();
        const pid_t l_grandChild = fork();
        if(l_grandChild == 0)
        {
            if(chdir(l_workingDirectory.c_str()) != 0) _exit(127);
            execl(l_monitorExe.c_str(), l_monitorExe.c_str(), "--launched-by-driver", static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit((l_grandChild > 0) ? 0 : 126);
    }

    if(l_child > 0)
    {
        int l_status = 0;
        waitpid(l_child, &l_status, 0);
        LogLine("Platform: launched " + l_monitorExe);
    }
    else LogLine("Platform: failed to fork for asvr_monitor");
}

#endif
