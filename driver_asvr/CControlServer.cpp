#include "stdafx.h"

#include "CControlServer.h"

// Transport-independent half; the pipe/socket thread lives in
// CControlServer_win.cpp / CControlServer_posix.cpp

CControlServer::CControlServer(CommandHandler f_handler)
{
    m_handler = f_handler;
}

CControlServer::~CControlServer()
{
    Stop();
}

void CControlServer::RunPendingCommands()
{
    std::deque<std::shared_ptr<PendingCommand>> l_batch;
    {
        std::lock_guard<std::mutex> l_lock(m_queueLock);
        l_batch.swap(m_pending);
    }
    for(auto &l_command : l_batch)
    {
        const std::string l_reply = m_handler(l_command->m_line);
        {
            std::lock_guard<std::mutex> l_lock(m_queueLock);
            l_command->m_reply = l_reply;
            l_command->m_done = true;
        }
        m_queueCv.notify_all();
    }
}

std::string CControlServer::QueueAndWait(const std::string &f_line)
{
    auto l_command = std::make_shared<PendingCommand>();
    l_command->m_line = f_line;

    std::unique_lock<std::mutex> l_lock(m_queueLock);
    if(m_stopRequested) return "err shutting down";
    m_pending.push_back(l_command);
    // Bounded wait so a client is never hung if vrserver stops calling RunFrame
    const bool l_finished = m_queueCv.wait_for(l_lock, std::chrono::seconds(5),
        [&l_command, this]() { return l_command->m_done || m_stopRequested; });
    if(!l_finished || !l_command->m_done) return "err timeout";
    return l_command->m_reply;
}
