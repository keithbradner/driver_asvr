#include "CPlatform.h"

#include <fstream>
#include <cctype>
#include <cwctype>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

namespace CPlatform
{

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

bool IsProcessRunning(const char *f_name)
{
    std::wstring l_target;
    for(const char *p = f_name; *p; p++) l_target.push_back(static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(*p))));
    l_target.append(L".exe");

    HANDLE l_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U);
    if(l_snapshot == INVALID_HANDLE_VALUE) return false;

    bool l_result = false;
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
                l_result = true;
                break;
            }
        } while(Process32NextW(l_snapshot, &l_entry));
    }
    CloseHandle(l_snapshot);
    return l_result;
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
