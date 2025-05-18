#pragma once

#include <string>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>
#include <memory>
#include <thread>
#include <atomic>

// Forward declaration
namespace input_manager {
    class InputManager;
}

namespace model_manager {
    class ModelManager;
}

namespace local_api {

class LocalAPI {
public:
    // Constructor and destructor
    LocalAPI();
    ~LocalAPI();
    
    // Set the input manager reference
    void setInputManager(std::shared_ptr<input_manager::InputManager> inputManager);
    
    // Initialize the API
    bool initialize(const std::string& configPath = "");
    
    // Start/stop the API server
    bool start(int port = 9000);
    void stop();
    
    // Process trading requests (direct processing, backward compatibility)
    bool processTradingRequest(const nlohmann::json& request);
    bool processBatchRequests(const std::vector<nlohmann::json>& requests);
    
    // Queue and confirm trading requests
    bool queueTradingRequest(const nlohmann::json& request);
    bool confirmQueuedRequests();
    
    // Symbol management
    
    // Clears a specific symbol from queues and stops its associated thread
    // Directly accesses AppState to manage threads (NOT via InputManager)
    bool clearSymbol(const std::string& symbol);
    
    // Clears all symbols from queues and stops all active threads
    // Directly accesses AppState to manage threads (NOT via InputManager)
    void clearAllRequests();
    
    // Status and information
    nlohmann::json getStatus() const;
    nlohmann::json getRequestStatus(const std::string& requestId) const;
    nlohmann::json getFormattedRequests() const;
    nlohmann::json getPendingRequests() const;
    
    // Configuration
    void setAutoConfirm(bool autoConfirm);
    bool getAutoConfirm() const;
    
    // Callback registration
    void registerTradeCallback(std::function<void(const nlohmann::json&)> callback);
    void registerErrorCallback(std::function<void(const std::string&, const std::string&)> callback);
    
    // Utility functions
    void setLogLevel(int level);
    void emergencyStop();
    
private:
    // HTTP server functionality
    void startHttpServer(int port);
    void stopHttpServer();
    void httpServerThread();
    void handleHttpRequest(int clientSocket);
    std::string processHttpRequest(const std::string& request);
    std::string buildHttpResponse(int statusCode, const std::string& responseBody, const std::string& contentType);
    
    // Request processing
    bool validateRequest(const nlohmann::json& request) const;
    bool executeRequest(const nlohmann::json& request);
    
    // Utility functions
    void logMessage(int level, const std::string& message) const;
    bool parseConfig(const std::string& configPath);
    void notifyRequestQueueChanged();
    
    // Data members
    std::weak_ptr<input_manager::InputManager> m_inputManager;
    nlohmann::json m_config;
    std::vector<nlohmann::json> m_requestQueue;
    std::vector<nlohmann::json> m_pendingQueue;
    bool m_isRunning;
    int m_logLevel;
    bool m_autoConfirm;
    
    // Connection status tracking
    nlohmann::json m_lastConnectionResults;
    bool m_hasConnectionIssues;
    
    // Callbacks
    std::function<void(const nlohmann::json&)> m_tradeCallback;
    std::function<void(const std::string&, const std::string&)> m_errorCallback;
    
    // HTTP server
    std::thread m_serverThread;
    std::atomic<bool> m_serverRunning;
    int m_serverSocket;
    int m_serverPort;
};

} // namespace local_api