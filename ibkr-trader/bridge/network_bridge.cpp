// #include "network_bridge.hpp"
// #include <iostream>
// #include <sstream>
// #include <cstring>
// #include <errno.h>

// // ═══════════════════════════════════════════════════════════════════════════════
// // TCP SERVER IMPLEMENTATION: x86 Container ↔ ARM MacBook
// // ═══════════════════════════════════════════════════════════════════════════════

// bool NetworkBridge::start() {
//     if (m_running.load()) {
//         std::cout << "⚠️  NetworkBridge already running on port " << m_port << std::endl;
//         return false;
//     }
    
//     // Create socket
//     m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
//     if (m_serverSocket < 0) {
//         std::cerr << "❌ Failed to create socket: " << strerror(errno) << std::endl;
//         return false;
//     }
    
//     // Allow socket reuse
//     int opt = 1;
//     if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
//         std::cerr << "⚠️  Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
//     }
    
//     // Bind to address
//     sockaddr_in address{};
//     address.sin_family = AF_INET;
//     address.sin_addr.s_addr = INADDR_ANY;
//     address.sin_port = htons(m_port);
    
//     if (bind(m_serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
//         std::cerr << "❌ Failed to bind socket to port " << m_port << ": " << strerror(errno) << std::endl;
//         close(m_serverSocket);
//         m_serverSocket = -1;
//         return false;
//     }
    
//     // Start listening
//     if (listen(m_serverSocket, 5) < 0) {
//         std::cerr << "❌ Failed to listen on socket: " << strerror(errno) << std::endl;
//         close(m_serverSocket);
//         m_serverSocket = -1;
//         return false;
//     }
    
//     // Start server thread
//     m_running.store(true);
//     m_serverThread = std::make_unique<std::thread>(&NetworkBridge::serverLoop, this);
    
//     std::cout << "🚀 NetworkBridge started on port " << m_port << std::endl;
//     std::cout << "📡 Ready for ARM MacBook connections..." << std::endl;
//     return true;
// }

// void NetworkBridge::stop() {
//     if (!m_running.load()) return;
    
//     std::cout << "🛑 Stopping NetworkBridge..." << std::endl;
//     m_running.store(false);
    
//     if (m_serverSocket >= 0) {
//         close(m_serverSocket);
//         m_serverSocket = -1;
//     }
    
//     if (m_serverThread && m_serverThread->joinable()) {
//         m_serverThread->join();
//     }
    
//     std::cout << "✅ NetworkBridge stopped" << std::endl;
// }

// void NetworkBridge::serverLoop() {
//     std::cout << "🔄 Server loop started, waiting for connections..." << std::endl;
    
//     while (m_running.load()) {
//         sockaddr_in clientAddress{};
//         socklen_t clientLen = sizeof(clientAddress);
        
//         int clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddress, &clientLen);
//         if (clientSocket < 0) {
//             if (m_running.load()) {
//                 std::cerr << "⚠️  Accept failed: " << strerror(errno) << std::endl;
//             }
//             continue;
//         }
        
//         std::cout << "📱 Client connected from " << inet_ntoa(clientAddress.sin_addr) << std::endl;
        
//         // Handle client in current thread (could be made async for multiple clients)
//         handleClient(clientSocket);
//         close(clientSocket);
//     }
// }

// void NetworkBridge::handleClient(int clientSocket) {
//     char buffer[1024];
    
//     while (m_running.load()) {
//         // Read request
//         ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
//         if (bytesRead <= 0) {
//             if (bytesRead < 0) {
//                 std::cerr << "⚠️  Recv failed: " << strerror(errno) << std::endl;
//             }
//             break;
//         }
        
//         buffer[bytesRead] = '\0';
//         std::string requestStr(buffer);
        
//         std::cout << "📨 Received request: " << requestStr << std::endl;
        
//         // Parse request
//         NetworkResponse response;
//         try {
//             auto request = NetworkRequest::fromJson(requestStr);
            
//             // Route command to appropriate serializer
//             if (request.command == "minute_ring") {
//                 response.data = serializeMinuteRing();
//                 response.message = "Minute ring data";
//             }
//             else if (request.command == "candle_ring") {
//                 response.data = serializeCandleRing();
//                 response.message = "Candle ring data";
//             }
//             else if (request.command == "price_ring") {
//                 response.data = serializePriceRing();
//                 response.message = "Price ring data";
//             }
//             else if (request.command == "indicators") {
//                 response.data = serializeIndicators();
//                 response.message = "Technical indicators";
//             }
//             else if (request.command == "stats") {
//                 response.data = serializeStats();
//                 response.message = "Ring buffer statistics";
//             }
//             else if (request.command == "all") {
//                 nlohmann::json allData;
//                 allData["minute_ring"] = serializeMinuteRing();
//                 allData["candle_ring"] = serializeCandleRing();
//                 allData["price_ring"] = serializePriceRing();
//                 allData["indicators"] = serializeIndicators();
//                 allData["stats"] = serializeStats();
//                 response.data = allData;
//                 response.message = "All ring buffer data";
//             }
//             else {
//                 response.success = false;
//                 response.message = "Unknown command: " + request.command;
//             }
//         }
//         catch (const std::exception& e) {
//             response.success = false;
//             response.message = "Error processing request: " + std::string(e.what());
//         }
        
