// #ifndef CONNECTION_HPP
// #define CONNECTION_HPP

// #include <chrono>
// #include <memory>
// #include <mutex>
// #include <thread>
// #include <unordered_map>
// #include <string>
// #include <vector>
// #include "ibkr/cppclient/client/CommonDefs.h"
// #include "ibkr/cppclient/client/DefaultEWrapper.h"
// #include "ibkr/cppclient/client/EClientSocket.h"
// #include "ibkr/cppclient/client/EReaderOSSignal.h"
// #include "ibkr/cppclient/client/EReader.h"
// #include "ibkr/cppclient/client/Contract.h"   // For Contract definition
// #include "../account_summary/account_summary.hpp" // For AccountSummaryManager
// #include "../decoder/decoder.hpp" // For IBKRDecoder

// // Forward declaration
// namespace model_manager {
//     class ModelManager;
// }

// namespace stock_data_tick {
//     class StockData;     // Forward declaration
// }

// // Forward declarations for thread-local instances
// namespace connection {
//     class ConnectionCache;
// }

// namespace ibkr_decoder {
//     class IBKRDecoder;
// }

// namespace ibkr_frame_analyzer {
//     class FrameAnalyzer;
// }

// namespace connection {

//     // Connection constants
//     extern int client_id;
//     extern const char* HOST;
//     extern int PORT;

//     // Struct for storing basic real-time quote data internally
//     struct RTQuoteData {
//         double bidPrice = 0.0;
//         int    bidSize  = 0;
//         double askPrice = 0.0;
//         int    askSize  = 0;
//         double lastPrice = 0.0;
//         int    lastSize  = 0;
//         double volume    = 0.0;
//         long   lastUpdateTime = 0; 
//     };

//     // Define a type alias for time points
//     using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

//     // IBKR Trader class for handling connections
//     class IBKRTrader : public DefaultEWrapper {
//     private:
//         // IBKR client objects
//         EReaderOSSignal m_osSignal;        // Synchronizes incoming messages
//         EClientSocket*  m_client;          // The IBKR client socket
//         std::mutex m_pingMutex;
        
//         // Thread-local instances to avoid sharing between threads
//         std::unique_ptr<connection::ConnectionCache> m_connectionCache;
//         std::unique_ptr<ibkr_decoder::IBKRDecoder> m_decoder;
//         std::unique_ptr<ibkr_frame_analyzer::FrameAnalyzer> m_frameAnalyzer;
        
//         // Protects ping tracking
//         int m_nextPingId = 1;
//         std::unordered_map<int, TimePoint> m_pingRequests;
        
//         // For order placement or tracking your next valid ID
//         int m_nextOrderId = 0;
        
//         // Model manager reference to receive market data directly
//         model_manager::ModelManager* m_modelManager = nullptr;
//         int m_requestId = -1;
//         double m_lastPrice = 0.0;
//         double m_lastShareSize = 0.0;
//         std::string m_symbol;
//         Contract m_contract;
//         // Timing control for data updates
//         bool m_stopDataStream = false;
//         std::thread m_dataRefreshThread;
//         TimePoint m_lastAccountUpdate;
        
//         // Account summary data manager
//         account_summary::AccountSummaryManager m_accountSummaryManager;
//         int m_lastAccountSummaryReqId = 9000; // Track the latest account summary request ID

//     public:
//         // Constructor / Destructor
//         IBKRTrader();
//         ~IBKRTrader();

//         // Connection methods
//         bool connect(int clientId = -1, const std::string& symbol = "", const Contract& contract = Contract());
//         void disconnect();
//         bool isConnected() const { return m_client->isConnected(); }
//         EClientSocket* getClient() { return m_client; }
        
//         // Set the model manager to receive market data and store symbol
//         void setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol);
        
//         // Set the request ID for market data
//         void setRequestId(int requestId) { m_requestId = requestId; }
        
//         // Get access to the account summary manager
//         // const account_summary::AccountSummaryManager& getAccountSummaryManager() const { return m_accountSummaryManager; }
        
