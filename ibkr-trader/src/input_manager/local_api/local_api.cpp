#include "local_api.hpp"
#include "../input_manager.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>

// Socket headers for HTTP server
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

namespace local_api {

// Constructor
LocalAPI::LocalAPI()
    : m_isRunning(false),
      m_logLevel(0),
      m_serverRunning(false),
      m_serverSocket(-1),
      m_serverPort(9000),
      m_autoConfirm(false) {
    // Initialize empty JSON objects
    m_config = nlohmann::json::object();
    m_requestQueue.clear();
    m_pendingQueue.clear();
    
    // Set default callbacks to do nothing
    m_tradeCallback = [](const nlohmann::json&) {};
    m_errorCallback = [](const std::string&, const std::string&) {};
    
    logMessage(0, "LocalAPI instance created");
}

// Destructor
LocalAPI::~LocalAPI() {
    // Make sure to stop the API if it's still running
    if (m_isRunning) {
        stop();
    }
    
    logMessage(0, "LocalAPI instance destroyed");
}

// Set the input manager reference
void LocalAPI::setInputManager(std::shared_ptr<input_manager::InputManager> inputManager) {
    m_inputManager = inputManager;
}

// Initialize the API with configuration
bool LocalAPI::initialize(const std::string& configPath) {
    logMessage(1, "Initializing LocalAPI");
    
    // If a config path is provided, parse it
    if (!configPath.empty()) {
        if (!parseConfig(configPath)) {
            logMessage(2, "Failed to parse configuration file: " + configPath);
            return false;
        }
    }
    
    // Additional initialization can go here
    
    logMessage(0, "LocalAPI initialized successfully");
    return true;
}

// Start the API server
bool LocalAPI::start(int port) {
    if (m_isRunning) {
        logMessage(1, "API already running");
        return true;
    }
    
    logMessage(0, "Starting LocalAPI server on port " + std::to_string(port));
    
    m_serverPort = port;
    m_isRunning = true;
    
    // Start the HTTP server
    startHttpServer(port);
    
    logMessage(0, "LocalAPI server started");
    return true;
}

// Stop the API server
void LocalAPI::stop() {
    if (!m_isRunning) {
        logMessage(1, "API not running");
        return;
    }
    
    logMessage(0, "Stopping LocalAPI server");
    
    // Stop the HTTP server
    stopHttpServer();
    
    m_isRunning = false;
    
    logMessage(0, "LocalAPI server stopped");
}

// Start HTTP server
void LocalAPI::startHttpServer(int port) {
    if (m_serverRunning) {
        stopHttpServer();
    }
    
    m_serverRunning = true;
    m_serverPort = port;
    
    // Start server in a separate thread
    m_serverThread = std::thread(&LocalAPI::httpServerThread, this);
    
    logMessage(0, "HTTP server started on port " + std::to_string(port));
}

// Stop HTTP server
void LocalAPI::stopHttpServer() {
    if (!m_serverRunning) {
        return;
    }
    
    logMessage(0, "Stopping HTTP server");
    
    // Signal the server thread to stop
    m_serverRunning = false;
    
    // Close server socket to stop accept() blocking
    if (m_serverSocket >= 0) {
        close(m_serverSocket);
        m_serverSocket = -1;
    }
    
    // Wait for the server thread to finish
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
    
    logMessage(0, "HTTP server stopped");
}

// HTTP server thread function
void LocalAPI::httpServerThread() {
    // Create socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket < 0) {
        logMessage(2, "Failed to create socket");
        m_serverRunning = false;
        return;
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logMessage(2, "Failed to set socket options");
        close(m_serverSocket);
        m_serverRunning = false;
        return;
    }
    
    // Bind to port
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // bind to 0.0.0.0 to accept connections from any IP
    address.sin_port = htons(m_serverPort);
    