//         // Send response
//         std::string responseStr = response.toJson() + "\n";
//         ssize_t bytesSent = send(clientSocket, responseStr.c_str(), responseStr.length(), 0);
//         if (bytesSent < 0) {
//             std::cerr << "⚠️  Send failed: " << strerror(errno) << std::endl;
//             break;
//         }
        
//         std::cout << "📤 Sent response (" << bytesSent << " bytes)" << std::endl;
//     }
// }

// // ═══════════════════════════════════════════════════════════════════════════════
// // JSON SERIALIZERS FOR RING BUFFER DATA
// // ═══════════════════════════════════════════════════════════════════════════════

// nlohmann::json NetworkBridge::serializeMinuteRing() const {
//     const auto& minuteRing = m_buffer.getMinuteRing();
//     const auto& minuteIndices = m_buffer.getMinuteIndices();
    
//     nlohmann::json result = nlohmann::json::array();
    
//     for (size_t i = 0; i < minuteRing.size(); ++i) {
//         if (minuteIndices[i] != -1 && !minuteRing[i].isEmpty()) {
//             nlohmann::json slot;
//             slot["slot"] = i;
//             slot["minute"] = minuteIndices[i];
//             slot["candle"] = {
//                 {"open", minuteRing[i].open},
//                 {"high", minuteRing[i].high},
//                 {"low", minuteRing[i].low},
//                 {"close", minuteRing[i].close},
//                 {"volume", minuteRing[i].volume},
//                 {"empty", minuteRing[i].empty}
//             };
//             result.push_back(slot);
//         }
//     }
    
//     return result;
// }

// nlohmann::json NetworkBridge::serializeCandleRing() const {
//     const auto& candleRing = m_buffer.getCandleRing();
//     size_t head = m_buffer.getCandleRingHead();
//     size_t count = m_buffer.getCandleRingCount();
    
//     nlohmann::json result = nlohmann::json::array();
    
//     if (count > 0) {
//         // Return in chronological order
//         for (size_t i = 0; i < count; ++i) {
//             size_t idx = (head + candleRing.size() - count + i) % candleRing.size();
//             const auto& candle = candleRing[idx];
            
//             nlohmann::json candleData;
//             candleData["index"] = idx;
//             candleData["candle"] = {
//                 {"open", candle.open},
//                 {"high", candle.high},
//                 {"low", candle.low},
//                 {"close", candle.close},
//                 {"volume", candle.volume},
//                 {"timestamp", candle.timestamp}
//             };
//             result.push_back(candleData);
//         }
//     }
    
//     return result;
// }

// nlohmann::json NetworkBridge::serializePriceRing() const {
//     const auto& priceRing = m_buffer.getPriceRing();
//     size_t head = m_buffer.getPriceRingHead();
//     size_t count = m_buffer.getPriceRingCount();
    
//     nlohmann::json result = nlohmann::json::array();
    
//     if (count > 0) {
//         // Return in chronological order
//         for (size_t i = 0; i < count; ++i) {
//             size_t idx = (head + priceRing.size() - count + i) % priceRing.size();
//             result.push_back(priceRing[idx]);
//         }
//     }
    
//     return result;
// }

// nlohmann::json NetworkBridge::serializeIndicators() const {
//     auto indicators = m_buffer.calculateIndicators();
    
//     nlohmann::json result;
//     result["vwap"] = indicators.vwap;
//     result["rsi"] = indicators.rsi;
//     result["ema9"] = indicators.ema9;
//     result["ema26"] = indicators.ema26;
//     result["alma"] = indicators.alma;
//     result["atr"] = indicators.atr;
//     result["valid"] = indicators.isValid();
    
//     return result;
// }

// nlohmann::json NetworkBridge::serializeStats() const {
//     nlohmann::json result;
//     result["minute_ring_size"] = m_buffer.getMinuteRing().size();
//     result["window_minutes"] = m_buffer.getWindowMinutes();
//     result["candle_ring_head"] = m_buffer.getCandleRingHead();
//     result["candle_ring_count"] = m_buffer.getCandleRingCount();
//     result["price_ring_head"] = m_buffer.getPriceRingHead();
//     result["price_ring_count"] = m_buffer.getPriceRingCount();
    
//     return result;
// } 