//         // Print account summary data (useful for debugging)
//         // void printAccountSummary() const { m_accountSummaryManager.printAllData(); }
        
//         // Cancel any active account summary requests to avoid hitting IBKR API limits
//         // void cancelAccountSummaryRequests();

//         // Create a reader and start reading incoming messages
//         std::unique_ptr<EReader> createReader();
//         std::thread startMessageProcessing(std::unique_ptr<EReader>& reader);

//         // Latency testing
//         // int sendPing();
//         // double measureAverageLatency(int numPings = 5);

//         // EWrapper overrides for connection handling
//         // void nextValidId(OrderId orderId) override;
//         void error(int id, long errorTime, int errorCode,
//                    const std::string& errorString,
//                    const std::string& advancedOrderRejectJson) override;
//         // void connectAck() override;
//         // void connectionClosed() override;
//         // void currentTime(long time) override;
        
//         // Tick data callbacks
//         void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override;
//         void tickSize(TickerId tickerId, TickType field, Decimal size) override;
//         void tickString(TickerId tickerId, TickType field, const std::string& value) override;
//         void tickGeneric(TickerId tickerId, TickType field, double value) override;
//         // void managedAccounts(const std::string& accountsList) override;
        
//         // Tick by tick callbacks
//         void tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
//                               Decimal size, const TickAttribLast& tickAttribLast, 
//                               const std::string& exchange, const std::string& specialConditions) override;
        
//         // Add the new callback declarations to the class definition
//         // virtual void tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, 
//         //                             Decimal bidSize, Decimal askSize, 
//         //                             const TickAttribBidAsk& tickAttribBidAsk) override;
        
//         // virtual void tickByTickMidPoint(int reqId, time_t time, double midPoint) override;
        
//         // virtual void tickOptionComputation(TickerId tickerId, TickType tickType, int tickAttrib,
//         //                                 double impliedVol, double delta, double optPrice,
//         //                                 double pvDividend, double gamma, double vega,
//         //                                 double theta, double undPrice) override;
        
//         // Uncomment this declaration as it's needed for the implementation in connection.cpp
//         virtual void realtimeBar(TickerId reqId, long time, double open, double high, 
//                                double low, double close, Decimal volume, 
//                                Decimal wap, int count) override;
        
//         // Market depth callbacks
//         virtual void updateMktDepth(TickerId id, int position, int operation, int side, 
//                                    double price, Decimal size) override;
//         virtual void updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, 
//                                      int operation, int side, double price, Decimal size, 
//                                      bool isSmartDepth) override;
        
//         // virtual void historicalTicks(int reqId, const std::vector<HistoricalTick>& ticks, bool done) override;
        
//         // virtual void historicalTicksBidAsk(int reqId, const std::vector<HistoricalTickBidAsk>& ticks, bool done) override;
        
//         // virtual void historicalTicksLast(int reqId, const std::vector<HistoricalTickLast>& ticks, bool done) override;

//         // Methods to request additional data types
        
//         // Account and Portfolio methods
//         void requestAccountSummary();
//         // void requestPositions();
//         // void requestPnL();
//         // void requestAccountUpdates(const std::string& account = "");
        
//         // Contract and Market Data methods
//         // void requestContractDetails(const std::string& symbol, const std::string& secType = "STK", 
//         //                            const std::string& currency = "USD", const std::string& exchange = "SMART");
//         void requestMarketData(const std::string& symbol, const std::string& secType = "STK", 
//                               const std::string& currency = "USD", const std::string& exchange = "SMART");
//         // void cancelMarketData(TickerId tickerId);
        
//         // Historical Data methods
//         // void requestHistoricalData(const std::string& symbol, const std::string& duration = "1 D", 
//         //                           const std::string& barSize = "1 min", const std::string& whatToShow = "TRADES");
        
//         // Real-time Bar data method
//         // void requestRealtimeBars(const std::string& symbol);
        
