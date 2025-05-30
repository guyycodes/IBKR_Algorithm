#include "local_api.hpp"
#include "../input_manager.hpp"
#include "../../models/model_manager.hpp"
#include "../../models/model_manager_factory.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>
#include "../../util/app_state/app_state.hpp"

// Socket headers for HTTP server
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>

namespace local_api {

// Constructor
LocalAPI::LocalAPI()
    : m_isRunning(false),
      m_logLevel(0),
      m_serverRunning(false),
      m_serverSocket(-1),
      m_serverPort(9000),
      m_autoConfirm(false),
      m_hasConnectionIssues(false) {
    // Initialize empty JSON objects
    m_config = nlohmann::json::object();
    m_lastConnectionResults = nlohmann::json::object();
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
        // Force the socket to close immediately
        #ifdef _WIN32
        // Windows
        closesocket(m_serverSocket);
        #else
        // Unix/Linux
        close(m_serverSocket);
        #endif
        m_serverSocket = -1;
    }
    
    // Wait for the server thread to finish
    if (m_serverThread.joinable()) {
        // Set a timeout for joining
        std::thread joinThread([this]() {
            if (m_serverThread.joinable()) {
                m_serverThread.join();
            }
        });
        
        // Wait for join thread with timeout
        if (joinThread.joinable()) {
            joinThread.join();
        }
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
    
    // Set socket to non-blocking mode
    int flags = fcntl(m_serverSocket, F_GETFL, 0);
    fcntl(m_serverSocket, F_SETFL, flags | O_NONBLOCK);
    
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
    
    // Accept connections loop with timeout
    int addrlen = sizeof(address);
    while (m_serverRunning) {
        // Use select to implement a timeout for accept
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_serverSocket, &readfds);
        
        // Set timeout to 1 second
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        // Wait for activity on the socket with timeout
        int activity = select(m_serverSocket + 1, &readfds, NULL, NULL, &timeout);
        
        if (activity < 0 && errno != EINTR) {
            // Error occurred
            logMessage(2, "Select error: " + std::string(strerror(errno)));
            break;
        }
        
        // Check if we should still be running
        if (!m_serverRunning) {
            break;
        }
        
        // Check if we have a connection
        if (activity > 0 && FD_ISSET(m_serverSocket, &readfds)) {
            int clientSocket = accept(m_serverSocket, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (clientSocket < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN && m_serverRunning) {
                    logMessage(2, "Failed to accept connection: " + std::string(strerror(errno)));
                }
                continue;
            }
            
            // Handle the request
            handleHttpRequest(clientSocket);
            
            // Close client socket
            close(clientSocket);
        }
    }
    
    // Clean up server socket
    if (m_serverSocket >= 0) {
        close(m_serverSocket);
        m_serverSocket = -1;
    }
    
