// // NetworkBridge.cpp
// // ═══════════════════════════════════════════════════════════════════════════════
// // Production-ready network bridge for Candle Ring and STK_Q data
// // ═══════════════════════════════════════════════════════════════════════════════
// // This class provides a TCP server that listens on a specified port and 
// // handles client connections. It supports multiple clients concurrently and 
// // uses a thread pool to process requests.
// // ═══════════════════════════════════════════════════════════════════════════════
// // Author: @jason-c-huang

// #include "network_bridge.hpp"
// #include <iostream>
// #include <sstream>
// #include <cstring>
// #include <cerrno>
// #include <csignal>
// #include <cstdlib>
// #include <algorithm>
// #ifdef __unix__
// #include <sys/uio.h>
// #include <fcntl.h>
// #include <poll.h>
// #include <sys/resource.h>
// #else
// #error "POSIX platform required - this NetworkBridge implementation uses poll(), fcntl(), inet_ntop() and other Unix-specific APIs"
// #endif

// namespace {                    // helpers (unnamed namespace)
// constexpr int RECV_CHUNK   = 4096;
// }

// // Type alias for cleaner code
// using time_ordered_tick_buffer::Candle;

// // ═══════════════════════════════════════════════════════════════════════════════
// // PRODUCTION-READY TCP SERVER: Candle Ring + STK_Q Only
// // ═══════════════════════════════════════════════════════════════════════════════

// bool NetworkBridge::start() {
//     if (m_running.exchange(true)) {                 // already running?
//         std::cout << "⚠️  NetworkBridge already active on port " << m_port << '\n';
//         return false;
//     }

//     // Increase file descriptor limit for Docker containers (default 1024 is too low)
//     struct rlimit rl{65535, 65535};
//     if (::setrlimit(RLIMIT_NOFILE, &rl) < 0) {
//         perror("setrlimit RLIMIT_NOFILE");
//         std::cout << "⚠️  Failed to increase file descriptor limit. "
//                   << "Consider: docker run --ulimit nofile=65535:65535\n";
        
//         // Critical: Adapt configuration to current FD limit to prevent EMFILE
//         struct rlimit currentLimit;
//         if (::getrlimit(RLIMIT_NOFILE, &currentLimit) == 0) {
//             rlim_t softLimit = currentLimit.rlim_cur;
//             std::cout << "📊 Current FD soft limit: " << softLimit << std::endl;
            
//             // Reserve FDs for: server socket(1) + worker threads(m_workerPoolSize) + system overhead(20)
//             rlim_t reservedFDs = 1 + m_workerPoolSize + 20;
            
//             if (softLimit > reservedFDs) {
//                 rlim_t availableFDs = softLimit - reservedFDs;
                
//                 // Reduce MAX_QUEUE_SIZE if needed (leave some buffer)
//                 size_t adaptedQueueSize = std::min(static_cast<size_t>(availableFDs * 0.8), MAX_QUEUE_SIZE);
                
//                 if (adaptedQueueSize < MAX_QUEUE_SIZE) {
//                     std::cout << "🔧 Reducing queue size from " << MAX_QUEUE_SIZE 
//                              << " to " << adaptedQueueSize << " due to FD limit\n";
//                     // Note: We can't modify the const, but we'll use local variables for listen() backlog
//                 }
//             } else {
//                 std::cout << "🚫 WARNING: Very low FD limit (" << softLimit 
//                          << ") may cause connection failures\n";
//             }
//         }
//     }

//     std::signal(SIGPIPE, SIG_IGN);                  // avoid crash on send()

//     // Create dual-stack sockets for IPv4 and IPv6 support
//     bool ipv4Success = false, ipv6Success = false;
    
//     // IPv4 Socket Setup
//     m_serverSocketV4 = ::socket(AF_INET, SOCK_STREAM, 0);
//     if (m_serverSocketV4 >= 0) {
//         // Set FD_CLOEXEC and O_NONBLOCK for IPv4 socket
//         if (::fcntl(m_serverSocketV4, F_SETFD, FD_CLOEXEC) < 0) {
//             perror("fcntl FD_CLOEXEC IPv4");
//         }
//         int flags = ::fcntl(m_serverSocketV4, F_GETFL, 0);
//         if (flags >= 0) {
//             ::fcntl(m_serverSocketV4, F_SETFL, flags | O_NONBLOCK);
//         }
        
