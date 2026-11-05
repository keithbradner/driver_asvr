#include "stdafx.h"

#include "CControlServer.h"
#include "../shared/ControlProtocol.h"

bool CControlServer::Start()
{
    if(m_thread.joinable()) return true;
    m_shutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if(m_shutdownEvent == NULL) return false;
    m_stopRequested = false;
    m_thread = std::thread(&CControlServer::ThreadMain, this);
    return true;
}

void CControlServer::Stop()
{
    if(!m_thread.joinable()) return;
    {
        std::lock_guard<std::mutex> l_lock(m_queueLock);
        m_stopRequested = true;
    }
    m_queueCv.notify_all();
    SetEvent(m_shutdownEvent);
    m_thread.join();
    CloseHandle(m_shutdownEvent);
    m_shutdownEvent = NULL;
}

void CControlServer::ThreadMain()
{
    OVERLAPPED l_overlapped = {};
    l_overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if(l_overlapped.hEvent == NULL) return;

    bool l_running = true;
    while(l_running)
    {
        HANDLE l_pipe = CreateNamedPipeA(g_controlPipeName,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1U, 4096U, 4096U, 0U, NULL);
        if(l_pipe == INVALID_HANDLE_VALUE) break;

        ResetEvent(l_overlapped.hEvent);
        bool l_connected = (ConnectNamedPipe(l_pipe, &l_overlapped) != FALSE);
        if(!l_connected)
        {
            const DWORD l_error = GetLastError();
            if(l_error == ERROR_IO_PENDING)
            {
                HANDLE l_waits[2] = { m_shutdownEvent, l_overlapped.hEvent };
                if(WaitForMultipleObjects(2U, l_waits, FALSE, INFINITE) == WAIT_OBJECT_0)
                {
                    // Cancellation is asynchronous; drain it before the buffers go away
                    CancelIo(l_pipe);
                    DWORD l_ignored = 0U;
                    GetOverlappedResult(l_pipe, &l_overlapped, &l_ignored, TRUE);
                    l_running = false;
                }
                else l_connected = true;
            }
            else if(l_error == ERROR_PIPE_CONNECTED) l_connected = true;
        }

        if(l_connected) l_running = ServeClient(l_pipe, l_overlapped);

        DisconnectNamedPipe(l_pipe);
        CloseHandle(l_pipe);
    }

    CloseHandle(l_overlapped.hEvent);
}

bool CControlServer::ServeClient(HANDLE f_pipe, OVERLAPPED &f_overlapped)
{
    std::string l_buffer;
    char l_chunk[512];
    for(;;)
    {
        ResetEvent(f_overlapped.hEvent);
        DWORD l_read = 0U;
        if(!ReadFile(f_pipe, l_chunk, sizeof(l_chunk), NULL, &f_overlapped))
        {
            if(GetLastError() != ERROR_IO_PENDING) return true; // client gone, accept the next one
            HANDLE l_waits[2] = { m_shutdownEvent, f_overlapped.hEvent };
            if(WaitForMultipleObjects(2U, l_waits, FALSE, INFINITE) == WAIT_OBJECT_0)
            {
                CancelIo(f_pipe);
                GetOverlappedResult(f_pipe, &f_overlapped, &l_read, TRUE);
                return false;
            }
        }
        if(!GetOverlappedResult(f_pipe, &f_overlapped, &l_read, FALSE)) return true;
        l_buffer.append(l_chunk, l_read);
        if(l_buffer.size() > (g_controlMaxLineLength * 8U)) return true; // garbage flood, drop the client

        size_t l_lineEnd;
        while((l_lineEnd = l_buffer.find('\n')) != std::string::npos)
        {
            std::string l_line = l_buffer.substr(0U, l_lineEnd);
            l_buffer.erase(0U, l_lineEnd + 1U);
            if(!l_line.empty() && (l_line.back() == '\r')) l_line.pop_back();
            if(l_line.size() > g_controlMaxLineLength) return true;

            const std::string l_reply = QueueAndWait(l_line);
            if(!WriteReply(f_pipe, f_overlapped, l_reply + '\n')) return true;
            {
                std::lock_guard<std::mutex> l_lock(m_queueLock);
                if(m_stopRequested) return false;
            }
        }
    }
}

bool CControlServer::WriteReply(HANDLE f_pipe, OVERLAPPED &f_overlapped, const std::string &f_reply)
{
    ResetEvent(f_overlapped.hEvent);
    DWORD l_written = 0U;
    if(!WriteFile(f_pipe, f_reply.c_str(), static_cast<DWORD>(f_reply.size()), NULL, &f_overlapped))
    {
        if(GetLastError() != ERROR_IO_PENDING) return false;
        HANDLE l_waits[2] = { m_shutdownEvent, f_overlapped.hEvent };
        if(WaitForMultipleObjects(2U, l_waits, FALSE, INFINITE) == WAIT_OBJECT_0)
        {
            CancelIo(f_pipe);
            GetOverlappedResult(f_pipe, &f_overlapped, &l_written, TRUE);
            return false;
        }
    }
    return (GetOverlappedResult(f_pipe, &f_overlapped, &l_written, FALSE) != FALSE);
}
