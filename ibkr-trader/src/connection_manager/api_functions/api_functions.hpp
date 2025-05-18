// api_functions.hpp
#ifndef API_FUNCTIONS_HPP
#define API_FUNCTIONS_HPP

#include <iostream>
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <deque>
#include <chrono>
#include <mutex>
#include "../connection/connection.hpp"
#include "../common_types/common_types.hpp"

namespace ibkr_api_functions {

    // Forward declarations
    class API_Functions;

    // Implementation class
    class API_Functions_Impl : public DefaultEWrapper {
    public:
        API_Functions_Impl(API_Functions& apiFunctions);
        ~API_Functions_Impl();

        // EWrapper interface methods that we need to implement
        
        // *** TICK PRICE / SIZE (TOP-OF-BOOK) ***
        void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override;
        void tickSize(TickerId tickerId, TickType field, Decimal size) override;
        void tickString(TickerId tickerId, TickType tickType, const std::string& value) override;
        void tickGeneric(TickerId tickerId, TickType tickType, double value) override;
        
        // *** MARKET DEPTH (L2) ***
        void updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) override;
        void updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, int operation,
                            int side, double price, Decimal size, bool isSmartDepth) override;
                            
        // *** REAL-TIME BARS ***
        void realtimeBar(TickerId reqId, long time, double open, double high, double low, 
                        double close, Decimal volume, Decimal wap, int count) override;
                        
        // *** HISTORICAL DATA (for bigger timeframe indicators) ***
        void historicalData(TickerId reqId, const Bar& bar) override;
        void historicalDataEnd(int reqId, const std::string& start, const std::string& end) override;
        
        // *** TICK-BY-TICK CALLBACKS ***
        void tickByTickAllLast(int reqId, int tickType, time_t time, double price, Decimal size,
                             const TickAttribLast& tickAttribLast, const std::string& exchange,
                             const std::string& specialConditions) override;
        void tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice,
                             Decimal bidSize, Decimal askSize, const TickAttribBidAsk& tickAttribBidAsk) override;
        void tickByTickMidPoint(int reqId, time_t time, double midPoint) override;
        
        // Error handling
        void error(int id, time_t errorTime, int errorCode, const std::string& errorString, const std::string& advancedOrderRejectJson) override;
        
        // Connection status
        void connectionClosed() override;
        
        // Order execution tracking for latency measurement
        void orderStatus(OrderId orderId, const std::string& status, Decimal filled,
                       Decimal remaining, double avgFillPrice, long long permId, int parentId,
                       double lastFillPrice, int clientId, const std::string& whyHeld,
                       double mktCapPrice) override;
        void execDetails(int reqId, const Contract& contract, const Execution& execution) override;

    private:
        API_Functions& m_apiFunctions;
    };

    // Struct to track real-time quote data
    struct RTQuoteData {
        double bidPrice = 0.0;
        double askPrice = 0.0;
        int bidSize = 0;
        int askSize = 0;
        double lastPrice = 0.0;
        int lastSize = 0;
        double volume = 0.0;
        std::string exchange;
        long long lastUpdateTime = 0;
    };

    // Struct to track market depth entries
    struct MktDepthEntry {
        double price = 0.0;
        int size = 0;
        bool isBid = false;
        int position = 0;
        std::string marketMaker;
    };

    // Struct to track latency metrics
    struct LatencyMetrics {
        double connectionLatencyMs = 0.0;
        double executionLatencyMs = 0.0;
        long long lastPingTime = 0;
        std::deque<double> recentLatencies;
        const size_t maxLatenciesToTrack = 100;
    };

    // Main API functions class
    class API_Functions {
    public:
        // Constructor
        API_Functions(connection::IBKRTrader& trader);
        
        // Destructor
        ~API_Functions();
        
        // Access to the client
        EClientSocket* getClient() { return m_trader.getClient(); }
        
        // Latency testing
        int sendPing();
        double measureAverageLatency(int numPings);
        
        /***************************************************************
         * Market Data Methods for NYSE & NASDAQ
         ***************************************************************/
        void requestMarketDataNYSE(int reqId, const Contract& contract, bool snapshot = false);
        void requestMarketDataNASDAQ(int reqId, const Contract& contract, bool snapshot = false);
        void requestMarketDataCBOE(int reqId, const Contract& contract, bool snapshot = false);
        void cancelMarketData(int reqId);
        
        /***************************************************************
         * Market Depth (Level 2) Methods for NYSE & NASDAQ
         ***************************************************************/
        void requestMarketDepthNYSE(int reqId, const Contract& contract, int numRows = 5);
        void requestMarketDepthNASDAQ(int reqId, const Contract& contract, int numRows = 5);
        void requestMarketDepthCBOE(int reqId, const Contract& contract, int numRows = 5);
        void cancelMarketDepth(int reqId);
        
        /***************************************************************
         * (2) Market Depth (Level 2)
         **************************************************************/
        void requestMarketDepth(int reqId, const Contract& contract, int numRows = 5);
        
        /***************************************************************
         * (3) Real-Time Bars (5-second)
         **************************************************************/
        void requestRealTimeBars(int reqId, const Contract& contract,
                               const std::string& whatToShow = "TRADES",
                               bool useRTH = true);
        void cancelRealTimeBars(int reqId);
        
        /***************************************************************
         * (4) Historical Data
         **************************************************************/
        void requestHistoricalData(int reqId, const Contract& contract,
                                  const std::string& endDateTime, // Empty string for latest
                                  const std::string& duration,    // e.g., "1 D", "1 W", "1 M"
                                  const std::string& barSize,     // e.g., "1 min", "5 mins", "1 hour"
                                  const std::string& whatToShow = "TRADES",
                                  int useRTH = 1,
                                  int formatDate = 1);
        void cancelHistoricalData(int reqId);
        
        /***************************************************************
         * (5) **Tick-by-Tick** (Near Real-Time per Trade/BidAsk)
         **************************************************************/
        void requestTickByTickData(int reqId, const Contract& contract,
                                  const std::string& tickType, // "Last", "AllLast", "BidAsk", "MidPoint"
                                  int numberOfTicks = 0,       // 0 = unlimited streaming data
                                  bool ignoreSize = true);     // Ignore size-only updates
        void cancelTickByTickData(int reqId);
        
        // Callback handlers for data (called by API_Functions_Impl)
        void handleTickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib);
        void handleTickSize(TickerId tickerId, TickType field, Decimal size);
        void handleTickString(TickerId tickerId, TickType tickType, const std::string& value);
        void handleTickGeneric(TickerId tickerId, TickType tickType, double value);
        void handleUpdateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size);
        void handleUpdateMktDepthL2(TickerId id, int position, const std::string& marketMaker, int operation,
                             int side, double price, Decimal size, bool isSmartDepth);
        void handleRealtimeBar(TickerId reqId, long time, double open, double high, double low,
                           double close, Decimal volume, Decimal wap, int count);
        void handleHistoricalData(TickerId reqId, const Bar& bar);
        void handleHistoricalDataEnd(int reqId, const std::string& start, const std::string& end);
        void handleTickByTickAllLast(int reqId, int tickType, time_t time, double price, Decimal size,
                                const TickAttribLast& tickAttribLast, const std::string& exchange,
                                const std::string& specialConditions);
        void handleTickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice,
                               Decimal bidSize, Decimal askSize, const TickAttribBidAsk& tickAttribBidAsk);
        void handleTickByTickMidPoint(int reqId, time_t time, double midPoint);
        void handleError(int id, int errorCode, const std::string& errorString);
        void handleOrderStatus(OrderId orderId, const std::string& status, Decimal filled,
                            Decimal remaining, double avgFillPrice, long long permId, int parentId,
                            double lastFillPrice, int clientId, const std::string& whyHeld,
                            double mktCapPrice);
        void handleExecDetails(int reqId, const Contract& contract, const Execution& execution);
        
    private:
        // Pimpl idiom - implementation details
        std::unique_ptr<API_Functions_Impl> m_impl;
        
        // Reference to the IBKRTrader instance
        connection::IBKRTrader& m_trader;
        
        // Maximum history length to store
        static constexpr size_t MAX_HISTORY_LENGTH = 1000;
        
        // Mutex for thread safety
        std::mutex m_dataMutex;
        
        // Map of request IDs to symbols
        std::map<int, std::string> m_reqIdToSymbol;
        
        // Map of real-time quote data by request ID
        std::map<int, RTQuoteData> m_quoteDataByReqId;
        
        // Map of market depth data by request ID
        std::map<int, std::vector<MktDepthEntry>> m_depthData;
        
        // Map of historical data by request ID
        std::map<int, std::deque<Bar>> m_historicalData;
        
        // Price and volume history by symbol for indicators
        std::map<std::string, std::deque<double>> m_priceHistory;
        std::map<std::string, std::deque<double>> m_volumeHistory;
        std::map<std::string, std::deque<std::pair<double, double>>> m_hlcHistory;
        
        // Latency metrics
        LatencyMetrics m_latencyMetrics;
    };

} // namespace ibkr_api_functions


#endif // API_FUNCTIONS_HPP