//         int opt = 1;
//         ::setsockopt(m_serverSocketV4, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
// #ifdef SO_REUSEPORT
//         ::setsockopt(m_serverSocketV4, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
// #endif
        
//         sockaddr_in addr4{};
//         addr4.sin_family = AF_INET;
//         addr4.sin_addr.s_addr = INADDR_ANY;
//         addr4.sin_port = htons(static_cast<uint16_t>(m_port));
        
//         if (::bind(m_serverSocketV4, reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4)) == 0) {
//             ipv4Success = true;
//             std::cout << "🌐 IPv4 socket bound to port " << m_port << std::endl;
//         } else {
//             perror("bind IPv4");
//             ::close(m_serverSocketV4);
//             m_serverSocketV4 = -1;
//         }
//     } else {
//         perror("socket IPv4");
//     }
    
//     // IPv6 Socket Setup  
//     m_serverSocketV6 = ::socket(AF_INET6, SOCK_STREAM, 0);
//     if (m_serverSocketV6 >= 0) {
//         // Set FD_CLOEXEC and O_NONBLOCK for IPv6 socket
//         if (::fcntl(m_serverSocketV6, F_SETFD, FD_CLOEXEC) < 0) {
//             perror("fcntl FD_CLOEXEC IPv6");
//         }
//         int flags = ::fcntl(m_serverSocketV6, F_GETFL, 0);
//         if (flags >= 0) {
//             ::fcntl(m_serverSocketV6, F_SETFL, flags | O_NONBLOCK);
//         }
        
//         int opt = 1;
//         ::setsockopt(m_serverSocketV6, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
// #ifdef SO_REUSEPORT  
//         ::setsockopt(m_serverSocketV6, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
// #endif
        
//         // Disable IPv4-mapped IPv6 addresses for cleaner dual-stack behavior
//         int ipv6only = 1;
//         ::setsockopt(m_serverSocketV6, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6only, sizeof(ipv6only));
        
//         sockaddr_in6 addr6{};
//         addr6.sin6_family = AF_INET6;
//         addr6.sin6_addr = IN6ADDR_ANY_INIT;
//         addr6.sin6_port = htons(static_cast<uint16_t>(m_port));
        
//         if (::bind(m_serverSocketV6, reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6)) == 0) {
//             ipv6Success = true;
//             std::cout << "🌐 IPv6 socket bound to port " << m_port << std::endl;
//         } else {
//             perror("bind IPv6");
//             ::close(m_serverSocketV6);
//             m_serverSocketV6 = -1;
//         }
//     } else {
//         perror("socket IPv6");
//     }
    
//     // Require at least one protocol to work
//     if (!ipv4Success && !ipv6Success) {
//         std::cout << "🚫 Failed to bind to both IPv4 and IPv6\n";
//         cleanupFD(); 
//         return false;
//     }
    
//     // Scale backlog with internal queue size, but adapt to FD limits if setrlimit() failed
//     int backlog = std::min(128, static_cast<int>(MAX_QUEUE_SIZE));
    
//     // Check if we need to reduce backlog due to FD constraints
//     struct rlimit currentLimit;
//     if (::getrlimit(RLIMIT_NOFILE, &currentLimit) == 0 && currentLimit.rlim_cur < 2048) {
//         // If soft limit is low (< 2048), reduce backlog to be more conservative
//         rlim_t conservativeBacklog = std::min(static_cast<rlim_t>(backlog), currentLimit.rlim_cur / 10);
//         backlog = static_cast<int>(std::max(conservativeBacklog, rlim_t{8})); // Min 8
//         std::cout << "🔧 Reduced listen backlog to " << backlog << " due to FD limit constraints\n";
//     }
    
//     // Start listening on both sockets
//     if (ipv4Success && ::listen(m_serverSocketV4, backlog) < 0) {
//         perror("listen IPv4"); 
//         ::close(m_serverSocketV4);
//         m_serverSocketV4 = -1;
//         ipv4Success = false;
//     }
    
//     if (ipv6Success && ::listen(m_serverSocketV6, backlog) < 0) {
//         perror("listen IPv6");
//         ::close(m_serverSocketV6); 
//         m_serverSocketV6 = -1;
//         ipv6Success = false;
//     }
    
//     // Require at least one protocol to still work after listen()
//     if (!ipv4Success && !ipv6Success) {
//         std::cout << "🚫 Failed to listen on both IPv4 and IPv6\n";
//         cleanupFD();
//         return false;
//     }

