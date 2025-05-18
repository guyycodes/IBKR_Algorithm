#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <string>
#include <vector>
#include "ibkr/cppclient/client/CommonDefs.h"
#include "ibkr/cppclient/client/DefaultEWrapper.h"
#include "ibkr/cppclient/client/EClientSocket.h"
#include "ibkr/cppclient/client/EReaderOSSignal.h"
#include "ibkr/cppclient/client/EReader.h"
#include "ibkr/cppclient/client/Contract.h"   // For Contract definition

namespace connection {

    // Connection constants
    extern const int client_id;
    extern const char* HOST;
    extern int PORT;

    // Struct for storing basic real-time quote data internally
    struct RTQuoteData {
        double bidPrice = 0.0;
        int    bidSize  = 0;
        double askPrice = 0.0;
        int    askSize  = 0;
        double lastPrice = 0.0;
        int    lastSize  = 0;
        double volume    = 0.0;
        long   lastUpdateTime = 0; 
    };

    // IBKR Trader class for handling connections
    class IBKRTrader : public DefaultEWrapper {
    private:
        using TimePoint = std::chrono::high_resolution_clock::time_point;
        EReaderOSSignal m_osSignal;        // Synchronizes incoming messages
        EClientSocket*  m_client;          // The IBKR client socket
        std::mutex m_pingMutex;
        
        // Protects ping tracking
        int m_nextPingId = 1000;
        std::unordered_map<int, TimePoint> m_pingRequests;
        
        // For order placement or tracking your next valid ID
        OrderId m_nextOrderId = -1;

    public:
        // Constructor / Destructor
        IBKRTrader();
        ~IBKRTrader();

        // Connection methods
        bool connect();
        void disconnect();
        bool isConnected() const { return m_client->isConnected(); }
        EClientSocket* getClient() { return m_client; }

        // Create a reader and start reading incoming messages
        std::unique_ptr<EReader> createReader();
        std::thread startMessageProcessing(std::unique_ptr<EReader>& reader);

        // Latency testing
        int sendPing();
        double measureAverageLatency(int numPings = 5);

        // EWrapper overrides for connection handling
        void nextValidId(OrderId orderId) override;
        void error(int id, long errorTime, int errorCode,
                   const std::string& errorString,
                   const std::string& advancedOrderRejectJson) override;
        void connectAck() override;
        void connectionClosed() override;
        void currentTime(long time) override;
    };

} // namespace connection

#endif // CONNECTION_HPP