#include "stdafx.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "PipeClient.h"
#include "../shared/ControlProtocol.h"
#include "../shared/SharedFrameInfo.h"

static uint64_t GetMilliseconds()
{
    timespec l_time;
    clock_gettime(CLOCK_MONOTONIC, &l_time);
    return (static_cast<uint64_t>(l_time.tv_sec) * 1000ULL) + (static_cast<uint64_t>(l_time.tv_nsec) / 1000000ULL);
}

// One transfer bounded by the wall-clock deadline; false on error or timeout
static bool TransferWithDeadline(int f_socket, bool f_write, void *f_buffer, size_t f_size, uint64_t f_deadline, size_t &f_transferred)
{
    f_transferred = 0U;
    for(;;)
    {
        const uint64_t l_now = GetMilliseconds();
        if(l_now >= f_deadline) return false;
        pollfd l_wait = { f_socket, static_cast<short>(f_write ? POLLOUT : POLLIN), 0 };
        const int l_ready = poll(&l_wait, 1U, static_cast<int>(f_deadline - l_now));
        if(l_ready < 0)
        {
            if(errno == EINTR) continue;
            return false;
        }
        if(l_ready == 0) return false; // timeout

        const ssize_t l_moved = f_write
            ? send(f_socket, f_buffer, f_size, MSG_NOSIGNAL)
            : read(f_socket, f_buffer, f_size);
        if(l_moved < 0)
        {
            if(errno == EINTR) continue;
            return false;
        }
        if(!f_write && (l_moved == 0)) return false; // server gone
        f_transferred = static_cast<size_t>(l_moved);
        // A short write never happens on a blocking unix socket at these sizes
        return true;
    }
}

int RunPipeSession(const std::vector<std::string> &f_lines)
{
    sockaddr_un l_address;
    socklen_t l_addressLength = 0U;
    if(!MakeControlSocketAddress(l_address, l_addressLength))
    {
        fputs("err: control socket name too long\n", stderr);
        return 2;
    }

    int l_socket = -1;
    for(int l_attempt = 0; l_attempt < 3; l_attempt++)
    {
        l_socket = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if(l_socket < 0)
        {
            fputs("err: failed to create the control socket\n", stderr);
            return 2;
        }
        if(connect(l_socket, reinterpret_cast<sockaddr*>(&l_address), l_addressLength) == 0) break;
        const int l_error = errno;
        close(l_socket);
        l_socket = -1;
        if((l_error == ENOENT) || (l_error == ECONNREFUSED))
        {
            fputs("err: control socket not found (is SteamVR running with the ASVR driver?)\n", stderr);
            return 2;
        }
        // The single-client server may be between accepts; brief retry
        usleep(200000U);
    }
    if(l_socket < 0)
    {
        fputs("err: control socket is busy with another client\n", stderr);
        return 2;
    }

    int l_result = 0;
    for(const auto &l_line : f_lines)
    {
        // The driver bounds command execution at 5 seconds; stay just above that
        const uint64_t l_deadline = GetMilliseconds() + 6000ULL;

        std::string l_request = l_line + '\n';
        size_t l_written = 0U;
        if(!TransferWithDeadline(l_socket, true, &l_request[0U], l_request.size(), l_deadline, l_written))
        {
            fputs("err: socket write failed\n", stderr);
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
            size_t l_read = 0U;
            if(!TransferWithDeadline(l_socket, false, l_chunk, sizeof(l_chunk), l_deadline, l_read))
            {
                fputs((GetMilliseconds() >= l_deadline) ? "err: reply timeout\n" : "err: socket read failed\n", stderr);
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

    close(l_socket);
    return l_result;
}

int RunFrameInfo()
{
    const int l_fd = shm_open(g_sharedFramePixelsName, O_RDONLY, 0);
    if(l_fd < 0)
    {
        fputs("err: frame block not found (is SteamVR running with the ASVR driver?)\n", stderr);
        return 2;
    }
    const SharedFramePixelsInfo *l_info = static_cast<const SharedFramePixelsInfo*>(
        mmap(nullptr, sizeof(SharedFramePixelsInfo), PROT_READ, MAP_SHARED, l_fd, 0));
    close(l_fd);
    if(l_info == MAP_FAILED)
    {
        fputs("err: failed to map the frame info\n", stderr);
        return 2;
    }

    int l_result;
    if(l_info->magic != g_sharedFramePixelsMagic)
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

    munmap(const_cast<SharedFramePixelsInfo*>(l_info), sizeof(SharedFramePixelsInfo));
    return l_result;
}
