#include "CPlatform.h"

#include <fstream>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <ctime>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <shellapi.h>
#else
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

namespace CPlatform
{

#ifdef _WIN32

std::filesystem::path GetExecutableDirectory()
{
    wchar_t l_buffer[MAX_PATH];
    GetModuleFileNameW(NULL, l_buffer, MAX_PATH);
    return std::filesystem::path(l_buffer).parent_path();
}

std::filesystem::path GetOpenVRPathsFile()
{
    wchar_t l_buffer[MAX_PATH] = { 0 };
    DWORD l_length = GetEnvironmentVariableW(L"LOCALAPPDATA", l_buffer, MAX_PATH);
    if((l_length == 0U) || (l_length >= MAX_PATH)) return std::filesystem::path();
    return std::filesystem::path(l_buffer) / "openvr" / "openvrpaths.vrpath";
}

std::filesystem::path GetStateDirectory()
{
    wchar_t l_buffer[MAX_PATH] = { 0 };
    DWORD l_length = GetEnvironmentVariableW(L"LOCALAPPDATA", l_buffer, MAX_PATH);
    if((l_length == 0U) || (l_length >= MAX_PATH)) return std::filesystem::path();
    std::filesystem::path l_directory = std::filesystem::path(l_buffer) / "asvr";
    std::error_code l_error;
    std::filesystem::create_directories(l_directory, l_error);
    return l_directory;
}

std::filesystem::path GetLogFile()
{
    wchar_t l_buffer[MAX_PATH] = { 0 };
    if(GetTempPathW(MAX_PATH, l_buffer) == 0U) return std::filesystem::path();
    return std::filesystem::path(l_buffer) / "asvr_monitor.log";
}

unsigned long FindProcessId(const char *f_name)
{
    std::wstring l_target;
    for(const char *p = f_name; *p; p++) l_target.push_back(static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(*p))));
    l_target.append(L".exe");

    HANDLE l_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U);
    if(l_snapshot == INVALID_HANDLE_VALUE) return 0UL;

    unsigned long l_result = 0UL;
    PROCESSENTRY32W l_entry = { 0 };
    l_entry.dwSize = sizeof(PROCESSENTRY32W);
    if(Process32FirstW(l_snapshot, &l_entry))
    {
        do
        {
            std::wstring l_name(l_entry.szExeFile);
            for(auto &l_char : l_name) l_char = towlower(l_char);
            if(l_name == l_target)
            {
                l_result = l_entry.th32ProcessID;
                break;
            }
        } while(Process32NextW(l_snapshot, &l_entry));
    }
    CloseHandle(l_snapshot);
    return l_result;
}

bool LaunchDetached(const std::filesystem::path &f_executable, const std::filesystem::path &f_workingDirectory)
{
    // CreateProcess may modify the command line buffer
    std::wstring l_commandLine = L"\"" + f_executable.wstring() + L"\"";
    STARTUPINFOW l_startup = {};
    l_startup.cb = sizeof(l_startup);
    PROCESS_INFORMATION l_process = {};
    const std::wstring l_workingDirectory = f_workingDirectory.wstring();
    if(!CreateProcessW(f_executable.c_str(), &l_commandLine[0U], NULL, NULL, FALSE, 0U, NULL,
        l_workingDirectory.empty() ? NULL : l_workingDirectory.c_str(), &l_startup, &l_process))
    {
        return false;
    }
    CloseHandle(l_process.hThread);
    CloseHandle(l_process.hProcess);
    return true;
}

bool OpenUrl(const std::string &f_url)
{
    std::wstring l_url(f_url.begin(), f_url.end()); // URLs are ASCII
    return (reinterpret_cast<INT_PTR>(ShellExecuteW(NULL, L"open", l_url.c_str(), NULL, NULL, SW_SHOWNORMAL)) > 32);
}

std::filesystem::path GetSteamVRLauncher(const std::filesystem::path &f_runtime)
{
    return f_runtime / "bin" / "win64" / "vrstartup.exe";
}

