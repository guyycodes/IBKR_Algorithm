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
#include "../account_summary/account_summary.hpp" // For AccountSummaryManager
#include "../decoder/decoder.hpp" // For IBKRDecoder

// Forward declaration
namespace model_manager {
    class ModelManager;
}

namespace stock_data_tick {
    class StockData;     // Forward declaration
}

// Forward declarations for thread-local instances
namespace connection {
    class ConnectionCache;
}

namespace ibkr_decoder {
    class IBKRDecoder;
}

namespace ibkr_frame_analyzer {
    class FrameAnalyzer;
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

    // Define a type alias for time points
    using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

    // IBKR Trader class for handling connections
    class IBKRTrader : public DefaultEWrapper {
    private:
        // IBKR client objects
        EReaderOSSignal m_osSignal;        // Synchronizes incoming messages
        EClientSocket*  m_client;          // The IBKR client socket
        std::mutex m_pingMutex;
        
        // Thread-local instances to avoid sharing between threads
        std::unique_ptr<connection::ConnectionCache> m_connectionCache;
        std::unique_ptr<ibkr_decoder::IBKRDecoder> m_decoder;
        std::unique_ptr<ibkr_frame_analyzer::FrameAnalyzer> m_frameAnalyzer;
        
        // Protects ping tracking
        int m_nextPingId = 1;
        std::unordered_map<int, TimePoint> m_pingRequests;
        
        // For order placement or tracking your next valid ID
        int m_nextOrderId = 0;
        
        // Model manager reference to receive market data directly
        model_manager::ModelManager* m_modelManager = nullptr;
        int m_requestId = -1;
        double m_lastPrice = 0.0;
        std::string m_symbol;
        Contract m_contract;
        // Timing control for data updates
        bool m_stopDataStream = false;
        std::thread m_dataRefreshThread;
        TimePoint m_lastAccountUpdate;
        
        // Account summary data manager
        account_summary::AccountSummaryManager m_accountSummaryManager;
        int m_lastAccountSummaryReqId = 9000; // Track the latest account summary request ID

    public:
        // Constructor / Destructor
        IBKRTrader();
        ~IBKRTrader();

        // Connection methods
        bool connect(int clientId = -1, const std::string& symbol = "", const Contract& contract = Contract());
        void disconnect();
        bool isConnected() const { return m_client->isConnected(); }
        EClientSocket* getClient() { return m_client; }
        
        // Set the model manager to receive market data and store symbol
        void setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol);
        
        // Set the request ID for market data
        void setRequestId(int requestId) { m_requestId = requestId; }
        

        std::unique_ptr<EReader> createReader();
        std::thread startMessageProcessing(std::unique_ptr<EReader>& reader);


        void error(int id, long errorTime, int errorCode,
                   const std::string& errorString,
                   const std::string& advancedOrderRejectJson) override;

        void tickString(TickerId tickerId, TickType field, const std::string& value) override;

        
        // Tick by tick callbacks
        void tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
                              Decimal size, const TickAttribLast& tickAttribLast, 
                              const std::string& exchange, const std::string& specialConditions) override;
        
        // Add the new callback declarations to the class definition
        void tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, 
                                    Decimal bidSize, Decimal askSize, 
                                    const TickAttribBidAsk& tickAttribBidAsk) override;
        
        // virtual void tickByTickMidPoint(int reqId, time_t time, double midPoint) override;
        
        // virtual void tickOptionComputation(TickerId tickerId, TickType tickType, int tickAttrib,
        //                                 double impliedVol, double delta, double optPrice,
        //                                 double pvDividend, double gamma, double vega,
        //                                 double theta, double undPrice) override;
        
        // Uncomment this declaration as it's needed for the implementation in connection.cpp
        void realtimeBar(TickerId reqId, long time, double open, double high, 
                               double low, double close, Decimal volume, 
                               Decimal wap, int count) override;
        

        void requestMarketData(const std::string& symbol, const std::string& secType = "STK", 
                              const std::string& currency = "USD", const std::string& exchange = "SMART");

        
        // Additional tick-by-tick data methods
        void requestTickByTickData(TickerId reqId, const std::string& symbol, const std::string& tickType = "AllLast", 
                                  int numberOfTicks = 0, bool ignoreSize = false, const Contract& contract = Contract());
        
        // Helper method to request only data essential for scalping
        void requestScalpingData(const std::string& symbol, const Contract& contract);
        
        // Start continuous data stream for scalping with timed updates
        void startScalpingDataStream(const std::string& symbol, const Contract& contract);
        
        // Stop the data refresh thread
        void stopScalpingDataStream();

        // Start the data stream for scalping with timed updates
        void startDataStream();

        // Route tick market data to ModelManager
        void routeTickToModelManager(double price = 0.0,
                                   double volume = 0.0,
                                   uint64_t timestamp = 0,
                                   double bid = 0.0,
                                   double ask = 0.0,
                                   double bidSize = 0.0,
                                   double askSize = 0.0,
                                   const std::string& exchange = "",
                                   const std::string& specialConditions = "",
                                   double open = 0.0,
                                   double high = 0.0,
                                   double low = 0.0,
                                   double close = 0.0,
                                   double wap = 0.0);


    private:

    };

} // namespace connection

#endif // CONNECTION_HPP