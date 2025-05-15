#pragma once

#include "cli_tool/cli_tool.hpp"
#include "local_api/local_api.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace input_manager {

enum class InputSource {
    NONE,
    CLI,
    API
};

class InputManager : public std::enable_shared_from_this<InputManager> {
public:
    // Constructor and destructor
    InputManager();
    ~InputManager();
    
    // Initialization and control
    bool initialize(const std::string& configPath = "");
    bool start(InputSource source, int apiPort = 8080);
    void stop();
    void runCli();
    void runApiServer(int port = 9000);
    
    // API request processing
    bool processApiRequest(const nlohmann::json& request);
    bool processBatchRequests(const std::vector<nlohmann::json>& requests);
    
    // Queue and confirm API requests
    bool queueApiRequest(const nlohmann::json& request);
    bool confirmQueuedRequests();
    
    // API configuration
    void setAutoConfirm(bool autoConfirm);
    bool getAutoConfirm() const;
    
    // Symbol management
    bool clearSymbol(const std::string& symbol);
    void clearAllInputs();
    
    // Callback registration
    void registerTradeCallback(std::function<void(const nlohmann::json&)> callback);
    void registerErrorCallback(std::function<void(const std::string&, const std::string&)> callback);
    
    // Utility functions
    void setLogLevel(int level);
    void emergencyStop();
    InputSource getActiveSource() const;
    nlohmann::json getStatus() const;
    nlohmann::json getOutput() const;
    nlohmann::json getPendingRequests() const;
    
    // UI functions
    void printBanner() const;
    void printMenu() const;
    void printCurrentInputs() const;
    void clearScreen() const;
    void waitForKeypress() const;
    void printApiHelpInfo(int port) const;
    
private:
    // Input sources
    std::unique_ptr<cli_tool::CliTool> m_cliTool;
    std::unique_ptr<local_api::LocalAPI> m_localApi;
    
    // State
    InputSource m_activeSource;
    nlohmann::json m_config;
    nlohmann::json m_outputJson;
    int m_logLevel;
    
    // Callbacks
    std::function<void(const nlohmann::json&)> m_tradeCallback;
    std::function<void(const std::string&, const std::string&)> m_errorCallback;
    
    // Helper functions
    void processOutput();
    void logMessage(int level, const std::string& message) const;
    bool parseConfig(const std::string& configPath);
};

} // namespace input_manager