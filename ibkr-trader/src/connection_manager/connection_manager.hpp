#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include "connection/connection.hpp"
#include "api_functions/api_functions.hpp"
#include <memory>
#include <thread>
#include <string>

// Forward declaration instead of direct include to avoid circular dependencies
namespace app_state {
    class AppState;
}

namespace connection_manager {

    class ConnectionManager {
    private:
        std::unique_ptr<connection::IBKRTrader> m_trader;
        std::unique_ptr<ibkr_api_functions::API_Functions> m_api_functions;
        std::unique_ptr<EReader> m_reader;
        std::thread m_msgProcessingThread;
        bool m_connected;
        std::string m_threadId; // Unique ID for this connection's thread

    public:
        // Constructor and destructor
        ConnectionManager();
        ~ConnectionManager();

        // Connect with an optional client ID
        bool connect(int clientId = -1);
        void disconnect();
        bool isConnected() const { return m_connected; }

        // Get the trader for direct access
        connection::IBKRTrader& getTrader() const { return *m_trader; }
        
        // Get the api functions for making requests
        ibkr_api_functions::API_Functions& getApiFunctions() const { return *m_api_functions; }
        
        // Set thread ID for tracking by AppState
        void setThreadId(const std::string& threadId) { m_threadId = threadId; }
        std::string getThreadId() const { return m_threadId; }
    };

} // namespace connection_manager

#endif // CONNECTION_MANAGER_HPP