//     // Start worker pool with exception safety
//     try {
//         startWorkerPool();
        
//         m_serverThread = std::make_unique<std::jthread>(
//             &NetworkBridge::serverLoop, this);
//     } catch (...) {
//         // Clean up on failure to maintain consistent state
//         cleanupFD();
//         throw;  // Re-throw original exception
//     }

//     std::cout << "🚀 NetworkBridge listening on :"
//               << m_port << " with " << m_workerPoolSize << " workers\n";
//     return true;
// }

// void NetworkBridge::stop() {
//     if (!m_running.exchange(false)) return;

//     std::cout << "🛑 Stopping NetworkBridge …\n";
    
//     // Close IPv4 socket
//     if (m_serverSocketV4 >= 0) {
//         ::shutdown(m_serverSocketV4, SHUT_RDWR);
//         ::close(m_serverSocketV4);
//         m_serverSocketV4 = -1;
//     }
    
//     // Close IPv6 socket  
//     if (m_serverSocketV6 >= 0) {
//         ::shutdown(m_serverSocketV6, SHUT_RDWR);
//         ::close(m_serverSocketV6);
//         m_serverSocketV6 = -1;
//     }
    
//     // Stop and release server thread (std::jthread auto-joins in destructor)
//     m_serverThread.reset();
    
//     // Stop worker pool
//     stopWorkerPool();
// }

// void NetworkBridge::serverLoop() {
//     while (m_running.load()) {
//         // Use poll() with timeout for responsive shutdown - monitor both IPv4 and IPv6 sockets
//         pollfd pfds[2];
//         int nfds = 0;
        
//         // Add IPv4 socket if available
//         if (m_serverSocketV4 >= 0) {
//             pfds[nfds].fd = m_serverSocketV4;
//             pfds[nfds].events = POLLIN;
//             pfds[nfds].revents = 0;
//             nfds++;
//         }
        
//         // Add IPv6 socket if available  
//         if (m_serverSocketV6 >= 0) {
//             pfds[nfds].fd = m_serverSocketV6;
//             pfds[nfds].events = POLLIN;
//             pfds[nfds].revents = 0;
//             nfds++;
//         }
        
//         if (nfds == 0) {
//             std::cout << "🚫 No server sockets available, exiting server loop\n";
//             break;
//         }
        
//         int pollResult = ::poll(pfds, nfds, 50);  // 50ms timeout
        
//         if (pollResult <= 0) {
//             if (pollResult < 0 && errno != EINTR) {
//                 perror("poll");
//             }
//             continue;  // Timeout or error - check m_running again
//         }
        
//         // Check each socket for activity
//         for (int i = 0; i < nfds; ++i) {
//             if (!(pfds[i].revents & POLLIN)) {
//                 // Check for socket errors
//                 if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
//                     std::cout << "🚫 Server socket error on fd=" << pfds[i].fd 
//                              << " (revents=" << pfds[i].revents << ")\n";
//                     // Close the problematic socket but continue with others
//                     if (pfds[i].fd == m_serverSocketV4) {
//                         ::close(m_serverSocketV4);
//                         m_serverSocketV4 = -1;
//                     } else if (pfds[i].fd == m_serverSocketV6) {
//                         ::close(m_serverSocketV6);
//                         m_serverSocketV6 = -1;
//                     }
                    
//                     // Critical fix: If both sockets are now closed, stop the server
//                     if (m_serverSocketV4 < 0 && m_serverSocketV6 < 0) {
//                         std::cout << "🚫 All server sockets failed - stopping NetworkBridge\n";
//                         m_running.store(false);  // Signal shutdown to prevent silent outage
//                         return;  // Exit serverLoop immediately
//                     }
//                 }
//                 continue;  // No incoming connection on this socket
//             }
            
//             // Accept connection - use sockaddr_storage for both IPv4 and IPv6
//             sockaddr_storage cliAddr{};
//             socklen_t cliAddrLen = sizeof(cliAddr);
//             int cliSock = ::accept(pfds[i].fd, reinterpret_cast<sockaddr*>(&cliAddr), &cliAddrLen);
            
//             if (cliSock < 0) {
//                 if (errno == EBADF || errno == EINTR) break;   // shutting down
//                 if (errno == EAGAIN || errno == EWOULDBLOCK) continue; // non-blocking, no connection ready
//                 perror("accept"); 
//                 continue;
//             }

