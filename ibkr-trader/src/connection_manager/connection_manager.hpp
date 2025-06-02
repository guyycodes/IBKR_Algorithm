//  connection_manager.hpp
// ───────────────────────────────────────────────────────────────────────────────

#pragma once
//  Thin RAII façade around IBKR's EClient/EReader pair
//  ────────────────────────────────────────────────────────────
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include "connection/connection.hpp"          // IBKRTrader wrapper
#include "../../util/app_state.hpp"

namespace connection_manager {

class ConnectionManager {
public:
    ConnectionManager()  = default;
    ~ConnectionManager() { disconnect(); }

    ConnectionManager(const ConnectionManager&)            = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager(ConnectionManager&&)                 = default;
    ConnectionManager& operator=(ConnectionManager&&)      = default;

    [[nodiscard]] bool connect(int               clientId,
                               std::string_view symbol,
                               const Contract&   contract);

    void disconnect();

    [[nodiscard]] bool          isConnected() const { return m_connected.load(); }
    [[nodiscard]] connection::IBKRTrader&   trader()            { return *m_trader; }

private:
    // worker that pumps IBKR messages → trader callbacks
    void readerLoop(const app_state::StopToken& tok);

    std::unique_ptr<connection::IBKRTrader> m_trader;
    std::unique_ptr<EReader>    m_reader;
    std::atomic<bool>           m_connected{false};

    // owned by AppState – NOT a raw std::thread
    std::string                 m_threadName;
};
} // namespace connection_manager