    if (bind(m_serverSocket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        logMessage(2, "Failed to bind to port " + std::to_string(m_serverPort));
        close(m_serverSocket);
        m_serverRunning = false;
        return;
    }
    
    // Listen for connections
    if (listen(m_serverSocket, 3) < 0) {
        logMessage(2, "Failed to listen on socket");
        close(m_serverSocket);
        m_serverRunning = false;
        return;
    }
    
    logMessage(0, "HTTP server listening on port " + std::to_string(m_serverPort));
    
    // Accept connections loop
    int addrlen = sizeof(address);
    while (m_serverRunning) {
        int clientSocket = accept(m_serverSocket, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (clientSocket < 0) {
            if (m_serverRunning) {
                logMessage(2, "Failed to accept connection");
            }
            continue;
        }
        
        // Handle the request
        handleHttpRequest(clientSocket);
        
        // Close client socket
        close(clientSocket);
    }
    
    // Clean up server socket (should be closed already)
    if (m_serverSocket >= 0) {
        close(m_serverSocket);
        m_serverSocket = -1;
    }
}

// Handle HTTP request
void LocalAPI::handleHttpRequest(int clientSocket) {
    // Buffer for incoming request
    char buffer[4096] = {0};
    int valread = read(clientSocket, buffer, sizeof(buffer) - 1);
    
    if (valread <= 0) {
        logMessage(1, "Empty request received");
        return;
    }
    
    // Process the request
    std::string response = processHttpRequest(std::string(buffer, valread));
    
    // Send response
    send(clientSocket, response.c_str(), response.length(), 0);
}

// Process HTTP request
std::string LocalAPI::processHttpRequest(const std::string& request) {
    // Log incoming request
    logMessage(1, "Received HTTP request: \n" + request);
    
    // Parse HTTP method and path
    std::istringstream requestStream(request);
    std::string method, path, version;
    requestStream >> method >> path >> version;
    
    // Default response is 404 Not Found
    std::string responseBody = "{\"error\": \"Not Found\"}";
    int statusCode = 404;
    std::string contentType = "application/json";
    
    // Process GET and POST requests
    if (method == "GET") {
        // GET /status - Return API status
        if (path == "/status") {
            responseBody = getStatus().dump(2);
            statusCode = 200;
        }
        // GET /trades - Return current trades
        else if (path == "/trades") {
            responseBody = getFormattedRequests().dump(2);
            statusCode = 200;
        }
        // GET /pending - Return pending trades
        else if (path == "/pending") {
            responseBody = getPendingRequests().dump(2);
            statusCode = 200;
        }
    }
    else if (method == "POST") {
        // Find the request body
        std::string requestBody;
        
        // For POST requests, we need to find the body after the headers
        size_t bodyPos = request.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            requestBody = request.substr(bodyPos + 4);
            logMessage(1, "Extracted request body: " + requestBody);
            
            // Process different API endpoints
            if (path == "/trade") {
                // Queue a trading request
                try {
                    nlohmann::json requestJson = nlohmann::json::parse(requestBody);
                    logMessage(0, "Queueing trade request: " + requestJson.dump());
                    
                    if (queueTradingRequest(requestJson)) {
                        responseBody = "{\"status\": \"success\", \"message\": \"Request queued\"}";
                        statusCode = 200;
                        
                        // Auto-confirm if enabled
                        if (m_autoConfirm) {
                            confirmQueuedRequests();
                        }
                    } else {
                        responseBody = "{\"status\": \"error\", \"message\": \"Failed to queue request\"}";
                        statusCode = 400;
                    }
                } catch (const std::exception& e) {
                    responseBody = "{\"status\": \"error\", \"message\": \"Invalid JSON: " + std::string(e.what()) + "\"}";
                    statusCode = 400;
                }
            }
            else if (path == "/confirm") {
                // Confirm all pending requests
                if (confirmQueuedRequests()) {
                    responseBody = "{\"status\": \"success\", \"message\": \"Requests confirmed\"}";
                    statusCode = 200;
                } else {
                    responseBody = "{\"status\": \"error\", \"message\": \"No pending requests to confirm\"}";
                    statusCode = 400;
                }
            }
            else if (path == "/clear") {
                // Clear a symbol
                try {
                    nlohmann::json requestJson = nlohmann::json::parse(requestBody);
                    if (requestJson.contains("symbol")) {
                        std::string symbol = requestJson["symbol"];
                        if (clearSymbol(symbol)) {
                            responseBody = "{\"status\": \"success\", \"message\": \"Symbol cleared\"}";
                            statusCode = 200;
                        } else {
                            responseBody = "{\"status\": \"error\", \"message\": \"Symbol not found\"}";
                            statusCode = 404;
                        }
                    } else {
                        responseBody = "{\"status\": \"error\", \"message\": \"Missing symbol\"}";
                        statusCode = 400;
                    }
                } catch (const std::exception& e) {
                    responseBody = "{\"status\": \"error\", \"message\": \"Invalid JSON\"}";
                    statusCode = 400;
                }
            }
            else if (path == "/clear-all") {
                // Clear all symbols
                clearAllRequests();
                responseBody = "{\"status\": \"success\", \"message\": \"All symbols cleared\"}";
                statusCode = 200;
            }
            else if (path == "/emergency-stop") {
                // Emergency stop
                emergencyStop();
                responseBody = "{\"status\": \"success\", \"message\": \"Emergency stop triggered\"}";
                statusCode = 200;
            }
            else if (path == "/set-auto-confirm") {
                // Set auto-confirm mode
                try {
                    nlohmann::json requestJson = nlohmann::json::parse(requestBody);
                    if (requestJson.contains("autoConfirm")) {
                        bool autoConfirm = requestJson["autoConfirm"];
                        setAutoConfirm(autoConfirm);
                        responseBody = "{\"status\": \"success\", \"message\": \"Auto-confirm " + 
                            std::string(autoConfirm ? "enabled" : "disabled") + "\"}";
                        statusCode = 200;
                    } else {
                        responseBody = "{\"status\": \"error\", \"message\": \"Missing autoConfirm parameter\"}";
                        statusCode = 400;
                    }
                } catch (const std::exception& e) {
                    responseBody = "{\"status\": \"error\", \"message\": \"Invalid JSON\"}";
                    statusCode = 400;
                }
            }
        } else {
            responseBody = "{\"status\": \"error\", \"message\": \"Empty request body\"}";
            statusCode = 400;
        }
    }
    
    // Build HTTP response
    std::string statusText = (statusCode == 200) ? "OK" : (statusCode == 400) ? "Bad Request" : "Not Found";
    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + std::to_string(responseBody.length()) + "\r\n";
    response += "Connection: close\r\n";
    response += "Access-Control-Allow-Origin: *\r\n"; // Enable CORS
    response += "\r\n";
    response += responseBody;
    
    return response;
}

// Queue a trading request without processing it
bool LocalAPI::queueTradingRequest(const nlohmann::json& request) {
    if (!m_isRunning) {
        logMessage(2, "Cannot queue request: API not running");
        return false;
    }
    
    logMessage(1, "Queuing trading request for symbol: " + request["symbol"].get<std::string>());
    
    // Validate the request
    if (!validateRequest(request)) {
        std::string error = "Invalid request format";
        logMessage(2, error);
        m_errorCallback("request_validation", error);
        return false;
    }
    
    // Add to pending queue
    m_pendingQueue.push_back(request);
    
    logMessage(0, "Request queued successfully (pending confirmation)");
    return true;
}

// Confirm all queued requests
bool LocalAPI::confirmQueuedRequests() {
    if (m_pendingQueue.empty()) {
        logMessage(1, "No pending requests to confirm");
        return false;
    }
    
    logMessage(0, "Confirming " + std::to_string(m_pendingQueue.size()) + " pending requests");
    
    // Move all pending requests to the main queue and process them
    for (const auto& request : m_pendingQueue) {
        // Add to main queue
        m_requestQueue.push_back(request);
        
        // Execute the request
        executeRequest(request);
    }
    
    // Clear the pending queue
    int confirmedCount = m_pendingQueue.size();
    m_pendingQueue.clear();
    
    // Notify input manager of changes
    notifyRequestQueueChanged();
    
    logMessage(0, "Confirmed " + std::to_string(confirmedCount) + " requests");
    return true;
}

// Get all pending requests in a format compatible with InputManager's output JSON
nlohmann::json LocalAPI::getPendingRequests() const {
    nlohmann::json formattedOutput;
    
    // Convert the pending queue to the format expected by InputManager
    for (const auto& request : m_pendingQueue) {
        if (request.contains("symbol") && request.contains("params")) {
            std::string symbol = request["symbol"];
            // Make sure symbol is uppercase
            std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
            formattedOutput[symbol] = request["params"];
        }
    }
    
    return formattedOutput;
}

// Set auto-confirm mode
void LocalAPI::setAutoConfirm(bool autoConfirm) {
    m_autoConfirm = autoConfirm;
    logMessage(0, std::string("Auto-confirm mode ") + (autoConfirm ? "enabled" : "disabled"));
}

// Get auto-confirm mode
bool LocalAPI::getAutoConfirm() const {
    return m_autoConfirm;
}

// Get the status of the API (updated to include pending requests)
nlohmann::json LocalAPI::getStatus() const {
    nlohmann::json status;
    status["running"] = m_isRunning;
    status["queue_size"] = m_requestQueue.size();
    status["pending_size"] = m_pendingQueue.size();
    status["auto_confirm"] = m_autoConfirm;
    status["config"] = m_config;
    
    return status;
}

// Get the status of a specific trading request
nlohmann::json LocalAPI::getRequestStatus(const std::string& requestId) const {
    nlohmann::json status;
    status["request_id"] = requestId;
    status["found"] = false;
    
    // Look for the request with the specified ID
    for (const auto& request : m_requestQueue) {
        if (request.contains("request_id") && request["request_id"] == requestId) {
            status["found"] = true;
            status["request"] = request;
            break;
        }
    }
    
    return status;
}

// Register a callback for trade execution events
void LocalAPI::registerTradeCallback(std::function<void(const nlohmann::json&)> callback) {
    logMessage(1, "Registering trade callback");
    m_tradeCallback = callback;
}

// Register a callback for error events
void LocalAPI::registerErrorCallback(std::function<void(const std::string&, const std::string&)> callback) {
    logMessage(1, "Registering error callback");
    m_errorCallback = callback;
}

// Set logging level
void LocalAPI::setLogLevel(int level) {
    logMessage(0, "Setting log level to " + std::to_string(level));
    m_logLevel = level;
}

// Emergency stop all trading activity
void LocalAPI::emergencyStop() {
    logMessage(0, "EMERGENCY STOP TRIGGERED");
    
    // Clear all pending requests
    clearAllRequests();
    
    // Stop the API
    stop();
    
    // Call error callback to notify of emergency stop
    m_errorCallback("emergency_stop", "Emergency stop triggered");
    
    // Try to propagate emergency stop to input manager if available
    auto inputManager = m_inputManager.lock();
    if (inputManager) {
        // Avoid potential infinite loop by not calling emergencyStop again
        // Just notify that we've already handled it
        logMessage(0, "Notifying InputManager of emergency stop");
    }
    
    logMessage(0, "Emergency stop completed");
}

// Get all current requests in a format compatible with InputManager's output JSON
nlohmann::json LocalAPI::getFormattedRequests() const {
    nlohmann::json formattedOutput;
    
    // Convert the request queue to the format expected by InputManager
    for (const auto& request : m_requestQueue) {
        if (request.contains("symbol") && request.contains("params")) {
            std::string symbol = request["symbol"];
            // Make sure symbol is uppercase
            std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
            formattedOutput[symbol] = request["params"];
        }
    }
    
    return formattedOutput;
}

// Validate trading request structure
bool LocalAPI::validateRequest(const nlohmann::json& request) const {
    // Perform validation of the request structure
    
    // Check that it's an object
    if (!request.is_object()) {
        return false;
    }
    
    // Check for required fields
    if (!request.contains("symbol")) {
        return false;
    }
    
    // Check that symbol is a string
    if (!request["symbol"].is_string()) {
        return false;
    }
    
    // Convert symbol to uppercase
    std::string symbol = request["symbol"];
    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
    
    // Check that it contains parameters
    if (!request.contains("params") || !request["params"].is_object()) {
        return false;
    }
    
    // Check required parameters
    const auto& params = request["params"];
    
    // Required parameters
    std::vector<std::string> requiredParams = {
        "lots", "margin", "stopLoss", "maxTrades", 
        "lossThreshold", "winThreshold", "minWinRate", "maxHoldSeconds"
    };
    
    for (const auto& param : requiredParams) {
        if (!params.contains(param)) {
            return false;
        }
    }
    
    // Type checking for parameters
    if (!params["lots"].is_number_integer() ||
        !params["maxTrades"].is_number_integer() ||
        !params["lossThreshold"].is_number_integer() ||
        !params["winThreshold"].is_number_integer() ||
        !params["maxHoldSeconds"].is_number_integer()) {
        return false;
    }
    
    // For string parameters, check they're strings
    if (!params["margin"].is_string() ||
        !params["stopLoss"].is_string() ||
        !params["minWinRate"].is_string()) {
        return false;
    }
    
    return true;
}

// Process a validated request
bool LocalAPI::executeRequest(const nlohmann::json& request) {
    logMessage(1, "Executing request for symbol: " + request["symbol"].get<std::string>());
    
    // In a real implementation, this would send the request to the trading system
    // For now, just simulate success
    
    // Create a response and store it, but don't call the callback directly
    // This avoids the double callback issue
    nlohmann::json response = request;
    response["status"] = "executed";
    response["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
    
    // DON'T call the trade callback here - let notifyRequestQueueChanged handle it
    // m_tradeCallback(response);  <-- This line is removed
    
    logMessage(0, "Request executed successfully");
    return true;
}

// Log a message
void LocalAPI::logMessage(int level, const std::string& message) const {
    // Only log if the message level is <= the current log level
    if (level <= m_logLevel) {
        std::string prefix;
        
        switch (level) {
            case 0: prefix = "INFO"; break;
            case 1: prefix = "DEBUG"; break;
            case 2: prefix = "ERROR"; break;
            default: prefix = "LOG"; break;
        }
        
        std::cout << "[LocalAPI] [" << prefix << "] " << message << std::endl;
    }
    
    // If we have access to the input manager, use its logging if available
    auto inputManager = m_inputManager.lock();
    if (inputManager && level <= m_logLevel) {
        // We would ideally use input manager's logging here, but to avoid
        // circular calls, we'll keep it simple
    }
}

// Parse configuration file
bool LocalAPI::parseConfig(const std::string& configPath) {
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

// Notify input manager of changes to the request queue
void LocalAPI::notifyRequestQueueChanged() {
    // Get formatted output JSON and notify input manager if available
    auto inputManager = m_inputManager.lock();
    if (inputManager) {
        nlohmann::json formattedOutput = getFormattedRequests();
        // Update the input manager's output JSON using the trade callback
        m_tradeCallback(formattedOutput);
    }
}

// Clear a specific symbol from the processing queue
bool LocalAPI::clearSymbol(const std::string& symbol) {
    logMessage(1, "Clearing symbol: " + symbol);
    
    bool found = false;
    
    // Convert symbol to uppercase for case-insensitive comparison
    std::string upperSymbol = symbol;
    std::transform(upperSymbol.begin(), upperSymbol.end(), upperSymbol.begin(), ::toupper);
    
    // Remove all requests for this symbol from the main queue
    auto it = m_requestQueue.begin();
    while (it != m_requestQueue.end()) {
        if (it->contains("symbol") && (*it)["symbol"].get<std::string>() == upperSymbol) {
            it = m_requestQueue.erase(it);
            found = true;
        } else {
            ++it;
        }
    }
    
    // Also check and remove from the pending queue
    auto pendingIt = m_pendingQueue.begin();
    while (pendingIt != m_pendingQueue.end()) {
        if (pendingIt->contains("symbol") && (*pendingIt)["symbol"].get<std::string>() == upperSymbol) {
            pendingIt = m_pendingQueue.erase(pendingIt);
            found = true;
        } else {
            ++pendingIt;
        }
    }
    
    if (found) {
        logMessage(0, "Symbol " + symbol + " cleared from queues");
        // Notify input manager of changes
        notifyRequestQueueChanged();
    } else {
        logMessage(1, "Symbol " + symbol + " not found in any queue");
    }
    
    return found;
}

// Clear all pending requests
void LocalAPI::clearAllRequests() {
    logMessage(0, "Clearing all requests");
    
    // Clear the main request queue
    m_requestQueue.clear();
    
    // Also clear the pending queue
    m_pendingQueue.clear();
    
    // Notify input manager of changes
    notifyRequestQueueChanged();
    
    logMessage(0, "All requests cleared from both queues");
}

// Process a trading request (for backward compatibility)
bool LocalAPI::processTradingRequest(const nlohmann::json& request) {
    if (!m_isRunning) {
        logMessage(2, "Cannot process request: API not running");
        return false;
    }
    
    logMessage(1, "Processing trading request directly");
    
    // First queue the request
    if (!queueTradingRequest(request)) {
        return false;
    }
    
    // Then immediately confirm it (bypassing the pending queue)
    bool success = false;
    
    // Add to main queue
    m_requestQueue.push_back(request);
    
    // Execute the request
    success = executeRequest(request);
    
    // Remove from pending queue
    for (auto it = m_pendingQueue.begin(); it != m_pendingQueue.end(); ++it) {
        if (it->contains("symbol") && 
            it->contains("params") && 
            (*it)["symbol"] == request["symbol"]) {
            m_pendingQueue.erase(it);
            break;
        }
    }
    
    // Notify input manager of changes
    notifyRequestQueueChanged();
    
    return success;
}

// Process a batch of trading requests (for backward compatibility)
bool LocalAPI::processBatchRequests(const std::vector<nlohmann::json>& requests) {
    if (!m_isRunning) {
        logMessage(2, "Cannot process batch: API not running");
        return false;
    }
    
    logMessage(1, "Processing batch of " + std::to_string(requests.size()) + " requests");
    
    bool allSuccessful = true;
    
    // Process each request in the batch
    for (const auto& request : requests) {
        if (!processTradingRequest(request)) {
            allSuccessful = false;
        }
    }
    
    return allSuccessful;
}

} // namespace local_api