static HANDLE g_instanceMutex = NULL;
static HANDLE g_userLaunchEvent = NULL; // auto-reset, set by a user-started second copy
static const wchar_t g_userLaunchEventName[] = L"Local\\asvr_monitor_user_launch";

bool AcquireSingleInstance()
{
    g_instanceMutex = CreateMutexW(NULL, FALSE, L"Local\\asvr_monitor_instance");
    if(g_instanceMutex == NULL) return true; // can't tell; let it run
    if(GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = NULL;
        return false;
    }
    g_userLaunchEvent = CreateEventW(NULL, FALSE, FALSE, g_userLaunchEventName);
    return true;
}

void ReleaseSingleInstance()
{
    if(g_userLaunchEvent)
    {
        CloseHandle(g_userLaunchEvent);
        g_userLaunchEvent = NULL;
    }
    if(g_instanceMutex)
    {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = NULL;
    }
}

void RaiseExistingInstance()
{
    // GLFW registers its window class as "GLFW30"; the monitor's title is fixed
    HWND l_window = FindWindowW(L"GLFW30", L"ASVR");
    if(!l_window) l_window = FindWindowW(NULL, L"ASVR");
    if(!l_window) return;
    if(IsIconic(l_window)) ShowWindow(l_window, SW_RESTORE);
    SetForegroundWindow(l_window);
}

void SignalExistingInstance()
{
    HANDLE l_event = OpenEventW(EVENT_MODIFY_STATE, FALSE, g_userLaunchEventName);
    if(!l_event) return;
    SetEvent(l_event);
    CloseHandle(l_event);
}

bool PollUserLaunchSignal()
{
    return g_userLaunchEvent && (WaitForSingleObject(g_userLaunchEvent, 0U) == WAIT_OBJECT_0);
}

#else

std::filesystem::path GetExecutableDirectory()
{
    char l_buffer[1024];
    const ssize_t l_length = readlink("/proc/self/exe", l_buffer, sizeof(l_buffer) - 1U);
    if(l_length <= 0) return std::filesystem::path();
    l_buffer[l_length] = '\0';
    return std::filesystem::path(l_buffer).parent_path();
}

std::filesystem::path GetOpenVRPathsFile()
{
    const char *l_configHome = std::getenv("XDG_CONFIG_HOME");
    if(l_configHome && l_configHome[0]) return std::filesystem::path(l_configHome) / "openvr" / "openvrpaths.vrpath";
    const char *l_home = std::getenv("HOME");
    if(!l_home || !l_home[0]) return std::filesystem::path();
    return std::filesystem::path(l_home) / ".config" / "openvr" / "openvrpaths.vrpath";
}

std::filesystem::path GetStateDirectory()
{
    std::filesystem::path l_directory;
    const char *l_stateHome = std::getenv("XDG_STATE_HOME");
    if(l_stateHome && l_stateHome[0]) l_directory = l_stateHome;
    else
    {
        const char *l_home = std::getenv("HOME");
        if(!l_home || !l_home[0]) return std::filesystem::path();
        l_directory = std::filesystem::path(l_home) / ".local" / "state";
    }
    l_directory /= "asvr";
    std::error_code l_error;
    std::filesystem::create_directories(l_directory, l_error);
    return l_directory;
}

std::filesystem::path GetLogFile()
{
    const std::filesystem::path l_directory = GetStateDirectory();
    if(!l_directory.empty()) return l_directory / "asvr_monitor.log";
    return std::filesystem::path("/tmp/asvr_monitor." + std::to_string(getuid()) + ".log");
}

