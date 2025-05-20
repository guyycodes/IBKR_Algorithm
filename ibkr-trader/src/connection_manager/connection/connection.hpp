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

// Forward declaration
namespace model_manager {
    class ModelManager;
}

namespace connection {

    // Connection constants
    extern int client_id;
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
        
        // Model manager reference to receive market data directly
        model_manager::ModelManager* m_modelManager = nullptr;
        int m_requestId = -1;
        double m_lastPrice = 0.0;
        std::string m_symbol;

    public:
        // Constructor / Destructor
        IBKRTrader();
        ~IBKRTrader();

        // Connection methods
        bool connect(int clientId = -1);
        void disconnect();
        bool isConnected() const { return m_client->isConnected(); }
        EClientSocket* getClient() { return m_client; }
        
        // Set the model manager to receive market data and store symbol
        void setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol);
        
        // Set the request ID for market data
        void setRequestId(int requestId) { m_requestId = requestId; }

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
        
        // Tick data callbacks
        void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override;
        void tickSize(TickerId tickerId, TickType field, Decimal size) override;
        void tickString(TickerId tickerId, TickType field, const std::string& value) override;
        void tickGeneric(TickerId tickerId, TickType field, double value) override;
        void managedAccounts(const std::string& accountsList) override;
        
        // Tick by tick callbacks
        void tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
                              Decimal size, const TickAttribLast& tickAttribLast, 
                              const std::string& exchange, const std::string& specialConditions) override;
        
    private:
        // Helper method to route market data to the model manager
        void routeTickToModelManager(double price, double volume, uint64_t timestamp = 0);
    };

} // namespace connection

#endif // CONNECTION_HPP