//         // Options Data
//         // void requestOptionChain(const std::string& symbol, const std::string& exchange = "SMART");
        
//         // Additional tick-by-tick data methods
//         void requestTickByTickData(TickerId reqId, const std::string& symbol, const std::string& tickType = "AllLast", 
//                                   int numberOfTicks = 0, bool ignoreSize = false, const Contract& contract = Contract());
        
//         // News and fundamentals
//         // void requestNewsBulletins(bool allMessages = true);
//         // void requestFundamentalData(const std::string& symbol);

//         // Account and Portfolio callbacks
//         void accountSummary(int reqId, const std::string& account, const std::string& tag, 
//                           const std::string& value, const std::string& currency) override;
//         // void accountSummaryEnd(int reqId) override;
//         // void position(const std::string& account, const Contract& contract, 
//         //              Decimal position, double avgCost) override;
//         // void positionEnd() override;
//         // void pnl(int reqId, double dailyPnL, double unrealizedPnL, double realizedPnL) override;
//         // void pnlSingle(int reqId, Decimal pos, double dailyPnL, double unrealizedPnL, 
//         //               double realizedPnL, double value) override;
//         // void updateAccountValue(const std::string& key, const std::string& val, 
//         //                       const std::string& currency, const std::string& accountName) override;
//         // void updatePortfolio(const Contract& contract, Decimal position, double marketPrice,
//         //                     double marketValue, double averageCost, double unrealizedPNL,
//         //                     double realizedPNL, const std::string& accountName) override;
//         // void updateAccountTime(const std::string& timeStamp) override;
//         // void accountDownloadEnd(const std::string& accountName) override;
        
//         // Contract Details callbacks
//         // void contractDetails(int reqId, const ContractDetails& contractDetails) override;
//         // void contractDetailsEnd(int reqId) override;
        
//         // Historical Data callbacks (add if not already present)
//         // virtual void historicalData(TickerId reqId, const Bar& bar) override;
        
//         // News and Fundamentals callbacks
//         // void updateNewsBulletin(int msgId, int msgType, const std::string& newsMessage, 
//         //                       const std::string& originExch) override;
//         // void fundamentalData(TickerId reqId, const std::string& data) override;

//         // Helper method to request all available data types at once
//         // void requestAllAvailableData(const std::string& symbol = "");
        
//         // Helper method to request only data essential for scalping
//         void requestScalpingData(const std::string& symbol, const Contract& contract);
        
//         // Start continuous data stream for scalping with timed updates
//         void startScalpingDataStream(const std::string& symbol, const Contract& contract);
        
//         // Stop the data refresh thread
//         void stopScalpingDataStream();

//         // Start the data stream for scalping with timed updates
//         void startDataStream();
        
//         // Utility method to test all data types with documentation
//         // void testAllDataTypes(const std::string& symbol);
        

//         // Market depth callbacks
//         // void updateMktDepth(TickerId id, int position, int operation, int side, 
//         //                  double price, Decimal size) override;
//         // void updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, 
//         //                    int operation, int side, double price, Decimal size, 
//         //                    bool isSmartDepth) override;

//         // Route tick market data to ModelManager
//         void routeTickToModelManager(double price = 0.0,
//                                    double volume = 0.0,
//                                    uint64_t timestamp = 0,
//                                    double bid = 0.0,
//                                    double ask = 0.0,
//                                    double bidSize = 0.0,
//                                    double askSize = 0.0,
//                                    const std::string& exchange = "",
//                                    const std::string& specialConditions = "",
//                                    double open = 0.0,
//                                    double high = 0.0,
//                                    double low = 0.0,
//                                    double close = 0.0,
//                                    double wap = 0.0,
//                                    double lastPrice = 0.0,
//                                    double lastShareSize = 0.0);


//     private:
//         // Helper method to route market data to the model manager
//         // void routeTickToModelManager(double price, double volume, uint64_t timestamp);
        
//         // Static variables for API connection configuration
//     };

// } // namespace connection

// #endif // CONNECTION_HPP