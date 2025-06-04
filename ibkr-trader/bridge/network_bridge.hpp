// #pragma once
// #include <string>
// #include <thread>
// #include <atomic>
// #include <memory>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #include <nlohmann/json.hpp>
// #include "../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"

// // ═══════════════════════════════════════════════════════════════════════════════
// // NETWORK BRIDGE: x86 Container ↔ ARM MacBook
// // ═══════════════════════════════════════════════════════════════════════════════

// class NetworkBridge {
// public:
//     explicit NetworkBridge(time_ordered_tick_buffer::TimeOrderedTickBuffer& buffer, int port = 8080)
//         : m_buffer(buffer), m_port(port), m_running(false) {}
    
//     ~NetworkBridge() { stop(); }
    
//     // Start the TCP server (non-blocking)
//     bool start();
    
//     // Stop the TCP server
//     void stop();
    
//     // Check if server is running
//     bool isRunning() const { return m_running.load(); }

// private:
//     void serverLoop();
//     void handleClient(int clientSocket);
    
//     // JSON serializers for ring buffer data
//     nlohmann::json serializeMinuteRing() const;
//     nlohmann::json serializeCandleRing() const;
//     nlohmann::json serializePriceRing() const;
//     nlohmann::json serializeIndicators() const;
//     nlohmann::json serializeStats() const;
    
//     time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buffer;
//     int m_port;
//     int m_serverSocket = -1;
//     std::atomic<bool> m_running;
//     std::unique_ptr<std::thread> m_serverThread;
// };

// // ═══════════════════════════════════════════════════════════════════════════════
// // ULTRA-FAST JSON PROTOCOL
// // ═══════════════════════════════════════════════════════════════════════════════

// struct NetworkRequest {
//     std::string command;  // "minute_ring", "candle_ring", "price_ring", "indicators", "stats", "all"
    
//     static NetworkRequest fromJson(const std::string& jsonStr) {
//         auto j = nlohmann::json::parse(jsonStr);
//         return {j.value("command", "stats")};
//     }
// };

// struct NetworkResponse {
//     bool success = true;
//     std::string message;
//     nlohmann::json data;
    
//     std::string toJson() const {
//         nlohmann::json j;
//         j["success"] = success;
//         j["message"] = message;
//         j["data"] = data;
//         return j.dump();
//     }
// }; 