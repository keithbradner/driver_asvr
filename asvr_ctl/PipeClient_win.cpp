#include "stdafx.h"

#include "PipeClient.h"
#include "../shared/ControlProtocol.h"
#include "../shared/SharedFrameInfo.h"

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

int RunPipeSession(const std::vector<std::string> &f_lines)
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

int RunFrameInfo()
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