unsigned long FindProcessId(const char *f_name)
{
    // /proc scan; comm is the executable's base name truncated to 15 chars
    std::string l_target(f_name);
    if(l_target.size() > 15U) l_target.resize(15U);
    std::error_code l_error;
    for(const auto &l_entry : std::filesystem::directory_iterator("/proc", l_error))
    {
        const std::string l_pid = l_entry.path().filename().string();
        if(l_pid.empty() || !std::isdigit(static_cast<unsigned char>(l_pid[0]))) continue;
        std::ifstream l_stream(l_entry.path() / "comm");
        if(!l_stream) continue;
        std::string l_comm;
        std::getline(l_stream, l_comm);
        if(l_comm == l_target) return std::strtoul(l_pid.c_str(), nullptr, 10);
    }
    return 0UL;
}

// Double-fork so the program is reparented to init: no SIGCHLD for us and no
// zombie when it exits
static bool ForkAndExec(const std::filesystem::path &f_workingDirectory, const char *f_file, const char *f_argument, bool f_searchPath)
{
    const pid_t l_child = fork();
    if(l_child < 0) return false;
    if(l_child == 0)
    {
        setsid();
        const pid_t l_grandChild = fork();
        if(l_grandChild == 0)
        {
            if(!f_workingDirectory.empty() && (chdir(f_workingDirectory.c_str()) != 0)) _exit(127);
            if(f_searchPath) execlp(f_file, f_file, f_argument, static_cast<char*>(nullptr));
            else execl(f_file, f_file, f_argument, static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit((l_grandChild > 0) ? 0 : 126);
    }
    int l_status = 0;
    waitpid(l_child, &l_status, 0);
    return (WIFEXITED(l_status) && (WEXITSTATUS(l_status) == 0));
}

bool LaunchDetached(const std::filesystem::path &f_executable, const std::filesystem::path &f_workingDirectory)
{
    return ForkAndExec(f_workingDirectory, f_executable.c_str(), nullptr, false);
}

bool OpenUrl(const std::string &f_url)
{
    return ForkAndExec(std::filesystem::path(), "xdg-open", f_url.c_str(), true);
}

std::filesystem::path GetSteamVRLauncher(const std::filesystem::path &f_runtime)
{
    return f_runtime / "bin" / "vrstartup.sh";
}

// Abstract unix socket: no file to clean up, released by the kernel when the
// process dies, invisible to other users through the uid suffix. The running
// instance listens on it; a user-started second copy connects to say hello.
static int g_instanceSocket = -1;

static bool MakeInstanceAddress(sockaddr_un &f_address, socklen_t &f_length)
{
    const std::string l_name = "asvr_monitor_instance." + std::to_string(getuid());
    memset(&f_address, 0, sizeof(f_address));
    f_address.sun_family = AF_UNIX;
    if(l_name.size() + 1U > sizeof(f_address.sun_path)) return false;
    memcpy(f_address.sun_path + 1, l_name.c_str(), l_name.size());
    f_length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1U + l_name.size());
    return true;
}

bool AcquireSingleInstance()
{
    sockaddr_un l_address;
    socklen_t l_length = 0U;
    if(!MakeInstanceAddress(l_address, l_length)) return true;
    g_instanceSocket = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if(g_instanceSocket < 0) return true; // can't tell; let it run
    if((bind(g_instanceSocket, reinterpret_cast<sockaddr*>(&l_address), l_length) != 0) || (listen(g_instanceSocket, 4) != 0))
    {
        close(g_instanceSocket);
        g_instanceSocket = -1;
        return false;
    }
    return true;
}

void ReleaseSingleInstance()
{
    if(g_instanceSocket >= 0)
    {
        close(g_instanceSocket);
        g_instanceSocket = -1;
    }
}

void RaiseExistingInstance()
{
    // No portable way to raise another process's X11/Wayland window; the
    // existing window is usually on screen already
}

void SignalExistingInstance()
{
    sockaddr_un l_address;
    socklen_t l_length = 0U;
    if(!MakeInstanceAddress(l_address, l_length)) return;
    const int l_socket = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(l_socket < 0) return;
    connect(l_socket, reinterpret_cast<sockaddr*>(&l_address), l_length); // the connection itself is the message
    close(l_socket);
}

bool PollUserLaunchSignal()
{
    if(g_instanceSocket < 0) return false;
    const int l_client = accept(g_instanceSocket, nullptr, nullptr);
    if(l_client < 0) return false;
    close(l_client);
    return true;
}

#endif

bool IsProcessRunning(const char *f_name)
{
    return (FindProcessId(f_name) != 0UL);
}

void Log(const char *f_text)
{
    static const std::filesystem::path s_path = GetLogFile();
    static bool s_appending = false;
    if(s_path.empty()) return;
    FILE *l_stream = nullptr;
#ifdef _WIN32
    if(_wfopen_s(&l_stream, s_path.c_str(), s_appending ? L"a" : L"w") != 0) l_stream = nullptr;
#else
    l_stream = fopen(s_path.c_str(), s_appending ? "a" : "w");
#endif
    if(!l_stream) return;
    s_appending = true;
    const std::time_t l_now = std::time(nullptr);
    std::tm l_time = {};
#ifdef _WIN32
    localtime_s(&l_time, &l_now);
#else
    localtime_r(&l_now, &l_time);
#endif
    fprintf(l_stream, "[%02d:%02d:%02d] %s\n", l_time.tm_hour, l_time.tm_min, l_time.tm_sec, f_text);
    fclose(l_stream);
}

void Log(const std::string &f_text)
{
    Log(f_text.c_str());
}

bool ReadFileUtf8(const std::filesystem::path &f_path, std::string &f_out)
{
    std::ifstream l_stream(f_path, std::ios::binary);
    if(!l_stream) return false;
    f_out.assign(std::istreambuf_iterator<char>(l_stream), std::istreambuf_iterator<char>());
    if((f_out.size() >= 3U) && (static_cast<unsigned char>(f_out[0U]) == 0xEFU) && (static_cast<unsigned char>(f_out[1U]) == 0xBBU) && (static_cast<unsigned char>(f_out[2U]) == 0xBFU))
    {
        f_out.erase(0U, 3U); // strip UTF-8 BOM
    }
    return true;
}

bool WriteFileUtf8(const std::filesystem::path &f_path, const std::string &f_data)
{
    // Write a sibling temp file, then rename over the target: a crash can never
    // leave a half-written openvrpaths.vrpath / steamvr.vrsettings behind
    std::filesystem::path l_temp(f_path);
    l_temp += ".tmp";
    {
        std::ofstream l_stream(l_temp, std::ios::binary | std::ios::trunc);
        if(!l_stream) return false;
        l_stream.write(f_data.data(), static_cast<std::streamsize>(f_data.size()));
        if(!l_stream.good()) return false;
    }
    std::error_code l_error;
    std::filesystem::rename(l_temp, f_path, l_error);
    if(l_error)
    {
        std::filesystem::remove(l_temp, l_error);
        return false;
    }
    return true;
}

std::filesystem::path NormalizeDriverPath(const std::filesystem::path &f_path)
{
    std::error_code l_error;
    std::filesystem::path l_result = std::filesystem::absolute(f_path, l_error);
    if(l_error) l_result = f_path;
    l_result = l_result.lexically_normal();
    std::string l_string = l_result.generic_string();
    while(!l_string.empty() && (l_string.back() == '/')) l_string.pop_back();
    return std::filesystem::path(l_string).make_preferred();
}

bool PathsEqual(const std::string &f_a, const std::string &f_b)
{
    auto l_canon = [](const std::string &f_in)
    {
        std::string l_out(f_in);
        for(auto &l_char : l_out)
        {
            l_char = static_cast<char>(std::tolower(static_cast<unsigned char>(l_char)));
            if(l_char == '\\') l_char = '/';
        }
        while(!l_out.empty() && (l_out.back() == '/')) l_out.pop_back();
        return l_out;
    };
    return (l_canon(f_a) == l_canon(f_b));
}

}