//             // Set FD_CLOEXEC on client socket to prevent leaks on fork/exec
//             if (::fcntl(cliSock, F_SETFD, FD_CLOEXEC) < 0) {
//                 perror("fcntl FD_CLOEXEC client");
//             }

//             // Add socket to worker queue with DoS protection (adapt to FD limits)
//             {
//                 std::lock_guard<std::mutex> lock(m_queueMutex);
                
//                 // Determine effective queue limit based on current FD availability
//                 size_t effectiveQueueLimit = MAX_QUEUE_SIZE;
//                 struct rlimit currentLimit;
//                 if (::getrlimit(RLIMIT_NOFILE, &currentLimit) == 0 && currentLimit.rlim_cur < 2048) {
//                     rlim_t reservedFDs = 2 + m_workerPoolSize + 20; // IPv4+IPv6 sockets + workers + overhead
//                     if (currentLimit.rlim_cur > reservedFDs) {
//                         effectiveQueueLimit = std::min(MAX_QUEUE_SIZE, 
//                             static_cast<size_t>((currentLimit.rlim_cur - reservedFDs) * 0.8));
//                     }
//                 }
                
//                 if (m_socketQueue.size() >= effectiveQueueLimit) {
//                     // Queue full - close connection immediately to prevent DoS/EMFILE
//                     // Extract IP address from sockaddr_storage (works for both IPv4 and IPv6)
//                     char ipBuf[INET6_ADDRSTRLEN];
//                     std::string ip = "unknown";
//                     if (cliAddr.ss_family == AF_INET) {
//                         sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&cliAddr);
//                         if (::inet_ntop(AF_INET, &addr4->sin_addr, ipBuf, sizeof(ipBuf))) {
//                             ip = ipBuf;
//                         }
//                     } else if (cliAddr.ss_family == AF_INET6) {
//                         sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&cliAddr);
//                         if (::inet_ntop(AF_INET6, &addr6->sin6_addr, ipBuf, sizeof(ipBuf))) {
//                             ip = ipBuf;
//                         }
//                     }
                    
//                     size_t currentSize = m_socketQueue.size();
//                     std::cout << "🚫 ["<<ip<<"] queue full (" << currentSize 
//                              << "/" << effectiveQueueLimit << "), dropping connection\n";
//                     ::close(cliSock);
//                     continue;
//                 }
//                 m_socketQueue.emplace(cliSock, cliAddr);
//             }
//             m_workAvailable.notify_one();
//         }
//     }
// }

// // ----- safeSend helper (handles short write + EINTR) -----
// bool safeSend(int fd, const char* buf, size_t len) {
//     while (len) {
//         ssize_t s = ::send(fd, buf, len, MSG_NOSIGNAL);
//         if (s <= 0) {  // Critical fix: treat 0 as error (peer half-closed)
//             if (s < 0 && errno == EINTR) continue;
//             if (s < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
//                 std::this_thread::sleep_for(std::chrono::milliseconds(1));
//                 continue;
//             }
//             return false;  // s == 0 (peer closed) or real error
//         }
//         buf += s; len -= s;
//     }
//     return true;
// }

// void NetworkBridge::handleClient(int sock, sockaddr_storage cli) {
//     // RAII socket cleanup - ensures close() on all exit paths (exception, return, break)
//     // Guard against double-close by checking socket validity
//     auto closeFd = [sock]{ 
//         if (sock >= 0) {
//             ::close(sock); 
//         }
//     };
//     std::unique_ptr<void, decltype(closeFd)> socketCloser(reinterpret_cast<void*>(1), closeFd);
    
//     // Optimize socket for performance and large transfers
//     int opt=1; ::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,&opt,sizeof(opt));
//     tuneSocketBuffers(sock);

//     // Extract IP address from sockaddr_storage (works for both IPv4 and IPv6)
//     char ipBuf[INET6_ADDRSTRLEN];
//     std::string ip = "unknown";
//     if (cli.ss_family == AF_INET) {
//         sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&cli);
//         if (::inet_ntop(AF_INET, &addr4->sin_addr, ipBuf, sizeof(ipBuf))) {
//             ip = ipBuf;
//         }
//     } else if (cli.ss_family == AF_INET6) {
//         sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&cli);
//         if (::inet_ntop(AF_INET6, &addr6->sin6_addr, ipBuf, sizeof(ipBuf))) {
//             ip = ipBuf;
//         }
//     }
//     std::cout << "📡 ["<<ip<<"] connected\n";

