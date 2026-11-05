#include "stdafx.h"

#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "CControlServer.h"
#include "../shared/ControlProtocol.h"

bool CControlServer::Start()
{
    if(m_thread.joinable()) return true;
    m_shutdownFd = eventfd(0U, EFD_CLOEXEC);
    if(m_shutdownFd < 0) return false;
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
    const uint64_t l_signal = 1U;
    (void)!write(m_shutdownFd, &l_signal, sizeof(l_signal));
    m_thread.join();
    close(m_shutdownFd);
    m_shutdownFd = -1;
}

// Returns false only on shutdown or EINTR-retry exhaustion; f_waits[0] is
// always the shutdown eventfd
static bool WaitReadable(pollfd *f_waits, nfds_t f_count)
{
    for(;;)
    {
        if(poll(f_waits, f_count, -1) >= 0) return (f_waits[0].revents == 0);
        if(errno != EINTR) return false;
    }
}

void CControlServer::ThreadMain()
{
    sockaddr_un l_address;
    socklen_t l_addressLength = 0U;
    if(!MakeControlSocketAddress(l_address, l_addressLength)) return;

    const int l_listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(l_listenFd < 0) return;
    if((bind(l_listenFd, reinterpret_cast<sockaddr*>(&l_address), l_addressLength) != 0) || (listen(l_listenFd, 1) != 0))
    {
        vr::IVRDriverLog *l_log = vr::VRDriverLog();
        if(l_log) l_log->Log("CControlServer: failed to bind the control socket (another vrserver running?)");
        close(l_listenFd);
        return;
    }

    bool l_running = true;
    while(l_running)
    {
        pollfd l_waits[2] = { { m_shutdownFd, POLLIN, 0 }, { l_listenFd, POLLIN, 0 } };
        if(!WaitReadable(l_waits, 2U)) break;

        const int l_client = accept4(l_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if(l_client < 0) continue;
        l_running = ServeClient(l_client);
        close(l_client);
    }

    close(l_listenFd);
}

bool CControlServer::ServeClient(int f_client)
{
    std::string l_buffer;
    char l_chunk[512];
    for(;;)
    {
        pollfd l_waits[2] = { { m_shutdownFd, POLLIN, 0 }, { f_client, POLLIN, 0 } };
        if(!WaitReadable(l_waits, 2U)) return false;

        const ssize_t l_read = read(f_client, l_chunk, sizeof(l_chunk));
        if(l_read <= 0) return true; // client gone, accept the next one
        l_buffer.append(l_chunk, static_cast<size_t>(l_read));
        if(l_buffer.size() > (g_controlMaxLineLength * 8U)) return true; // garbage flood, drop the client

        size_t l_lineEnd;
        while((l_lineEnd = l_buffer.find('\n')) != std::string::npos)
        {
            std::string l_line = l_buffer.substr(0U, l_lineEnd);
            l_buffer.erase(0U, l_lineEnd + 1U);
            if(!l_line.empty() && (l_line.back() == '\r')) l_line.pop_back();
            if(l_line.size() > g_controlMaxLineLength) return true;

            const std::string l_reply = QueueAndWait(l_line);
            if(!WriteReply(f_client, l_reply + '\n')) return true;
            {
                std::lock_guard<std::mutex> l_lock(m_queueLock);
                if(m_stopRequested) return false;
            }
        }
    }
}

bool CControlServer::WriteReply(int f_client, const std::string &f_reply)
{
    size_t l_sent = 0U;
    while(l_sent < f_reply.size())
    {
        // MSG_NOSIGNAL: a client that vanished mid-reply must not SIGPIPE vrserver
        const ssize_t l_written = send(f_client, f_reply.c_str() + l_sent, f_reply.size() - l_sent, MSG_NOSIGNAL);
        if(l_written < 0)
        {
            if(errno == EINTR) continue;
            return false;
        }
        l_sent += static_cast<size_t>(l_written);
    }
    return true;
}
