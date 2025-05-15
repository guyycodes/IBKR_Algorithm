#include "input_manager.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>

namespace input_manager {

// Constructor
InputManager::InputManager()
    : m_activeSource(InputSource::NONE),
      m_logLevel(0) {
    // Create instances of CLI tool and Local API
    m_cliTool = std::make_unique<cli_tool::CliTool>();
    m_localApi = std::make_unique<local_api::LocalAPI>();
    
    // Initialize empty JSON objects
    m_config = nlohmann::json::object();
    m_outputJson = nlohmann::json::object();
    
    // Set default callbacks to do nothing
    m_tradeCallback = [](const nlohmann::json&) {};
    m_errorCallback = [](const std::string&, const std::string&) {};
    
    logMessage(0, "InputManager instance created");
}

// Destructor
InputManager::~InputManager() {
    // Make sure to stop before destroying
    stop();
    
    logMessage(0, "InputManager instance destroyed");
}

// Initialize the input manager
bool InputManager::initialize(const std::string& configPath) {
    logMessage(1, "Initializing InputManager");
    
    // If a config path is provided, parse it
    if (!configPath.empty()) {
        if (!parseConfig(configPath)) {
            logMessage(2, "Failed to parse configuration file: " + configPath);
            return false;
        }
    }
    
    // Set up bidirectional references
    m_cliTool->setInputManager(shared_from_this());
    m_localApi->setInputManager(shared_from_this());
    
    // Set up the submit callback for CliTool
    m_cliTool->setSubmitCallback([this](const nlohmann::json& inputs) {
        // Update our output JSON with the CLI inputs directly
        m_outputJson = inputs;
        
        // Process the output (this is the common bottleneck function for both CLI and API)
        processOutput();
    });
    
    // Register callbacks with LocalAPI to update our output JSON
    m_localApi->registerTradeCallback([this](const nlohmann::json& tradeData) {
        // Update our output JSON with the API inputs
        m_outputJson = tradeData;
        
        // Process through the same bottleneck
        processOutput();
    });
    
    m_localApi->registerErrorCallback([this](const std::string& errorCode, const std::string& errorMessage) {
        // Forward error to our registered callback
        m_errorCallback(errorCode, errorMessage);
    });
    
    // Initialize the Local API
    if (!m_localApi->initialize(configPath)) {
        logMessage(2, "Failed to initialize Local API");
        return false;
    }
    
    logMessage(0, "InputManager initialized successfully");
    return true;
}

// Start the input manager with specified source
bool InputManager::start(InputSource source, int apiPort) {
    logMessage(0, std::string("Starting InputManager with source: ") + 
        (source == InputSource::CLI ? "CLI" : "API"));
    
    // Stop if already running
    if (m_activeSource != InputSource::NONE) {
        stop();
    }
    
    m_activeSource = source;
    
    // Start the appropriate input source
    if (source == InputSource::API) {
        if (!m_localApi->start(apiPort)) {
            logMessage(2, "Failed to start Local API");
            m_activeSource = InputSource::NONE;
            return false;
        }
    }
    
    logMessage(0, "InputManager started successfully");
    return true;
}

// Stop the input manager
void InputManager::stop() {
    logMessage(0, "Stopping InputManager");
    
    // Stop the active input source
    if (m_activeSource == InputSource::API) {
        m_localApi->stop();
    }
    
    m_activeSource = InputSource::NONE;
    
    logMessage(0, "InputManager stopped");
}

// Run the CLI tool (blocking)
void InputManager::runCli() {
    logMessage(0, "Starting CLI tool");
    
    // Set the active source to CLI
    m_activeSource = InputSource::CLI;
    
    // Run the CLI tool (this is a blocking call)
    m_cliTool->run();
    
    // When CLI exits, set active source to NONE
    m_activeSource = InputSource::NONE;
    
    logMessage(0, "CLI tool exited");
}

// Process a trading request via API
bool InputManager::processApiRequest(const nlohmann::json& request) {
    if (m_activeSource != InputSource::API) {
        logMessage(2, "Cannot process API request: API not active");
        return false;
    }
    
    logMessage(1, "Processing API request");
    
    // Process the request using LocalAPI
    bool success = m_localApi->processTradingRequest(request);
    
    if (success) {
        // Update the output JSON
        std::string symbol = request["symbol"];
        std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
        m_outputJson[symbol] = request["params"];
        
        // Process the output
        processOutput();
    }
    
    return success;
}

// Process batch requests via API
bool InputManager::processBatchRequests(const std::vector<nlohmann::json>& requests) {
    if (m_activeSource != InputSource::API) {
        logMessage(2, "Cannot process batch requests: API not active");
        return false;
    }
    
    logMessage(1, "Processing batch of " + std::to_string(requests.size()) + " API requests");
    
    // Process each request
    bool allSuccessful = true;
    for (const auto& request : requests) {
        if (!processApiRequest(request)) {
            allSuccessful = false;
        }
    }
    
    return allSuccessful;
}

// Clear a symbol from both CLI and API
bool InputManager::clearSymbol(const std::string& symbol) {
    logMessage(1, "Clearing symbol: " + symbol);
    
    bool apiResult = true;
    if (m_activeSource == InputSource::API) {
        apiResult = m_localApi->clearSymbol(symbol);
    }
    
    // Remove from output JSON
    std::string upperSymbol = symbol;
    std::transform(upperSymbol.begin(), upperSymbol.end(), upperSymbol.begin(), ::toupper);
    
    bool jsonResult = false;
    if (m_outputJson.contains(upperSymbol)) {
        m_outputJson.erase(upperSymbol);
        jsonResult = true;
    }
    
    return apiResult || jsonResult;
}

// Clear all inputs from both CLI and API
void InputManager::clearAllInputs() {
    logMessage(0, "Clearing all inputs");
    
    if (m_activeSource == InputSource::API) {
        m_localApi->clearAllRequests();
    }
    
    // Clear output JSON
    m_outputJson.clear();
}

// Register callbacks for events
void InputManager::registerTradeCallback(std::function<void(const nlohmann::json&)> callback) {
    logMessage(1, "Registering trade callback");
    
    m_tradeCallback = callback;
    
    // Register with LocalAPI as well
    m_localApi->registerTradeCallback(callback);
}

void InputManager::registerErrorCallback(std::function<void(const std::string&, const std::string&)> callback) {
    logMessage(1, "Registering error callback");
    
    m_errorCallback = callback;
    
    // Register with LocalAPI as well
    m_localApi->registerErrorCallback(callback);
}

// Set logging level
void InputManager::setLogLevel(int level) {
    logMessage(0, "Setting log level to " + std::to_string(level));
    
    m_logLevel = level;
    
    // Set in LocalAPI as well
    m_localApi->setLogLevel(level);
}

// Emergency stop
void InputManager::emergencyStop() {
    logMessage(0, "EMERGENCY STOP TRIGGERED");
    
    // Stop both input sources
    stop();
    
    // Clear all inputs
    clearAllInputs();
    
    // Call the emergency stop on LocalAPI
    m_localApi->emergencyStop();
    
    // Call error callback
    m_errorCallback("emergency_stop", "Emergency stop triggered");
    
    logMessage(0, "Emergency stop completed");
}

// Get the current active input source
InputSource InputManager::getActiveSource() const {
    return m_activeSource;
}

// Get the status of the input manager
nlohmann::json InputManager::getStatus() const {
    nlohmann::json status;
    
    status["active_source"] = (m_activeSource == InputSource::CLI) ? "CLI" : 
                             (m_activeSource == InputSource::API) ? "API" : "NONE";
    
    if (m_activeSource == InputSource::API) {
        status["api_status"] = m_localApi->getStatus();
    }
    
    status["output_json_size"] = m_outputJson.size();
    
    return status;
}

// Get the final JSON output
nlohmann::json InputManager::getOutput() const {
    return m_outputJson;
}

// Print banner
void InputManager::printBanner() const {
    std::cout << "=======================================" << std::endl;
    std::cout << "       IBKR Scalping Tool v1.0        " << std::endl;
    std::cout << "=======================================" << std::endl;
}

// Clear the screen
void InputManager::clearScreen() const {
    // Use a system-independent approach
    std::cout << std::string(50, '\n');
}

// Print the menu
void InputManager::printMenu() const {
    std::cout << "\n=======================================" << std::endl;
    std::cout << "              MAIN MENU              " << std::endl;
    std::cout << "=======================================" << std::endl;
    std::cout << "1. Add Trading Symbol and Parameters" << std::endl;
    std::cout << "2. Clear Symbol Input" << std::endl;
    std::cout << "3. Submit All Inputs" << std::endl;
    std::cout << "4. Clear Screen" << std::endl;
    std::cout << "9. Emergency Exit" << std::endl;
    std::cout << "0. Exit" << std::endl;
    std::cout << "=======================================" << std::endl;
}

// Print current inputs
void InputManager::printCurrentInputs() const {
    if (m_outputJson.empty()) {
        std::cout << "\nNo inputs currently stored." << std::endl;
    } else {
        std::cout << "\nCurrent Inputs (" << m_outputJson.size() << "/10):" << std::endl;
        for (auto it = m_outputJson.begin(); it != m_outputJson.end(); ++it) {
            std::string symbol = it.key();
            const auto& entry = it.value();
            
            std::cout << symbol << ": [";
            
            // Check if this is the new nested format with params inside
            if (entry.contains("params")) {
                // New format: m_outputJson[symbol]["params"][param_name]
                const auto& params = entry["params"];
                std::cout << "lots:" << params["lots"] << ", ";
                std::cout << "margin: \"" << params["margin"] << "\", ";
                std::cout << "stopLoss: \"" << params["stopLoss"] << "\", ";
                std::cout << "maxTrades: " << params["maxTrades"] << ", ";
                std::cout << "lossThreshold: " << params["lossThreshold"] << ", ";
                std::cout << "winThreshold: " << params["winThreshold"] << ", ";
                std::cout << "minWinRate: \"" << params["minWinRate"] << "\", ";
                std::cout << "maxHoldSeconds: " << params["maxHoldSeconds"] << "]" << std::endl;
                
                // Print status and timestamp if available
                if (entry.contains("status")) {
                    std::cout << "   Status: " << entry["status"];
                }
                if (entry.contains("timestamp")) {
                    std::cout << ", Timestamp: " << entry["timestamp"];
                }
                std::cout << std::endl;
            } else {
                // Old format: m_outputJson[symbol][param_name]
                // This is for backward compatibility
                std::cout << "lots:" << entry["lots"] << ", ";
                std::cout << "margin: \"" << entry["margin"] << "\", ";
                std::cout << "stopLoss: \"" << entry["stopLoss"] << "\", ";
                std::cout << "maxTrades: " << entry["maxTrades"] << ", ";
                std::cout << "lossThreshold: " << entry["lossThreshold"] << ", ";
                std::cout << "winThreshold: " << entry["winThreshold"] << ", ";
                std::cout << "minWinRate: \"" << entry["minWinRate"] << "\", ";
                std::cout << "maxHoldSeconds: " << entry["maxHoldSeconds"] << "]" << std::endl;
            }
        }
    }
}

// Wait for keypress
void InputManager::waitForKeypress() const {
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
}

// *****************************************************************************
// *                                                                           *
// *                      INPUT MANAGER BOTTLENECK                             *
// *                                                                           *
// * This is the central bottleneck where all trading data from both CLI       *
// * and API inputs must pass through. All data is standardized to the same    *
// * format regardless of its source.                                          *
// *                                                                           *
// * DATA STRUCTURE:                                                           *
// * {                                                                         *
// *   "SYMBOL": {                 // Stock symbol (e.g., "AAPL")              *
// *     "params": {               // Trading parameters                       *
// *       "lots": 5,              // Integer                                  *
// *       "margin": ".10",        // String                                   *
// *       "stopLoss": ".05",      // String                                   *
// *       "maxTrades": 10,        // Integer                                  *
// *       "lossThreshold": 3,     // Integer                                  *
// *       "winThreshold": 5,      // Integer                                  *
// *       "minWinRate": ".50",    // String                                   *
// *       "maxHoldSeconds": 3600  // Integer                                  *
// *     },                                                                    *
// *     "status": "executed",     // Status of the request                    *
// *     "symbol": "SYMBOL",       // Duplicate of the key for convenience     *
// *     "timestamp": 12345678901  // Unix timestamp when processed            *
// *   }                                                                       *
// * }                                                                         *
// *                                                                           *
// *****************************************************************************
// Process the internal output JSON
void InputManager::processOutput() {
    // Create a standardized output format
    nlohmann::json standardizedOutput;
    
    // Log the source of the input
    if (m_activeSource == InputSource::CLI) {
        std::cout << "\n===== INPUT FROM CLI =====" << std::endl;
        
        // For CLI input, we need to transform the flat format to the nested format with status/timestamp
        for (auto it = m_outputJson.begin(); it != m_outputJson.end(); ++it) {
            std::string symbol = it.key();
            nlohmann::json params = it.value();
            
            // Create a standardized request format that matches API output
            nlohmann::json standardizedRequest = {
                {"symbol", symbol},
                {"params", params},
                {"status", "executed"},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
            };
            
            // Add to our standardized output
            standardizedOutput[symbol] = standardizedRequest;
        }
    } else if (m_activeSource == InputSource::API) {
        std::cout << "\n===== INPUT FROM API =====" << std::endl;
        
        // API input may already be in the correct format, but ensure consistency
        for (auto it = m_outputJson.begin(); it != m_outputJson.end(); ++it) {
            std::string symbol = it.key();
            nlohmann::json params = it.value();
            
            // The API might provide input in various formats, standardize it
            nlohmann::json standardizedRequest;
            
            // Check if this is already in the expected format with status/timestamp
            if (params.contains("status")) {
                standardizedRequest = params;
            } else {
                // Create a standardized request format
                standardizedRequest = {
                    {"symbol", symbol},
                    {"params", params},
                    {"status", "executed"},
                    {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
                };
            }
            
            // Add to our standardized output
            standardizedOutput[symbol] = standardizedRequest;
        }
    } else {
        std::cout << "\n===== INPUT SOURCE UNKNOWN =====" << std::endl;
    }
    
    // Print the full JSON structure for analysis
    std::cout << "JSON Structure:" << std::endl;
    std::cout << standardizedOutput.dump(2) << std::endl;
    std::cout << "======================" << std::endl;
    
    // Log the output processing
    logMessage(0, "Processing output JSON with " + std::to_string(standardizedOutput.size()) + " symbols");
    
    // Save the standardized format back to m_outputJson
    m_outputJson = standardizedOutput;
    
    // Validate and normalize outputs if needed
    for (auto it = m_outputJson.begin(); it != m_outputJson.end(); ++it) {
        std::string symbol = it.key();
        
        // Example: log the trading parameters for each symbol
        logMessage(1, "Symbol " + symbol + " request processed");
    }
    
    // Call the trade callback with the updated output
    if (m_tradeCallback) {
        logMessage(0, "Executing trade callback with processed output");
        m_tradeCallback(m_outputJson);
    }
}
// *****************************************************************************
// *                         END OF BOTTLENECK                                 *
// *****************************************************************************

// Log a message
void InputManager::logMessage(int level, const std::string& message) const {
    // Only log if the message level is <= the current log level
    if (level <= m_logLevel) {
        std::string prefix;
        
        switch (level) {
            case 0: prefix = "INFO"; break;
            case 1: prefix = "DEBUG"; break;
            case 2: prefix = "ERROR"; break;
            default: prefix = "LOG"; break;
        }
        
        std::cout << "[InputManager] [" << prefix << "] " << message << std::endl;
    }
}

// Parse configuration file
bool InputManager::parseConfig(const std::string& configPath) {
    try {
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            logMessage(2, "Failed to open config file: " + configPath);
            return false;
        }
        
        configFile >> m_config;
        configFile.close();
        
        logMessage(0, "Configuration loaded successfully");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(2, "Error parsing config: " + std::string(e.what()));
        return false;
    }
}

// Run the API server (blocking)
void InputManager::runApiServer(int port) {
    logMessage(0, "Starting API server on port " + std::to_string(port));
    
    // Start in API mode
    if (!start(InputSource::API, port)) {
        logMessage(2, "Failed to start API server");
        return;
    }
    
    // Print help information
    clearScreen();
    printBanner();
    printApiHelpInfo(port);
    
    // Wait for user to stop the server
    std::cout << "\nAPI server is running. Press Enter to stop the server..." << std::endl;
    std::cin.get();
    
    // Stop the server
    stop();
    
    logMessage(0, "API server stopped");
}

// Queue a trading request via API
bool InputManager::queueApiRequest(const nlohmann::json& request) {
    if (m_activeSource != InputSource::API) {
        logMessage(2, "Cannot queue API request: API not active");
        return false;
    }
    
    logMessage(1, "Queueing API request for symbol: " + request["symbol"].get<std::string>());
    
    // Queue the request using LocalAPI
    return m_localApi->queueTradingRequest(request);
}

// Confirm all queued requests
bool InputManager::confirmQueuedRequests() {
    if (m_activeSource != InputSource::API) {
        logMessage(2, "Cannot confirm requests: API not active");
        return false;
    }
    
    logMessage(0, "Confirming all queued requests");
    
    // Confirm requests using LocalAPI
    return m_localApi->confirmQueuedRequests();
}

// Set auto-confirm mode
void InputManager::setAutoConfirm(bool autoConfirm) {
    logMessage(0, std::string("Setting auto-confirm mode to ") + (autoConfirm ? "enabled" : "disabled"));
    
    if (m_activeSource == InputSource::API) {
        m_localApi->setAutoConfirm(autoConfirm);
    }
}

// Get auto-confirm mode
bool InputManager::getAutoConfirm() const {
    if (m_activeSource == InputSource::API) {
        return m_localApi->getAutoConfirm();
    }
    return false;
}

// Get pending requests
nlohmann::json InputManager::getPendingRequests() const {
    if (m_activeSource == InputSource::API) {
        return m_localApi->getPendingRequests();
    }
    return nlohmann::json::object();
}

// Print API help information
void InputManager::printApiHelpInfo(int port) const {
    std::cout << "\n=== API SERVER RUNNING ===\n" << std::endl;
    std::cout << "The server is now running and listening for HTTP requests on port " << port << std::endl;
    std::cout << "Available endpoints:" << std::endl;
    std::cout << "  GET  /status      - Get server status" << std::endl;
    std::cout << "  GET  /trades      - Get current trades" << std::endl;
    std::cout << "  GET  /pending     - Get pending (queued) trades" << std::endl;
    std::cout << "  POST /trade       - Queue a trade request" << std::endl;
    std::cout << "  POST /confirm     - Confirm all queued requests" << std::endl;
    std::cout << "  POST /clear       - Clear a symbol" << std::endl;
    std::cout << "  POST /clear-all   - Clear all symbols" << std::endl;
    std::cout << "  POST /emergency-stop - Trigger emergency stop" << std::endl;
    std::cout << "  POST /set-auto-confirm - Enable/disable auto-confirmation" << std::endl;
    
    std::cout << "\nExample curl commands:" << std::endl;
    std::cout << "  curl -X GET http://localhost:" << port << "/status" << std::endl;
    std::cout << "  curl -X GET http://localhost:" << port << "/trades" << std::endl;
    std::cout << "  curl -X GET http://localhost:" << port << "/pending" << std::endl;
    std::cout << "  curl -X POST -H \"Content-Type: application/json\" -d '{\"symbol\":\"AAPL\",\"params\":{\"lots\":5,\"margin\":\".10\",\"stopLoss\":\".05\",\"maxTrades\":10,\"lossThreshold\":3,\"winThreshold\":5,\"minWinRate\":\".50\",\"maxHoldSeconds\":3600}}' http://localhost:" << port << "/trade" << std::endl;
    std::cout << "  curl -X POST http://localhost:" << port << "/confirm" << std::endl;
    std::cout << "  curl -X POST -H \"Content-Type: application/json\" -d '{\"symbol\":\"AAPL\"}' http://localhost:" << port << "/clear" << std::endl;
    std::cout << "  curl -X POST http://localhost:" << port << "/clear-all" << std::endl;
    std::cout << "  curl -X POST http://localhost:" << port << "/emergency-stop" << std::endl;
    std::cout << "  curl -X POST -H \"Content-Type: application/json\" -d '{\"autoConfirm\":true}' http://localhost:" << port << "/set-auto-confirm" << std::endl;
}

} // namespace input_manager