//     // Critical: Set absolute deadline for entire request to prevent slow-loris attacks
//     // Client must complete their request within this time, regardless of tiny reads
//     const auto requestDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(RECV_TIMEOUT_SEC);

//     std::string inBuf;
//     char  tmp[RECV_CHUNK];

//     while (m_running.load()) {
//         // Check absolute deadline first - prevents slow-loris holding workers indefinitely
//         if (std::chrono::steady_clock::now() > requestDeadline) {
//             std::cout << "🚫 ["<<ip<<"] request deadline exceeded (" << RECV_TIMEOUT_SEC 
//                      << "s), closing to prevent slow-loris\n";
//             break;
//         }
//         ssize_t n = ::recv(sock,tmp,sizeof(tmp),0);
//         if (n < 0) {
//             if (errno == EINTR) continue;
//             if (errno == EAGAIN || errno == EWOULDBLOCK) break; // idle timeout
//             break; // real error
//         }
//         if (n == 0) break; // connection closed
        
//         // Memory exhaustion protection - check BEFORE appending
//         if (inBuf.size() + n > MAX_INPUT_BUFFER) {
//             std::cout << "🚫 ["<<ip<<"] REQUEST_TOO_LARGE (" 
//                      << inBuf.size() << " + " << n << " > " << MAX_INPUT_BUFFER << "), closing\n";
//             break;  // Close connection
//         }
        
//         inBuf.append(tmp, n);

//         // framing: newline-delimited JSON
//         size_t pos;
//         while ((pos=inBuf.find('\n')) != std::string::npos) {
//             std::string oneMsg = inBuf.substr(0,pos);
//             inBuf.erase(0,pos+1);

//             // Flow control: Reject oversized individual messages
//             if (oneMsg.size() > 256 * 1024) {
//                 std::cout << "🚫 ["<<ip<<"] MESSAGE_TOO_LARGE (" 
//                          << oneMsg.size() << " > 256KB), skipping\n";
//                 continue;  // Skip this message, continue processing
//             }

//             NetworkResponse r;
//             try {
//                 auto req = NetworkRequest::fromJson(oneMsg);
//                 if      (req.command=="candle_ring") {
//                     r.data = serializeCandleRing();
//                     r.message = r.data.empty() ? "No completed candles available yet" 
//                                                : "Candle ring data (completed 1-min candles)";
//                 }
//                 else if (req.command=="candle_stats") {
//                     r.data = serializeCandleRingStats();
//                     r.message = "Candle ring statistics";
//                 }
//                 else if (req.command=="stk_q_stats") {
//                     r.data = serializeSTK_Q_Stats();
//                     r.message = "STK_Q statistics";
//                 }
//                 else if (req.command=="stk_q_latest") {
//                     r.data = serializeSTK_Q_Latest(req.n);
//                     r.message = r.data.empty() ? "No tick data available" 
//                                                : "Latest " + std::to_string(r.data.size()) + " tick(s)";
//                 }
//                 else  { r.success=false; r.message="unknown command"; }
//             }
//             catch (const std::exception& e){ r.success=false; r.message=e.what(); }

//             std::string json = r.toJson(); 
//             json.push_back('\n');
            
//             // Use optimized sending for large responses
//             if (json.size() > LARGE_RESPONSE_THRESHOLD) {
//                 if (!sendLargeResponse(sock, json)) {
//                     perror("sendLargeResponse"); 
//                     ::shutdown(sock, SHUT_RDWR);
//                     break;
//                 }
//             } else {
//                 if (!safeSend(sock, json.data(), json.size())) {
//                     ::shutdown(sock, SHUT_RDWR);
//                     break;
//                 }
//             }
//         }
//     }
//     std::cout << "🔌 ["<<ip<<"] disconnected\n";
//     // Socket automatically closed by RAII socketCloser destructor
// }

// void NetworkBridge::cleanupFD(){
//     if (m_serverSocketV4 >= 0) { 
//         ::close(m_serverSocketV4); 
//         m_serverSocketV4 = -1; 
//     }
//     if (m_serverSocketV6 >= 0) { 
//         ::close(m_serverSocketV6); 
//         m_serverSocketV6 = -1; 
//     }
//     m_running.store(false);
// }

