#include "stdafx.h"

#include "Screenshot.h"
#include "../shared/ControlProtocol.h"
#include "../shared/SharedFrameInfo.h"

// Command-line control for the ASVR driver: sends one command line over the
// control pipe and prints the driver's reply. `screenshot` and `frame` are
// handled locally from the shared frame mapping instead.
// Exit codes: 0 = ok, 1 = driver replied "err ...", 2 = transport/local failure.

static const char g_usage[] =
"asvr_ctl - control the ASVR virtual headset\n"
"\n"
"Usage: asvr_ctl <command> [args...]\n"
"       asvr_ctl -              read commands from stdin, one per line\n"
"\n"
"Units: meters and degrees. OpenVR axes: +X right, +Y up, -Z forward.\n"
"Poses are 'x y z qx qy qz qw'. Positive yaw turns right, positive pitch up.\n"
"\n"
"Driver commands (sent over the control pipe):\n"
"  ping                                        liveness check\n"
"  status                                      HMD pose + controller offsets\n"
"  hmd_pose <x y z qx qy qz qw>                absolute HMD pose\n"
"  hmd_rotate <yaw> <pitch> [roll]             relative rotation\n"
"  hmd_translate <x y z> [world|local]         relative move (default local;\n"
"                                              forward = 0 0 -0.5)\n"
"  ctrl <left|right> offset <x y z qx qy qz qw>  head-relative controller pose\n"
"  ctrl <left|right> aim <yaw> <pitch>         aim the controller laser\n"
"  ctrl <left|right> button <name> <press|release|click>\n"
"                                              name: trigger grip system a b x y\n"
"                                              (a/b right hand, x/y left hand)\n"
"  ctrl <left|right> trigger <0..1>            analog trigger\n"
"  ctrl <left|right> joystick <x> <y>          persists until changed\n"
"\n"
"Local commands (read the shared frame mapping):\n"
"  frame                                       frame counter / resolution\n"
"  screenshot <out.png> [--both] [--wait-new-frame] [--adapter N]\n"
"                                              save the HMD view (left eye;\n"
"                                              --both keeps both eyes)\n";

static int RunPipeSession(const std::vector<std::string> &f_lines);
static int RunFrameInfo();

int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        fputs(g_usage, stdout);
        return 2;
    }

    const std::string l_first(argv[1]);
    if(!l_first.compare("--help") || !l_first.compare("-h") || !l_first.compare("help"))
    {
        fputs(g_usage, stdout);
        return 0;
    }
    if(!l_first.compare("screenshot")) return RunScreenshot(argc - 2, argv + 2);
    if(!l_first.compare("frame")) return RunFrameInfo();

    std::vector<std::string> l_lines;
    if(!l_first.compare("-"))
    {
        char l_buffer[1024];
        bool l_firstLine = true;
        while(fgets(l_buffer, sizeof(l_buffer), stdin))
        {
            std::string l_line(l_buffer);
            // PowerShell pipes tend to prepend a UTF-8 BOM
            if(l_firstLine && (l_line.compare(0U, 3U, "\xEF\xBB\xBF") == 0)) l_line.erase(0U, 3U);
            l_firstLine = false;
            while(!l_line.empty() && ((l_line.back() == '\n') || (l_line.back() == '\r'))) l_line.pop_back();
            if(!l_line.empty()) l_lines.push_back(l_line);
        }
    }
    else
    {
        std::string l_line;
        for(int i = 1; i < argc; i++)
        {
            if(i > 1) l_line.push_back(' ');
            l_line.append(argv[i]);
        }
        l_lines.push_back(l_line);
    }
    return RunPipeSession(l_lines);
}

// One overlapped transfer bounded by the wall-clock deadline; false on error or timeout
static bool TransferWithDeadline(HANDLE f_pipe, OVERLAPPED &f_overlapped, bool f_write, void *f_buffer, DWORD f_size, ULONGLONG f_deadline, DWORD &f_transferred)
{
    ResetEvent(f_overlapped.hEvent);
    const BOOL l_started = f_write
        ? WriteFile(f_pipe, f_buffer, f_size, NULL, &f_overlapped)
        : ReadFile(f_pipe, f_buffer, f_size, NULL, &f_overlapped);
    if(!l_started)
    {
        if(GetLastError() != ERROR_IO_PENDING) return false;
        const ULONGLONG l_now = GetTickCount64();
        const DWORD l_wait = (f_deadline > l_now) ? static_cast<DWORD>(f_deadline - l_now) : 0U;
        if(WaitForSingleObject(f_overlapped.hEvent, l_wait) != WAIT_OBJECT_0)
        {
            CancelIo(f_pipe);
            GetOverlappedResult(f_pipe, &f_overlapped, &f_transferred, TRUE);
            return false;
        }
    }
    return (GetOverlappedResult(f_pipe, &f_overlapped, &f_transferred, FALSE) != FALSE);
}

