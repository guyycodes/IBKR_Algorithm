#include "connection.hpp"
#include <iostream>
#include <chrono>

namespace connection {

    // Define connection constants
    const char* HOST = "host.docker.internal";
    // Paper Trading port is 4002, Live Trading would be 7496
    // We're using Paper Trading for testing
    int PORT = 4002;  // Paper Trading port
    const int client_id = 0;
    
    // Constructor implementation
    IBKRTrader::IBKRTrader() 
        : m_osSignal(2000)
        , m_client(new EClientSocket(this, &m_osSignal))
    {
    }
    
    // Destructor implementation
    IBKRTrader::~IBKRTrader() {
        delete m_client;
    }
    
    // Connect to IBKR Gateway
    bool IBKRTrader::connect() {
        bool success = m_client->eConnect(HOST, PORT, client_id, /*extraAuth=*/false);
        if (success) {
            std::cout << "[INFO] Connection initiated to " << HOST << ":" << PORT << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to connect to IBKR." << std::endl;
        }
        return success;
    }
    
    // Disconnect from IBKR Gateway
    void IBKRTrader::disconnect() {
        m_client->eDisconnect();
        std::cout << "[INFO] Disconnected from IBKR.\n";
    }
    
    //  createReader() is responsible for making a reader for retrieving messages from the IBKR connection, std::unique_ptr<EReader> (smart pointer that automatically manages memory
    std::unique_ptr<EReader> IBKRTrader::createReader() {
        std::unique_ptr<EReader> reader(new EReader(m_client, &m_osSignal));
        reader->start();
        return reader;
    }
    
    // Start message processing in a separate thread
    std::thread IBKRTrader::startMessageProcessing(std::unique_ptr<EReader>& reader) {
        return std::thread([this, &reader]() {
            while (m_client->isConnected()) {
                m_osSignal.waitForSignal(); // waits for a notification that messages are available
                reader->processMsgs(); // processes any received messages
            }
        });
    }
    
    // Send a ping and return the request ID
    int IBKRTrader::sendPing() {
        std::lock_guard<std::mutex> lock(m_pingMutex);
        int pingId = m_nextPingId++;
        
        // Record the time we sent the request
        m_pingRequests[pingId] = std::chrono::high_resolution_clock::now();
        
        // Use reqCurrentTime as our ping mechanism
        m_client->reqCurrentTime();
        
        return pingId;
    }
    
    // Measure average latency over multiple pings
    double IBKRTrader::measureAverageLatency(int numPings) {
        double totalLatency = 0;
        int successfulPings = 0;
        
        for (int i = 0; i < numPings; i++) {
            sendPing();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Wait for responses
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Calculate average from completed pings (placeholder example)
        std::lock_guard<std::mutex> lock(m_pingMutex);
        // In a real scenario, you'd track each ping's round-trip precisely.
        return successfulPings > 0 ? totalLatency / successfulPings : -1.0;
    }

    /***************************************************
     * EWrapper overrides for connection handling
     ***************************************************/

    void IBKRTrader::nextValidId(OrderId orderId) {
        std::cout << "[INFO] nextValidId: " << orderId << std::endl;
        m_nextOrderId = orderId;
    }

    void IBKRTrader::error(int id, long errorTime, int errorCode,
                           const std::string& errorString,
                           const std::string& advancedOrderRejectJson) {
        std::cerr << "[ERROR] ReqId: " << id 
                  << " Code: " << errorCode 
                  << " Msg: " << errorString << std::endl;
    }

    void IBKRTrader::connectAck() {
        std::cout << "[INFO] connectAck: Connected to IBKR" << std::endl;
    }

    void IBKRTrader::connectionClosed() {
        std::cout << "[INFO] Connection closed." << std::endl;
    }

    // This is called when server responds to reqCurrentTime() (for pings)
    void IBKRTrader::currentTime(long time) {
        auto now = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> lock(m_pingMutex);
        
        if (!m_pingRequests.empty()) {
            auto it = m_pingRequests.begin(); // FIFO
            auto sendTime = it->second;
            auto latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(now - sendTime).count();
            double latencyMs = latencyUs / 1000.0;

            std::cout << "[Ping] latency: " << latencyMs << " ms (Server time: " << time << ")" << std::endl;
            
            m_pingRequests.erase(it);
        }
    }

} // namespace connection