// // NetworkBridge.hpp
// // ═══════════════════════════════════════════════════════════════════════════════
// // Production-ready network bridge for Candle Ring and STK_Q data
// // ═══════════════════════════════════════════════════════════════════════════════
// // This class provides a TCP server that listens on a specified port and 
// // handles client connections. It supports multiple clients concurrently and 
// // uses a thread pool to process requests.
// // ═══════════════════════════════════════════════════════════════════════════════
// // Author: @jason-c-huang

// #pragma once
// #include <string>
// #include <thread>
// #include <atomic>
// #include <memory>
// #include <vector>
// #include <mutex>
// #include <condition_variable>
// #include <queue>
// #ifdef __unix__
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <netinet/tcp.h>
// #include <arpa/inet.h>
// #include <unistd.h>
// #else
// #error "POSIX platform required - NetworkBridge uses Unix-specific networking APIs"
// #endif
// #include <nlohmann/json.hpp>
// #include "../../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"
// #include "../../util/stk_q/stk_q.hpp"

// // ═══════════════════════════════════════════════════════════════════════════════
// // PRODUCTION-READY NETWORK BRIDGE: Candle Ring + STK_Q Only
// // ═══════════════════════════════════════════════════════════════════════════════

// class NetworkBridge {
// public:
//     explicit NetworkBridge(time_ordered_tick_buffer::TimeOrderedTickBuffer& buffer, 
//                           stk_q::STK_Q& stkq, 
//                           int port = 8080,
//                           size_t workerPoolSize = 0)  // 0 = auto-detect
//         : m_buffer(buffer), m_stkq(stkq), m_port(port), m_running(false) {
//         m_workerPoolSize = determineWorkerPoolSize(workerPoolSize);
//     }
    
//     ~NetworkBridge() noexcept { 
//         try { stop(); } catch (...) { /* destructor must not throw */ }
//     }
    
//     // Start the TCP server (non-blocking)
//     bool start();
    
//     // Stop the TCP server
//     void stop();
    
//     // Check if server is running
//     bool isRunning() const { return m_running.load(); }

// private:
//     void serverLoop();
//     void workerLoop(std::stop_token stopToken);
//     void handleClient(int clientSocket, sockaddr_storage clientAddr);
//     void cleanupFD();
//     void startWorkerPool();
//     void stopWorkerPool();
//     void tuneSocketBuffers(int socket);
//     bool sendLargeResponse(int socket, const std::string& response);
//     size_t determineWorkerPoolSize(size_t requested) const;
    
//     // JSON serializers (match bindings)
//     nlohmann::json serializeCandleRing() const;
//     nlohmann::json serializeCandleRingStats() const;
//     nlohmann::json serializeSTK_Q_Stats() const;
//     nlohmann::json serializeSTK_Q_Latest(size_t n = 1) const;
    
//     time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buffer;
//     stk_q::STK_Q& m_stkq;
//     int m_port;
//     int m_serverSocketV4 = -1;  // IPv4 socket
//     int m_serverSocketV6 = -1;  // IPv6 socket  
//     std::atomic<bool> m_running;
//     std::unique_ptr<std::jthread> m_serverThread;
    
//     // Thread pool for client connections
//     static constexpr size_t DEFAULT_WORKER_POOL_SIZE = 8;
//     static constexpr size_t MAX_QUEUE_SIZE = 512;  // DoS protection
//     size_t m_workerPoolSize;
//     std::vector<std::jthread> m_workerPool;
//     std::queue<std::pair<int, sockaddr_storage>> m_socketQueue;
//     std::mutex m_queueMutex;
//     std::condition_variable m_workAvailable;
    
//     // Back-pressure handling constants
//     static constexpr size_t LARGE_RESPONSE_THRESHOLD = 64 * 1024;  // 64KB
//     static constexpr size_t SEND_BUFFER_SIZE = 256 * 1024;        // 256KB
//     static constexpr size_t RECV_BUFFER_SIZE = 64 * 1024;         // 64KB
//     static constexpr size_t CHUNK_SIZE = 32 * 1024;               // 32KB chunks
//     static constexpr int RECV_TIMEOUT_SEC = 30;                   // 30s recv timeout
//     static constexpr size_t MAX_INPUT_BUFFER = 1024 * 1024;       // 1MB input buffer limit
// };

// // ═══════════════════════════════════════════════════════════════════════════════
// // JSON PROTOCOL (Newline-Delimited)
// // ═══════════════════════════════════════════════════════════════════════════════

// struct NetworkRequest {
//     std::string command;  // "candle_ring", "candle_stats", "stk_q_stats", "stk_q_latest"
//     size_t n = 1;         // For stk_q_latest: number of ticks to return
    
//     static NetworkRequest fromJson(const std::string& jsonStr) {
//         // Critical DoS protection: Depth-limited JSON parsing to prevent recursion bombs
//         // Small payloads like "[[[[[[" repeated can cause unbounded recursion → std::bad_alloc → SIGABRT
//         static constexpr int DEPTH_LIMIT = 64;
//         auto j = nlohmann::json::parse(jsonStr, nullptr,
//                                    /*allow_exceptions=*/false,
//                                    /*ignore_comments=*/false,
//                                    DEPTH_LIMIT);
//         if (j.is_discarded()) {
//             throw std::invalid_argument("Invalid JSON format or recursion depth exceeded");
//         }
        
//         NetworkRequest req;
//         // Safe access with exception handling
//         try {
//             req.command = j.value("command", "candle_ring");
//             req.n = j.value("n", 1);
//         } catch (...) {
//             throw std::invalid_argument("Invalid JSON structure");
//         }
//         return req;
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