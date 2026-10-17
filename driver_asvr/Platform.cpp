#include "stdafx.h"

#include "Platform.h"

#ifndef _WIN32
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

#ifdef _WIN32

static HANDLE g_primaryGuard = NULL;

bool Platform::AcquirePrimaryGuard()
{
    g_primaryGuard = CreateMutexA(NULL, FALSE, "Local\\driver_asvr_primary");
    return (g_primaryGuard != NULL) && (GetLastError() != ERROR_ALREADY_EXISTS);
}

void Platform::ReleasePrimaryGuard()
{
    if(g_primaryGuard)
    {
        CloseHandle(g_primaryGuard);
        g_primaryGuard = NULL;
    }
}

void Platform::LaunchMonitor(const std::string &f_directory)
{
    const std::string l_monitorExe = f_directory + "\\asvr_monitor.exe";

    STARTUPINFOA l_startup = {};
    l_startup.cb = sizeof(l_startup);
    PROCESS_INFORMATION l_process = {};
    if(CreateProcessA(l_monitorExe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, f_directory.c_str(), &l_startup, &l_process))
    {
        CloseHandle(l_process.hThread);
        CloseHandle(l_process.hProcess);
    }
    else
    {
        char l_line[128];
        snprintf(l_line, sizeof(l_line), "CServerDriver: failed to launch asvr_monitor.exe, error=%lu", GetLastError());
        vr::VRDriverLog()->Log(l_line);
    }
}

#else

bool Platform::AcquirePrimaryGuard()
{
    // Only vrserver loads the driver on Linux (no compositor plugin provider,
    // and the direct-mode component is served over vrserver IPC)
    return true;
}

void Platform::ReleasePrimaryGuard()
{
}

void Platform::LaunchMonitor(const std::string &f_directory)
{
    const std::string l_monitorExe = f_directory + "/asvr_monitor";

    // Double-fork so the monitor is reparented to init: vrserver never gets a
    // SIGCHLD from it and no zombie lingers when the monitor exits on its own
    const pid_t l_child = fork();
    if(l_child == 0)
    {
        setsid();
        const pid_t l_grandChild = fork();
        if(l_grandChild == 0)
        {
            if(chdir(f_directory.c_str()) != 0) _exit(127);
            execl(l_monitorExe.c_str(), l_monitorExe.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit((l_grandChild > 0) ? 0 : 126);
    }

    if(l_child > 0)
    {
        int l_status = 0;
        waitpid(l_child, &l_status, 0);
    }
    else vr::VRDriverLog()->Log("CServerDriver: failed to fork for asvr_monitor");
}

#endif