// void NetworkBridge::startWorkerPool() {
//     m_workerPool.reserve(m_workerPoolSize);
//     for (size_t i = 0; i < m_workerPoolSize; ++i) {
//         m_workerPool.emplace_back(&NetworkBridge::workerLoop, this);
//     }
//     std::cout << "🧵 Started " << m_workerPoolSize << " worker threads\n";
// }

// void NetworkBridge::stopWorkerPool() {
//     // First: Close any remaining sockets in queue (prevent workers from picking up new work)
//     {
//         std::lock_guard<std::mutex> lock(m_queueMutex);
//         while (!m_socketQueue.empty()) {
//             ::close(m_socketQueue.front().first);
//             m_socketQueue.pop();
//         }
//     }
    
//     // Second: Request stop for all worker threads (triggers stop tokens)
//     for (auto& worker : m_workerPool) {
//         worker.request_stop();
//     }
    
//     // Third: Wake all workers to check stop tokens (they'll find empty queue and exit)
//     m_workAvailable.notify_all();
    
//     // Fourth: std::jthread automatically joins in destructor when vector is cleared
//     m_workerPool.clear();
    
//     std::cout << "✅ All worker threads stopped\n";
// }

// void NetworkBridge::workerLoop(std::stop_token stopToken) {
//     while (!stopToken.stop_requested()) {
//         std::pair<int, sockaddr_storage> work;
        
//         // Wait for work or stop signal
//         {
//             std::unique_lock<std::mutex> lock(m_queueMutex);
//             m_workAvailable.wait(lock, [this, &stopToken] { 
//                 return !m_socketQueue.empty() || stopToken.stop_requested(); 
//             });
            
//             if (stopToken.stop_requested()) break;
            
//             if (m_socketQueue.empty()) continue;
            
//             work = m_socketQueue.front();
//             m_socketQueue.pop();
//         }
        
//         // Handle the client connection
//         handleClient(work.first, work.second);
//     }
// }

// void NetworkBridge::tuneSocketBuffers(int socket) {
//     // Increase send buffer for large responses (candle rings, historical data)
//     // Note: Linux kernel may multiply by 2 for internal overhead/metadata
//     int sndbuf = static_cast<int>(SEND_BUFFER_SIZE);
//     if (::setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
//         perror("setsockopt SO_SNDBUF");
//     }
    
//     // Increase receive buffer for large requests
//     // Note: Linux kernel may multiply by 2 for internal overhead/metadata
//     int rcvbuf = static_cast<int>(RECV_BUFFER_SIZE);
//     if (::setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
//         perror("setsockopt SO_RCVBUF");
//     }
    
//     // Set receive timeout to prevent worker pool exhaustion
//     // Note: Idle connections that never send data will hold worker threads 
//     // for up to RECV_TIMEOUT_SEC (30s) - this is acceptable behavior
//     struct timeval timeout;
//     timeout.tv_sec = RECV_TIMEOUT_SEC;
//     timeout.tv_usec = 0;
//     if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
//         perror("setsockopt SO_RCVTIMEO");
//     }
// }

// bool NetworkBridge::sendLargeResponse(int socket, const std::string& response) {
//     const char* data = response.data();
//     size_t remaining = response.size();
//     size_t totalSent = 0;
    
//     // Configurable deadline for large response transmission (default 30s for production)
//     // Can be overridden via BRIDGE_SEND_DEADLINE_MS environment variable
//     static const auto deadlineMs = []() {
//         if (const char* envDeadline = ::getenv("BRIDGE_SEND_DEADLINE_MS")) {
//             int ms = std::atoi(envDeadline);
//             if (ms > 0 && ms <= 300000) {  // Sanity bounds: 1ms to 5 minutes
//                 std::cout << "🔧 Using BRIDGE_SEND_DEADLINE_MS=" << ms << "ms\n";
//                 return ms;
//             }
//         }
//         return 30000;  // Default 30 seconds (was 5s - too aggressive for CPU-throttled containers)
//     }();
    
//     const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadlineMs);
    
//     // Critical DoS protection: Track consecutive short/zero writes to detect slow-loris on output
//     // Client ACKing 1 byte per RTT can hold worker for entire deadline - abort early on no progress
//     int noProgressCounter = 0;
//     static constexpr int MAX_NO_PROGRESS = 15;  // Abort after 15 consecutive short/zero writes
    
