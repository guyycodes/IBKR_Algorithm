// ───────────────────────────────────────────────────────────────────────────────
//  connection_manager.cpp
// ───────────────────────────────────────────────────────────────────────────────

#include "connection_manager.hpp"
#include <string>

namespace connection_manager {

bool ConnectionManager::connect(int               clientId,
                                std::string_view symbol,
                                const Contract&   c)
{
    if (m_connected.load()) return true;

    m_trader = std::make_unique<connection::IBKRTrader>();

    if (!m_trader->connect(clientId, std::string(symbol), c)) return false;

    m_reader = m_trader->createReader();

    // register a jthread inside AppState so we inherit watchdog-logic etc.
    m_threadName = "IBKR-READER-" + std::string(symbol);
    auto& as     = app_state::AppState::getInstance();

    as.startThread(
        m_threadName,
        [this](const app_state::StopToken& tok) { readerLoop(tok); });

    m_connected.store(true);
    return true;
}

void ConnectionManager::disconnect()
{
    if (!m_connected.exchange(false)) return;

    m_trader->disconnect();
    if (!m_threadName.empty())
        app_state::AppState::getInstance().requestThreadStop(m_threadName,
                                                             "ConnectionManager");
}

void ConnectionManager::readerLoop(const app_state::StopToken& tok)
{
    while (!tok.stop_requested())
    {
        if (m_reader && m_trader && m_trader->isConnected()) {
            // Simply process messages - processMsgs() handles empty queue
            m_reader->processMsgs();
        }
        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}
} // namespace connection_manager