    logMessage(0, "HTTP server thread exiting cleanly");
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
        // GET /queue-data?symbol=XYZ - Return queue data for a specific symbol
        else if (path.find("/queue-data") == 0) {
            // Extract symbol from query string
            size_t symbolPos = request.find("symbol=");
            if (symbolPos != std::string::npos) {
                // Extract the symbol parameter value
                std::string symbol;
                size_t valueStart = symbolPos + 7; // length of "symbol="
                size_t valueEnd = request.find(" ", valueStart);
                if (valueEnd == std::string::npos) {
                    valueEnd = request.find("\r", valueStart);
                }
                if (valueEnd != std::string::npos) {
                    symbol = request.substr(valueStart, valueEnd - valueStart);
                    
                    // Get queue data for the symbol
                    responseBody = getSymbolQueueData(symbol).dump(2);
                    statusCode = 200;
                } else {
                    responseBody = "{\"status\": \"error\", \"message\": \"Invalid symbol parameter\"}";
                    statusCode = 400;
                }
            } else {
                responseBody = "{\"status\": \"error\", \"message\": \"Missing symbol parameter\"}";
                statusCode = 400;
            }
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
                        bool autoConfirmWasDone = false;
                        
                        // Auto-confirm if enabled
                        if (m_autoConfirm) {
                            autoConfirmWasDone = confirmQueuedRequests();
                            
                            // Check for connection issues after auto-confirm
                            if (autoConfirmWasDone && m_hasConnectionIssues) {
                                // Return a warning response with connection status
                                nlohmann::json response = {
                                    {"status", "warning"},
                                    {"message", "Request queued and auto-confirmed but IBKR connection failed"},
                                    {"connection_status", m_lastConnectionResults},
                                    {"error_code", 502},
                                    {"error_message", "Couldn't connect to TWS/Gateway after maximum retry attempts"}
                                };
                                responseBody = response.dump(2);
                                statusCode = 200; // Still 200 because request was processed, but with warning
                                return buildHttpResponse(statusCode, responseBody, contentType);
                            }
                        }
                        
                        // Standard success response if no auto-confirm or no connection issues
                        responseBody = "{\"status\": \"success\", \"message\": \"" + 
                            std::string(autoConfirmWasDone ? "Request queued and auto-confirmed" : "Request queued") + "\"}";
                        statusCode = 200;
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
                    if (m_hasConnectionIssues) {
                        // Return a more detailed response with connection status
                        nlohmann::json response = {
                            {"status", "warning"},
                            {"message", "Requests confirmed but IBKR connection failed - max retries reached"},
                            {"connection_status", m_lastConnectionResults},
                            {"error_code", 502},
                            {"error_message", "Couldn't connect to TWS/Gateway after maximum retry attempts"}
                        };
                        responseBody = response.dump(2);
                        statusCode = 200; // Still 200 because request was processed, but with warning
                    } else {
                        responseBody = "{\"status\": \"success\", \"message\": \"Requests confirmed\"}";
                        statusCode = 200;
                    }
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
                // Call emergency stop function
                logMessage(0, "Emergency stop endpoint called");
                
                // Create a watchdog that will force exit after sending the response
                std::thread([]() {
                    // Wait a short time to allow response to be sent
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    std::cout << "Emergency stop timeout reached. Forcing exit..." << std::endl;
                    _exit(0);  // Force immediate exit
                }).detach();
                
                // Send normal response
                responseBody = "{\"status\": \"success\", \"message\": \"Emergency stop triggered\"}";
                statusCode = 200;
                
                // Normal processing will continue, but watchdog will force exit soon
                emergencyStop();
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
    
    return buildHttpResponse(statusCode, responseBody, contentType);
}

// Helper method to build HTTP response
std::string LocalAPI::buildHttpResponse(int statusCode, const std::string& responseBody, const std::string& contentType) {
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
    
    nlohmann::json connectionResults = nlohmann::json::object();
    bool hasConnectionIssues = false;
    
    // Move all pending requests to the main queue and process them
    for (const auto& request : m_pendingQueue) {
        // Add to main queue
        m_requestQueue.push_back(request);
        
        // Execute the request
        bool executeSuccess = executeRequest(request);
        
        // Get the symbol to check its connection status
        if (request.contains("symbol")) {
            std::string symbol = request["symbol"];
            std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
            
            // Check connection status via AppState and ModelManagerFactory
            auto& factory = model_manager::ModelManagerFactory::getInstance();
            auto model = factory.getModelManager(symbol);
            
            if (model) {
                // Check if the model has exceeded max connection attempts or has connection issues
                int connectionAttempts = model->getConnectionAttempts();
                bool connectedToIBKR = model->isConnected();
                
                connectionResults[symbol] = {
                    {"symbol", symbol},
                    {"connected", connectedToIBKR},
                    {"connection_attempts", connectionAttempts},
                    {"max_attempts_reached", connectionAttempts > model_manager::ModelManager::getMaxConnectionAttempts() - 1}
                };
                
                if (!connectedToIBKR && connectionAttempts >= model_manager::ModelManager::getMaxConnectionAttempts()) {
                    hasConnectionIssues = true;
                }
            }
        }
    }
    
    // Clear the pending queue
    int confirmedCount = m_pendingQueue.size();
    m_pendingQueue.clear();
    
    // Notify input manager of changes
    notifyRequestQueueChanged();
    
    logMessage(0, "Confirmed " + std::to_string(confirmedCount) + " requests");
    
    // Store connection status for use in HTTP responses
    m_lastConnectionResults = connectionResults;
    m_hasConnectionIssues = hasConnectionIssues;
    
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
    
    // Get the list of symbols that are running on threads
    auto& appState = app_state::AppState::getInstance();
    auto runningSymbols = appState.getRunningSymbols();
    status["active_threads"] = runningSymbols;
    status["thread_count"] = runningSymbols.size();
    
    // Add detailed information about requests in the queue
    nlohmann::json queuedSymbols = nlohmann::json::object();
    for (const auto& request : m_requestQueue) {
        if (request.contains("symbol") && request.contains("params")) {
            std::string symbol = request["symbol"];
            std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
            queuedSymbols[symbol] = request["params"];
        }
    }
    status["queued_symbols"] = queuedSymbols;
    
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

// Emergency stop handler for HTTP server - force close everything
void LocalAPI::emergencyStop() {
    std::cout << "[LocalAPI] Emergency stop triggered" << std::endl;
    
    // Set the server running flag to false to stop the main loop
    m_serverRunning = false;
    
    // Close the server socket to force accept() to return with an error
    if (m_serverSocket >= 0) {
        std::cout << "[LocalAPI] Forcibly closing server socket" << std::endl;
        close(m_serverSocket);
        m_serverSocket = -1;
    }
    
    // Clear all pending requests to avoid starting new operations
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_requestQueue.clear();
        m_pendingQueue.clear();
    }
    
    // Request emergency stop of all threads via AppState
    auto& appState = app_state::AppState::getInstance();
    appState.requestEmergencyStop(1000, "LocalAPI::emergencyStop");
    
    // Notify the InputManager of the emergency stop if not already handling one
    if (!m_isEmergencyStop) {
        m_isEmergencyStop = true;
        std::cout << "[LocalAPI] Notifying InputManager of emergency stop" << std::endl;
        auto inputManager = m_inputManager.lock();
        if (inputManager) {
            inputManager->emergencyStop();
        }
    }
    
    std::cout << "[LocalAPI] Emergency stop actions completed" << std::endl;
}

// Handle the /emergency-stop endpoint
void LocalAPI::handleEmergencyStop(int clientSocket) {
    std::cout << "[LocalAPI] Received emergency stop request via HTTP endpoint" << std::endl;
    
    // Create JSON response
    nlohmann::json response = {
        {"status", "success"},
        {"message", "Emergency stop triggered"}
    };
    
    // Send success response before stopping
    std::string responseStr = "HTTP/1.1 200 OK\r\n";
    responseStr += "Content-Type: application/json\r\n";
    responseStr += "Connection: close\r\n";
    responseStr += "Content-Length: " + std::to_string(response.dump().length()) + "\r\n\r\n";
    responseStr += response.dump();
    
    // Send the response
    if (clientSocket >= 0) {
        send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
        close(clientSocket);
    }
    
    // Trigger emergency stop (this will close sockets and stop threads)
    emergencyStop();
    
    // No need to call AppState emergency stop again as it's already called in emergencyStop()
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
//
// This method performs several important operations:
// 1. Removes the symbol from both the main request queue and pending queue
// 2. Stops and removes the thread associated with the symbol
// 3. Updates the API's internal state
//
// IMPORTANT: This method directly accesses AppState to stop threads rather than
// calling back to InputManager->clearSymbol() to avoid infinite recursion.
// The original design had a circular reference where:
//   - LocalAPI::clearSymbol() called InputManager::clearSymbol()
//   - InputManager::clearSymbol() called LocalAPI::clearSymbol()
//   - This created an infinite loop that caused stack overflow
bool LocalAPI::clearSymbol(const std::string& symbol) {
    logMessage(1, "Clearing symbol: " + symbol);
    
    bool found = false;
    
    // Convert symbol to uppercase for case-insensitive comparison
    std::string upperSymbol = symbol;
    std::transform(upperSymbol.begin(), upperSymbol.end(), upperSymbol.begin(), ::toupper);
    
    // Access ModelManagerFactory to check if the model already exists
    auto& factory = model_manager::ModelManagerFactory::getInstance();
    
    // Try to get the model before removing it to ensure it's properly cleared
    auto model = factory.getModelManager(upperSymbol);
    
    // Remove all requests for this symbol from the main queue
    auto it = m_requestQueue.begin();
    while (it != m_requestQueue.end()) {
        if (it->contains("symbol")) {
            std::string reqSymbol = (*it)["symbol"].get<std::string>();
            std::transform(reqSymbol.begin(), reqSymbol.end(), reqSymbol.begin(), ::toupper);
            
            if (reqSymbol == upperSymbol) {
                it = m_requestQueue.erase(it);
                found = true;
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
    
    // Also check and remove from the pending queue
    auto pendingIt = m_pendingQueue.begin();
    while (pendingIt != m_pendingQueue.end()) {
        if (pendingIt->contains("symbol")) {
            std::string reqSymbol = (*pendingIt)["symbol"].get<std::string>();
            std::transform(reqSymbol.begin(), reqSymbol.end(), reqSymbol.begin(), ::toupper);
            
            if (reqSymbol == upperSymbol) {
                pendingIt = m_pendingQueue.erase(pendingIt);
                found = true;
            } else {
                ++pendingIt;
            }
        } else {
            ++pendingIt;
        }
    }

    // Thread Management: Stop and remove the running thread if it exists
    // We access AppState directly instead of going through InputManager to avoid circular references
    auto& appState = app_state::AppState::getInstance();
    
    // Check if the thread is running
    if (appState.hasRunningThread(upperSymbol)) {
        appState.requestThreadStop(upperSymbol, "LocalAPI::clearSymbol");
        found = true;
        logMessage(0, "Thread stop requested for symbol " + upperSymbol);
    }
    
    // Ensure the model is fully removed from the factory to reset connection attempts
    // This forces a new ModelManager to be created next time this symbol is used
    if (factory.hasModel(upperSymbol)) {
        factory.removeModel(upperSymbol);
        found = true;
    }
    
    // IMPORTANT: We do NOT call back to InputManager::clearSymbol here
    // This would create an infinite recursion:
    //   LocalAPI::clearSymbol -> InputManager::clearSymbol -> LocalAPI::clearSymbol -> ...
    // Instead, we handle everything directly in this method
    
    if (found) {
        logMessage(0, "Symbol " + symbol + " cleared from queues and threads");
        // Notify input manager of changes
        notifyRequestQueueChanged();
    } else {
        logMessage(1, "Symbol " + symbol + " not found in any queue or active threads");
    }
    
    return found;
}

// Clear all pending requests
//
// This method performs several important operations:
// 1. Clears all symbols from both the main request queue and pending queue
// 2. Stops ALL running model threads via AppState
// 3. Updates the API's internal state
//
// IMPORTANT: This method directly accesses AppState to stop threads rather than
// calling back to InputManager->clearAllInputs() to avoid infinite recursion.
void LocalAPI::clearAllRequests() {
    logMessage(0, "Clearing all requests and threads");
    
    // Clear the main request queue
    m_requestQueue.clear();
    
    // Also clear the pending queue
    m_pendingQueue.clear();
    
    // Thread Management: Request all threads to stop using proper request API
    // We access AppState directly instead of going through InputManager
    auto& appState = app_state::AppState::getInstance();
    appState.requestAllThreadsStop("LocalAPI::clearAllRequests");
    logMessage(0, "Stop request sent for all model threads");
    
    // IMPORTANT: We do NOT call back to InputManager::clearAllInputs here
    // This would create an infinite recursion:
    //   LocalAPI::clearAllRequests -> InputManager::clearAllInputs -> LocalAPI::clearAllRequests -> ...
    // Instead, we handle everything directly in this method
    
    // Notify input manager of changes
    notifyRequestQueueChanged();
    
    logMessage(0, "All requests cleared from queues and all threads stop requested");
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

// Get queue data for a specific symbol
nlohmann::json LocalAPI::getSymbolQueueData(const std::string& symbol) const {
    nlohmann::json queueData;
    queueData["symbol"] = symbol;
    queueData["ticks"] = nlohmann::json::array();
    
    try {
        // Convert symbol to uppercase
        std::string upperSymbol = symbol;
        std::transform(upperSymbol.begin(), upperSymbol.end(), upperSymbol.begin(), ::toupper);
        
        // Get model manager for this symbol
        auto& factory = model_manager::ModelManagerFactory::getInstance();
        auto model = factory.getModelManager(upperSymbol);
        
        if (model) {
            // Log initial information
            std::cout << "[LocalAPI] Getting queue data for symbol: " << upperSymbol 
                      << ", Connected: " << (model->isConnected() ? "Yes" : "No")
                      << ", Request ID: " << (model->isConnected() ? "Valid" : "None") << std::endl;
                      
            // Add connection status to response
            queueData["connection_status"] = model->getConnectionStatus();
            
            // First try the normal way - get all ticks in the time window
            auto ticks = model->getTicksInWindow();
            
            // If no ticks in window, try getting direct queue size
            size_t queueSize = model->getTickCount();
            queueData["queue_size"] = queueSize;
            
            // If window method returned ticks, use them
            if (!ticks.empty()) {
                std::cout << "[LocalAPI] Found " << ticks.size() << " ticks in time window for symbol: " << upperSymbol << std::endl;
                queueData["tick_count"] = ticks.size();
                
                // Add individual ticks to the response
                for (const auto& tick : ticks) {
                    nlohmann::json tickData;
                    tickData["price"] = tick.last;  // Backward compatibility - use 'last' field
                    tickData["bid"] = tick.bid;
                    tickData["ask"] = tick.ask;
                    tickData["bidSize"] = tick.bidSize;
                    tickData["askSize"] = tick.askSize;
                    tickData["last"] = tick.last;
                    tickData["volume"] = tick.volume;
                    tickData["timestamp"] = tick.timestamp;
                    
                    // Add technical indicators
                    tickData["rsi"] = tick.rsi;
                    tickData["ema9"] = tick.ema9;
                    tickData["ema26"] = tick.ema26;
                    tickData["alma"] = tick.alma;
                    tickData["chaikin"] = tick.chaikin;
                    
                    // Add OHLC data
                    tickData["open"] = tick.open;
                    tickData["high"] = tick.high;
                    tickData["low"] = tick.low;
                    tickData["close"] = tick.close;
                    
                    // Add derived metrics
                    tickData["mid"] = tick.mid;
                    tickData["spread"] = tick.spread;
                    tickData["spreadPercent"] = tick.spreadPercent;
                    tickData["vwap"] = tick.vwap;
                    tickData["imbalance"] = tick.imbalance;
                    
                    queueData["ticks"].push_back(tickData);
                }
            } 
            // If window is empty but queue has data, try alternative approach
            else if (queueSize > 0) {
                std::cout << "[LocalAPI] Window returned no ticks but queue has " << queueSize 
                          << " items. Using direct queue access for symbol: " << upperSymbol << std::endl;
                
                // Get direct access to the raw data model's queue for diagnostic purposes
                auto rawModel = model->getRawDataModel();
                if (rawModel) {
                    auto* stockQueue = rawModel->getStockQueue();
                    if (stockQueue) {
                        std::cout << "[LocalAPI] Direct queue access shows " << stockQueue->size() 
                                  << " items in queue" << std::endl;
                        
                        // Get the latest tick as a fallback
                        stock_data_tick::StockData latestTick;
                        if (rawModel->getLatestTickFromQueue(latestTick)) {
                            std::cout << "[LocalAPI] Found latest tick - adding to response" << std::endl;
                            
                            // Add at least the latest tick to the response
                            nlohmann::json tickData;
                            tickData["price"] = latestTick.last;
                            tickData["bid"] = latestTick.bid;
                            tickData["ask"] = latestTick.ask;
                            tickData["bidSize"] = latestTick.bidSize;
                            tickData["askSize"] = latestTick.askSize;
                            tickData["last"] = latestTick.last;
                            tickData["volume"] = latestTick.volume;
                            tickData["timestamp"] = latestTick.timestamp;
                            
                            // Add technical indicators
                            tickData["rsi"] = latestTick.rsi;
                            tickData["ema9"] = latestTick.ema9;
                            tickData["ema26"] = latestTick.ema26;
                            tickData["alma"] = latestTick.alma;
                            tickData["chaikin"] = latestTick.chaikin;
                            
                            // Add OHLC data
                            tickData["open"] = latestTick.open;
                            tickData["high"] = latestTick.high;
                            tickData["low"] = latestTick.low;
                            tickData["close"] = latestTick.close;
                            
                            // Add derived metrics
                            tickData["mid"] = latestTick.mid;
                            tickData["spread"] = latestTick.spread;
                            tickData["spreadPercent"] = latestTick.spreadPercent;
                            tickData["vwap"] = latestTick.vwap;
                            tickData["imbalance"] = latestTick.imbalance;
                            
                            queueData["ticks"].push_back(tickData);
                            queueData["tick_count"] = 1;
                            queueData["note"] = "Using latest tick as fallback";
                        } else {
                            queueData["tick_count"] = 0;
                            queueData["note"] = "Queue has data but couldn't extract items";
                        }
                    }
                }
            } else {
                // Truly empty queue
                queueData["tick_count"] = 0;
                queueData["note"] = "Queue is completely empty";
            }
            
            // Add timestamp to response
            queueData["request_timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            
            // Add current time window settings for diagnostics
            auto timeWindow = model->getTimeWindow();
            queueData["window_settings"] = {
                {"size", timeWindow.first},
                {"unit", (timeWindow.second == model_manager::TimeWindowUnit::SECONDS ? "seconds" : 
                          (timeWindow.second == model_manager::TimeWindowUnit::MINUTES ? "minutes" : "hours"))}
            };
            
            // Add volume profile summary
            auto& volumeProfile = model->getVolumeProfile();
            queueData["volume_profile_summary"] = volumeProfile.get_summary();
        } else {
            queueData["error"] = "Symbol not found or no model available";
        }
    } catch (const std::exception& e) {
        queueData["error"] = std::string("Exception while retrieving queue data: ") + e.what();
    }
    
    return queueData;
}

} // namespace local_api