static int RunPipeSession(const std::vector<std::string> &f_lines)
{
    HANDLE l_pipe = INVALID_HANDLE_VALUE;
    for(int l_attempt = 0; l_attempt < 3; l_attempt++)
    {
        l_pipe = CreateFileA(g_controlPipeName, GENERIC_READ | GENERIC_WRITE, 0U, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if(l_pipe != INVALID_HANDLE_VALUE) break;
        const DWORD l_error = GetLastError();
        if(l_error == ERROR_PIPE_BUSY)
        {
            if(!WaitNamedPipeA(g_controlPipeName, 2000U))
            {
                fputs("err: control pipe is busy with another client\n", stderr);
                return 2;
            }
        }
        else if(l_error == ERROR_FILE_NOT_FOUND)
        {
            fputs("err: control pipe not found (is SteamVR running with the ASVR driver?)\n", stderr);
            return 2;
        }
        else
        {
            fprintf(stderr, "err: failed to open the control pipe, error=%lu\n", l_error);
            return 2;
        }
    }
    if(l_pipe == INVALID_HANDLE_VALUE)
    {
        fputs("err: control pipe is busy with another client\n", stderr);
        return 2;
    }

    OVERLAPPED l_overlapped = {};
    l_overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if(l_overlapped.hEvent == NULL)
    {
        CloseHandle(l_pipe);
        fputs("err: failed to create the I/O event\n", stderr);
        return 2;
    }

    int l_result = 0;
    for(const auto &l_line : f_lines)
    {
        // The driver bounds command execution at 5 seconds; stay just above that
        const ULONGLONG l_deadline = GetTickCount64() + 6000ULL;

        std::string l_request = l_line + '\n';
        DWORD l_written = 0U;
        if(!TransferWithDeadline(l_pipe, l_overlapped, true, &l_request[0U], static_cast<DWORD>(l_request.size()), l_deadline, l_written))
        {
            fputs("err: pipe write failed\n", stderr);
            l_result = 2;
            break;
        }

        // Read until the reply newline
        std::string l_reply;
        bool l_complete = false;
        bool l_failed = false;
        while(!l_complete && !l_failed)
        {
            char l_chunk[512];
            DWORD l_read = 0U;
            if(!TransferWithDeadline(l_pipe, l_overlapped, false, l_chunk, sizeof(l_chunk), l_deadline, l_read))
            {
                fputs((GetTickCount64() >= l_deadline) ? "err: reply timeout\n" : "err: pipe read failed\n", stderr);
                l_failed = true;
            }
            else
            {
                l_reply.append(l_chunk, l_read);
                l_complete = (!l_reply.empty() && (l_reply.back() == '\n'));
            }
        }
        if(l_failed)
        {
            l_result = 2;
            break;
        }
        fputs(l_reply.c_str(), stdout);
        if(l_reply.compare(0U, 2U, "ok")) l_result = 1;
    }

    CloseHandle(l_overlapped.hEvent);
    CloseHandle(l_pipe);
    return l_result;
}

static int RunFrameInfo()
{
    HANDLE l_mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, g_sharedFrameMappingName);
    if(l_mapping == NULL)
    {
        fputs("err: frame mapping not found (is SteamVR running with the ASVR driver?)\n", stderr);
        return 2;
    }
    const SharedFrameInfo *l_info = static_cast<const SharedFrameInfo*>(MapViewOfFile(l_mapping, FILE_MAP_READ, 0U, 0U, sizeof(SharedFrameInfo)));
    if(l_info == NULL)
    {
        CloseHandle(l_mapping);
        fputs("err: failed to map the frame info\n", stderr);
        return 2;
    }

    int l_result;
    if(l_info->magic != g_sharedFrameMagic)
    {
        fputs("err: no frame published yet (compositor not presenting?)\n", stderr);
        l_result = 2;
    }
    else
    {
        printf("ok gen=%u frames=%llu %ux%u\n", l_info->generation,
            static_cast<unsigned long long>(l_info->frameCounter), l_info->width, l_info->height);
        l_result = 0;
    }

    UnmapViewOfFile(const_cast<SharedFrameInfo*>(l_info));
    CloseHandle(l_mapping);
    return l_result;
}