//     // Send in chunks using vectorized I/O for efficiency
//     while (remaining > 0 && m_running.load()) {
//         // Check deadline
//         if (std::chrono::steady_clock::now() > deadline) {
//             return false;  // Timeout exceeded
//         }
        
//         // Critical: Check no-progress counter to prevent slow-loris DoS on output
//         if (noProgressCounter >= MAX_NO_PROGRESS) {
//             std::cout << "🚫 sendLargeResponse: Aborting after " << MAX_NO_PROGRESS 
//                      << " consecutive short writes (slow-loris protection)\n";
//             return false;  // Abort to prevent worker thread starvation
//         }
        
//         size_t chunkSize = std::min(remaining, CHUNK_SIZE);
        
//         // Prepare iovec for this chunk
//         struct iovec iov;
//         iov.iov_base = const_cast<char*>(data + totalSent);
//         iov.iov_len = chunkSize;
        
//         struct msghdr msg = {};
//         msg.msg_iov = &iov;
//         msg.msg_iovlen = 1;
        
//         ssize_t sent = ::sendmsg(socket, &msg, MSG_NOSIGNAL);
//         if (sent <= 0) {
//             if (sent < 0 && errno == EINTR) continue;
//             if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
//                 // Socket buffer full - brief pause then retry
//                 noProgressCounter++;  // Count as no progress
//                 std::this_thread::sleep_for(std::chrono::milliseconds(1));
//                 continue;
//             }
//             if (sent == 0) {
//                 // No bytes sent but no error - socket might be busy, retry
//                 noProgressCounter++;  // Count as no progress
//                 std::this_thread::sleep_for(std::chrono::milliseconds(1));
//                 continue;
//             }
//             return false;  // Real error
//         }
        
//         // Check for insufficient progress (slow-loris: client ACKs tiny amounts)
//         if (sent < static_cast<ssize_t>(chunkSize / 4)) {  // Less than 25% of chunk sent
//             noProgressCounter++;
//         } else {
//             noProgressCounter = 0;  // Reset counter on good progress
//         }
        
//         totalSent += static_cast<size_t>(sent);
//         remaining -= static_cast<size_t>(sent);
        
//         // Brief yield to prevent monopolizing CPU on large transfers
//         if (totalSent % (CHUNK_SIZE * 4) == 0) {
//             std::this_thread::yield();
//         }
//     }
    
//     return remaining == 0;  // Success if all data sent
// }

// size_t NetworkBridge::determineWorkerPoolSize(size_t requested) const {
//     if (requested > 0) {
//         return requested;  // Explicit override
//     }
    
//     // Check environment variable
//     if (const char* envWorkers = ::getenv("NETWORK_BRIDGE_WORKERS")) {
//         size_t envSize = std::stoul(envWorkers);
//         if (envSize > 0 && envSize <= 128) {  // Sanity bounds
//             std::cout << "🔧 Using NETWORK_BRIDGE_WORKERS=" << envSize << "\n";
//             return envSize;
//         }
//     }
    
//     // Auto-detect based on hardware
//     size_t hwConcurrency = std::thread::hardware_concurrency();
//     if (hwConcurrency > 0) {
//         // For I/O bound tasks: use 2x cores (up to reasonable limit)
//         size_t autoSize = std::min(hwConcurrency * 2, size_t{32});
//         autoSize = std::max(autoSize, DEFAULT_WORKER_POOL_SIZE);  // Min 8
//         std::cout << "🔧 Auto-detected " << autoSize << " workers (" 
//                   << hwConcurrency << " cores)\n";
//         return autoSize;
//     }
    
//     // Fallback to default
//     return DEFAULT_WORKER_POOL_SIZE;
// }

// // ═══════════════════════════════════════════════════════════════════════════════
// // SIMPLIFIED JSON SERIALIZERS (Match Python Bindings)
// // ═══════════════════════════════════════════════════════════════════════════════

// nlohmann::json NetworkBridge::serializeCandleRing() const {
//     // THREAD SAFETY: TimeOrderedTickBuffer::getCandleRingData() is internally synchronized
//     // - Uses std::lock_guard<std::mutex> m_mutex for all data access
//     // - Returns a COPY of candles vector (not reference) = safe for concurrent access
//     // - Producer thread can write while we serialize without data corruption
//     auto candles = m_buffer.getCandleRingData();
    
