#pragma once

// Named pipe server for the external control channel (see shared/ControlProtocol.h).
// A worker thread owns the pipe and blocks on I/O; received command lines are
// queued and executed by RunPendingCommands() on the vrserver thread, so the
// handler may touch device state freely. One client at a time, served serially.
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
    bool ServeClient(HANDLE f_pipe, OVERLAPPED &f_overlapped); // false = shutdown requested
    std::string QueueAndWait(const std::string &f_line);
    bool WriteReply(HANDLE f_pipe, OVERLAPPED &f_overlapped, const std::string &f_reply);

    CommandHandler m_handler;
    std::thread m_thread;
    HANDLE m_shutdownEvent = NULL;

    // shared_ptr: a command abandoned on timeout may still be executed later by
    // RunPendingCommands after the pipe thread has moved on
    std::mutex m_queueLock;
    std::condition_variable m_queueCv;
    std::deque<std::shared_ptr<PendingCommand>> m_pending;
    bool m_stopRequested = false;
};
