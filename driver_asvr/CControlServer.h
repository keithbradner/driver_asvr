#pragma once

// Server for the external control channel (see shared/ControlProtocol.h) -
// a named pipe on Windows, a unix domain socket on Linux. A worker thread owns
// the transport and blocks on I/O; received command lines are queued and
// executed by RunPendingCommands() on the vrserver thread, so the handler may
// touch device state freely. One client at a time, served serially.
class CControlServer final
{
public:
    typedef std::function<std::string(const std::string&)> CommandHandler; // returns the reply line, without '\n'

    explicit CControlServer(CommandHandler f_handler);
    ~CControlServer();

    bool Start();
    void Stop();

    void RunPendingCommands(); // vrserver thread only

private:
    struct PendingCommand
    {
        std::string m_line;
        std::string m_reply;
        bool m_done = false;
    };

    void ThreadMain();
    std::string QueueAndWait(const std::string &f_line);
#ifdef _WIN32
    bool ServeClient(HANDLE f_pipe, OVERLAPPED &f_overlapped); // false = shutdown requested
    bool WriteReply(HANDLE f_pipe, OVERLAPPED &f_overlapped, const std::string &f_reply);
#else
    bool ServeClient(int f_client); // false = shutdown requested
    bool WriteReply(int f_client, const std::string &f_reply);
#endif

    CommandHandler m_handler;
    std::thread m_thread;
#ifdef _WIN32
    HANDLE m_shutdownEvent = NULL;
#else
    int m_shutdownFd = -1; // eventfd, signalled once by Stop()
#endif

    // shared_ptr: a command abandoned on timeout may still be executed later by
    // RunPendingCommands after the transport thread has moved on
    std::mutex m_queueLock;
    std::condition_variable m_queueCv;
    std::deque<std::shared_ptr<PendingCommand>> m_pending;
    bool m_stopRequested = false;
};