//     nlohmann::json result = nlohmann::json::array();
    
//     // Candles are already in chronological order (oldest → newest)
//     for (size_t i = 0; i < candles.size(); ++i) {
//         const auto& candle = candles[i];
        
//         nlohmann::json candleData;
//         candleData["index"] = i;  // Sequential index in result
//         candleData["candle"] = {
//             {"open", candle.open},
//             {"high", candle.high},
//             {"low", candle.low},
//             {"close", candle.close},
//             {"volume", candle.volume},
//             {"timestamp", candle.timestamp}
//         };
//         result.push_back(candleData);
//     }
    
//     return result;
// }

// nlohmann::json NetworkBridge::serializeCandleRingStats() const {
//     // THREAD SAFETY: All TimeOrderedTickBuffer getters are internally synchronized
//     // - getCandleRingHead(), getCandleRingCount(), getWindowMinutes() use std::lock_guard<std::mutex>
//     // - Each call is atomic, returns snapshot values safe for serialization
//     // - No external synchronization needed in NetworkBridge
//     nlohmann::json result;
//     result["candle_ring_head"] = m_buffer.getCandleRingHead();
//     result["candle_ring_count"] = m_buffer.getCandleRingCount();
//     result["window_minutes"] = m_buffer.getWindowMinutes();
    
//     return result;
// }

// nlohmann::json NetworkBridge::serializeSTK_Q_Stats() const {
//     // THREAD SAFETY: All STK_Q methods are internally synchronized with std::mutex
//     // - size(), empty(), peek(), peekLatest() use std::lock_guard<std::mutex> m_mutex
//     // - Methods return copies of data, safe for concurrent producer/consumer access
//     // - No external synchronization needed in NetworkBridge
//     nlohmann::json result;
//     result["total_ticks"] = m_stkq.size();
//     result["is_empty"] = m_stkq.empty();
    
//     // Get time range if data exists - both peek methods are thread-safe
//     stk_q::STK_Q_Data oldest, newest;
//     if (m_stkq.peek(oldest) && m_stkq.peekLatest(newest)) {
//         result["oldest_timestamp"] = oldest.time;
//         result["newest_timestamp"] = newest.time;
//         result["time_span_ms"] = newest.time - oldest.time;
//     }
    
//     return result;
// }

// nlohmann::json NetworkBridge::serializeSTK_Q_Latest(size_t n) const {
//     // THREAD SAFETY: STK_Q::peekLatest() is internally synchronized with std::mutex
//     // - Uses std::lock_guard<std::mutex> m_mutex for safe data access
//     // - Returns a COPY of STK_Q_Data struct (not reference) = safe for serialization
//     // - Producer thread can add ticks while we serialize without data corruption
//     nlohmann::json result = nlohmann::json::array();
    
//     if (n == 0) return result;  // Early return for invalid input
    
//     // Explicit error for bulk retrieval requests instead of silent truncation
//     if (n > 1) {
//         return {{"error", "STK_Q bulk retrieval not implemented - only n=1 supported"}};
//     }
    
//     // STK_Q doesn't support bulk retrieval - only has peek/peekLatest for single items
//     // TODO: Enhance STK_Q to support getLatestN() method for bulk retrieval
    
//     stk_q::STK_Q_Data latestData;
//     if (m_stkq.peekLatest(latestData)) {
//         nlohmann::json tickData;
//         tickData["symbol"] = latestData.symbol;
//         tickData["time"] = latestData.time;
//         tickData["last"] = latestData.last;
//         tickData["bid"] = latestData.bid;
//         tickData["ask"] = latestData.ask;
//         tickData["volume"] = latestData.volume;
//         tickData["bidSize"] = latestData.bidSize;
//         tickData["askSize"] = latestData.askSize;
//         tickData["vwap"] = latestData.vwap;
//         tickData["exchange"] = latestData.exchange;
//         tickData["mid"] = latestData.mid;
//         tickData["spread"] = latestData.spread;
//         tickData["spreadPercent"] = latestData.spreadPercent;
//         tickData["imbalance"] = latestData.imbalance;
//         tickData["rsi"] = latestData.rsi;
//         tickData["ema9"] = latestData.ema9;
//         tickData["ema26"] = latestData.ema26;
//         tickData["alma"] = latestData.alma;
//         tickData["atr"] = latestData.atr;
//         result.push_back(tickData);
//     }
    
//     return result;
// } 