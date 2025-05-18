// api_functions.cpp
// this file contains the functions that call the IBKR API and get data according to the specific need of this project
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "api_functions.hpp"

namespace ibkr_api_functions {

    // API_Functions_Impl implementation
    API_Functions_Impl::API_Functions_Impl(API_Functions& apiFunctions)
        : m_apiFunctions(apiFunctions)
    {
    }

    API_Functions_Impl::~API_Functions_Impl() {
    }

    // Forward all EWrapper callbacks to API_Functions handlers

    // *** TICK PRICE / SIZE (TOP-OF-BOOK) ***
    void API_Functions_Impl::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
        m_apiFunctions.handleTickPrice(tickerId, field, price, attrib);
    }

    void API_Functions_Impl::tickSize(TickerId tickerId, TickType field, Decimal size) {
        m_apiFunctions.handleTickSize(tickerId, field, size);
    }

    void API_Functions_Impl::tickString(TickerId tickerId, TickType tickType, const std::string& value) {
        m_apiFunctions.handleTickString(tickerId, tickType, value);
    }

    void API_Functions_Impl::tickGeneric(TickerId tickerId, TickType tickType, double value) {
        m_apiFunctions.handleTickGeneric(tickerId, tickType, value);
    }

    // *** MARKET DEPTH (L2) ***
    void API_Functions_Impl::updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) {
        m_apiFunctions.handleUpdateMktDepth(id, position, operation, side, price, size);
    }

    void API_Functions_Impl::updateMktDepthL2(TickerId id, int position, const std::string& marketMaker,
                         int operation, int side, double price, Decimal size, bool isSmartDepth) {
        m_apiFunctions.handleUpdateMktDepthL2(id, position, marketMaker, operation, side, price, size, isSmartDepth);
    }

    // *** REAL-TIME BARS ***
    void API_Functions_Impl::realtimeBar(TickerId reqId, long time, double open, double high,
                    double low, double close, Decimal volume, Decimal wap, int count) {
        m_apiFunctions.handleRealtimeBar(reqId, time, open, high, low, close, volume, wap, count);
    }

    // *** HISTORICAL DATA (for bigger timeframe indicators) ***
    void API_Functions_Impl::historicalData(TickerId reqId, const Bar& bar) {
        m_apiFunctions.handleHistoricalData(reqId, bar);
    }

    void API_Functions_Impl::historicalDataEnd(int reqId, const std::string& start, const std::string& end) {
        m_apiFunctions.handleHistoricalDataEnd(reqId, start, end);
    }

    // *** TICK-BY-TICK CALLBACKS ***
    void API_Functions_Impl::tickByTickAllLast(int reqId, int tickType, time_t time, double price, Decimal size,
                          const TickAttribLast& tickAttribLast, const std::string& exchange,
                          const std::string& specialConditions) {
        m_apiFunctions.handleTickByTickAllLast(reqId, tickType, time, price, size, tickAttribLast, exchange, specialConditions);
    }

    void API_Functions_Impl::tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice,
                          Decimal bidSize, Decimal askSize, const TickAttribBidAsk& tickAttribBidAsk) {
        m_apiFunctions.handleTickByTickBidAsk(reqId, time, bidPrice, askPrice, bidSize, askSize, tickAttribBidAsk);
    }

    void API_Functions_Impl::tickByTickMidPoint(int reqId, time_t time, double midPoint) {
        m_apiFunctions.handleTickByTickMidPoint(reqId, time, midPoint);
    }

    // Error handling
    void API_Functions_Impl::error(int id, time_t errorTime, int errorCode, const std::string& errorString, const std::string& advancedOrderRejectJson) {
        m_apiFunctions.handleError(id, errorCode, errorString);
    }

    // Connection status
    void API_Functions_Impl::connectionClosed() {
        std::cout << "Connection to IBKR closed." << std::endl;
    }

    // Order execution tracking for latency measurement
    void API_Functions_Impl::orderStatus(OrderId orderId, const std::string& status, Decimal filled,
                    Decimal remaining, double avgFillPrice, long long permId, int parentId,
                    double lastFillPrice, int clientId, const std::string& whyHeld,
                    double mktCapPrice) {
        m_apiFunctions.handleOrderStatus(orderId, status, filled, remaining, avgFillPrice, 
                                        permId, parentId, lastFillPrice, clientId, whyHeld, mktCapPrice);
    }

    void API_Functions_Impl::execDetails(int reqId, const Contract& contract, const Execution& execution) {
        m_apiFunctions.handleExecDetails(reqId, contract, execution);
    }

    // API_Functions implementation
    
    // Constructor
    API_Functions::API_Functions(connection::IBKRTrader& trader)
        : m_trader(trader)
    {
        m_impl = std::make_unique<API_Functions_Impl>(*this);
    }

    // Destructor
    API_Functions::~API_Functions() {
        // destructor
    }

    // Latency testing
    int API_Functions::sendPing() {
        auto start = std::chrono::high_resolution_clock::now();
        m_latencyMetrics.lastPingTime = start.time_since_epoch().count();
        
        int pingReqId = m_trader.sendPing();
        return pingReqId;
    }

    double API_Functions::measureAverageLatency(int numPings) {
        double totalLatency = 0.0;
        for (int i = 0; i < numPings; ++i) {
            int pingReqId = sendPing();
            // Wait for ping response in a real implementation
            // For now, simulate a typical latency
            std::this_thread::sleep_for(std::chrono::milliseconds(50 + rand() % 20));
            
            auto end = std::chrono::high_resolution_clock::now();
            auto start = std::chrono::time_point<std::chrono::high_resolution_clock>(
                std::chrono::nanoseconds(m_latencyMetrics.lastPingTime));
            
            double latencyMs = std::chrono::duration<double, std::milli>(end - start).count();
            
            // Store in recent latencies
            m_latencyMetrics.recentLatencies.push_back(latencyMs);
            if (m_latencyMetrics.recentLatencies.size() > m_latencyMetrics.maxLatenciesToTrack) {
                m_latencyMetrics.recentLatencies.pop_front();
            }
            
            totalLatency += latencyMs;
        }
        
        // Calculate average latency
        double avgLatency = totalLatency / numPings;
        m_latencyMetrics.connectionLatencyMs = avgLatency;
        
        return avgLatency;
    }

    /***************************************************************
     * Market Data Methods for NYSE & NASDAQ
     ***************************************************************/
    
    void API_Functions::requestMarketDataNYSE(int reqId, const Contract& contract, bool snapshot) {
        // Clone and configure contract for NYSE
        Contract nyseContract = contract;
        nyseContract.exchange = "NYSE";  // Specify NYSE exchange
        
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        m_quoteDataByReqId[reqId] = RTQuoteData();
        m_quoteDataByReqId[reqId].exchange = "NYSE";
        
        // Request specific IBKR generic ticks for NYSE data
        std::string genericTickList = "233,236,258";  // RTVolume, ShortableData, etc.
        bool regulatorySnapshot = false;
        m_trader.getClient()->reqMktData(reqId, nyseContract, genericTickList, snapshot, regulatorySnapshot, {});
    }
    
    void API_Functions::requestMarketDataNASDAQ(int reqId, const Contract& contract, bool snapshot) {
        // Clone and configure contract for NASDAQ
        Contract nasdaqContract = contract;
        nasdaqContract.exchange = "ISLAND";  // "ISLAND" is IBKR's code for NASDAQ
        
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        m_quoteDataByReqId[reqId] = RTQuoteData();
        m_quoteDataByReqId[reqId].exchange = "NASDAQ";
        
        // Request specific IBKR generic ticks for NASDAQ data
        std::string genericTickList = "233,236,258,411";  // Additional NASDAQ-specific data
        bool regulatorySnapshot = false;
        m_trader.getClient()->reqMktData(reqId, nasdaqContract, genericTickList, snapshot, regulatorySnapshot, {});
    }
    
    void API_Functions::requestMarketDataCBOE(int reqId, const Contract& contract, bool snapshot) {
        // Clone and configure contract for CBOE
        Contract cboeContract = contract;
        cboeContract.exchange = "CBOE";  // Specify CBOE exchange
        
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        m_quoteDataByReqId[reqId] = RTQuoteData();
        m_quoteDataByReqId[reqId].exchange = "CBOE";
        
        // Request specific IBKR generic ticks for options data
        std::string genericTickList = "100,101,104,106,165,221,233,236,258";  // Options-specific data
        bool regulatorySnapshot = false;
        m_trader.getClient()->reqMktData(reqId, cboeContract, genericTickList, snapshot, regulatorySnapshot, {});
    }
    
    void API_Functions::cancelMarketData(int reqId) {
        m_trader.getClient()->cancelMktData(reqId);
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol.erase(reqId);
        m_quoteDataByReqId.erase(reqId);
    }
    
    /***************************************************************
     * Market Depth (Level 2) Methods for NYSE & NASDAQ
     ***************************************************************/
    
    void API_Functions::requestMarketDepthNYSE(int reqId, const Contract& contract, int numRows) {
        // Clone and configure contract for NYSE
        Contract nyseContract = contract;
        nyseContract.exchange = "NYSE";  // Specify NYSE exchange
        
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        m_depthData[reqId].clear();
        
        bool isSmartDepth = false; 
        m_trader.getClient()->reqMktDepth(reqId, nyseContract, numRows, isSmartDepth, {});
    }
    
    void API_Functions::requestMarketDepthNASDAQ(int reqId, const Contract& contract, int numRows) {
        // Clone and configure contract for NASDAQ
        Contract nasdaqContract = contract;
        nasdaqContract.exchange = "ISLAND";  // "ISLAND" is IBKR's code for NASDAQ
        
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        m_depthData[reqId].clear();
        
        bool isSmartDepth = false; 
        m_trader.getClient()->reqMktDepth(reqId, nasdaqContract, numRows, isSmartDepth, {});
    }
    
    void API_Functions::requestMarketDepthCBOE(int reqId, const Contract& contract, int numRows) {
        // Clone and configure contract for CBOE
        Contract cboeContract = contract;
        cboeContract.exchange = "CBOE";  // Specify CBOE exchange
        
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        m_depthData[reqId].clear();
        
        bool isSmartDepth = false; 
        m_trader.getClient()->reqMktDepth(reqId, cboeContract, numRows, isSmartDepth, {});
    }
    
    void API_Functions::cancelMarketDepth(int reqId) {
        m_trader.getClient()->cancelMktDepth(reqId, false);
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol.erase(reqId);
        m_depthData.erase(reqId);
    }

    /***************************************************************
     * (2) Market Depth (Level 2)
     **************************************************************/
    void API_Functions::requestMarketDepth(int reqId, const Contract& contract, int numRows) {
        {
            std::lock_guard<std::mutex> lk(m_dataMutex);
            m_reqIdToSymbol[reqId] = contract.symbol;
            m_depthData[reqId].clear();
        }

        bool isSmartDepth = false; 
        m_trader.getClient()->reqMktDepth(reqId, contract, numRows, isSmartDepth, {});
    }

    /***************************************************************
     * (3) Real-Time Bars (5-second)
     **************************************************************/
    void API_Functions::requestRealTimeBars(int reqId, const Contract& contract,
                                            const std::string& whatToShow,
                                            bool useRTH) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        
        // Initialize historical data deque
        if (m_historicalData.find(reqId) == m_historicalData.end()) {
            m_historicalData[reqId] = std::deque<Bar>();
        }
        
        // 5-second bars - smallest granularity available in IBKR API
        m_trader.getClient()->reqRealTimeBars(reqId, contract, 5, whatToShow, useRTH, {});
    }

    void API_Functions::cancelRealTimeBars(int reqId) {
        m_trader.getClient()->cancelRealTimeBars(reqId);
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol.erase(reqId);
    }

    /***************************************************************
     * (4) Historical Data
     **************************************************************/
    void API_Functions::requestHistoricalData(int reqId, const Contract& contract,
                                             const std::string& endDateTime,
                                             const std::string& duration,
                                             const std::string& barSize,
                                             const std::string& whatToShow,
                                             int useRTH,
                                             int formatDate) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        
        // Initialize historical data deque
        if (m_historicalData.find(reqId) == m_historicalData.end()) {
            m_historicalData[reqId] = std::deque<Bar>();
        }
        
        // Request historical data and keep it updated
        bool keepUpToDate = true;  // Keep updating with live data after initial history
        m_trader.getClient()->reqHistoricalData(reqId, contract, endDateTime, duration,
                                    barSize, whatToShow, useRTH, formatDate, keepUpToDate, {});
    }

    void API_Functions::cancelHistoricalData(int reqId) {
        m_trader.getClient()->cancelHistoricalData(reqId);
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol.erase(reqId);
        m_historicalData.erase(reqId);
    }

    /***************************************************************
     * (5) **Tick-by-Tick** (Near Real-Time per Trade/BidAsk)
     **************************************************************/
    void API_Functions::requestTickByTickData(int reqId, const Contract& contract,
                                             const std::string& tickType,
                                             int numberOfTicks,
                                             bool ignoreSize) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol[reqId] = contract.symbol;
        
        // Initialize price and volume history for indicators
        if (m_priceHistory.find(contract.symbol) == m_priceHistory.end()) {
            m_priceHistory[contract.symbol] = std::deque<double>();
            m_volumeHistory[contract.symbol] = std::deque<double>();
            m_hlcHistory[contract.symbol] = std::deque<std::pair<double, double>>();
        }
        
        // Request continuous streaming (numberOfTicks = 0)
        m_trader.getClient()->reqTickByTickData(reqId, contract, tickType, numberOfTicks, ignoreSize);
    }

    void API_Functions::cancelTickByTickData(int reqId) {
        m_trader.getClient()->cancelTickByTickData(reqId);
        std::lock_guard<std::mutex> lk(m_dataMutex);
        m_reqIdToSymbol.erase(reqId);
    }

    /***************************************************
     * Data Callback Handlers
     ***************************************************/

    // *** TICK PRICE / SIZE (TOP-OF-BOOK) ***
    void API_Functions::handleTickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        auto it = m_quoteDataByReqId.find(tickerId);
        if (it == m_quoteDataByReqId.end())
            return;

        auto& rtQuote = it->second;
        // Find the associated symbol outside of switch
        auto symbolIt = m_reqIdToSymbol.find(tickerId);
        const std::string* symbolPtr = nullptr;
        if (symbolIt != m_reqIdToSymbol.end()) {
            symbolPtr = &symbolIt->second;
        }
        
        switch (field) {
            case TickType::BID:
                rtQuote.bidPrice = price;
                break;
            case TickType::ASK:
                rtQuote.askPrice = price;
                break;
            case TickType::LAST:
                rtQuote.lastPrice = price;
                
                // Store price for indicators if we have the symbol
                if (symbolPtr) {
                    const std::string& symbol = *symbolPtr;
                    m_priceHistory[symbol].push_back(price);
                    if (m_priceHistory[symbol].size() > MAX_HISTORY_LENGTH) {
                        m_priceHistory[symbol].pop_front();
                    }
                }
                break;
            default:
                break;
        }
        rtQuote.lastUpdateTime = std::chrono::system_clock::now().time_since_epoch().count();
    }

    void API_Functions::handleTickSize(TickerId tickerId, TickType field, Decimal size) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        auto it = m_quoteDataByReqId.find(tickerId);
        if (it == m_quoteDataByReqId.end())
            return;

        auto& rtQuote = it->second;
        // Find the associated symbol outside of switch
        auto symbolIt = m_reqIdToSymbol.find(tickerId);
        const std::string* symbolPtr = nullptr;
        if (symbolIt != m_reqIdToSymbol.end()) {
            symbolPtr = &symbolIt->second;
        }
        
        switch (field) {
            case TickType::BID_SIZE:
                rtQuote.bidSize = static_cast<int>(size);
                break;
            case TickType::ASK_SIZE:
                rtQuote.askSize = static_cast<int>(size);
                break;
            case TickType::LAST_SIZE:
                rtQuote.lastSize = static_cast<int>(size);
                
                // Store volume for indicators if we have the symbol
                if (symbolPtr) {
                    const std::string& symbol = *symbolPtr;
                    m_volumeHistory[symbol].push_back(static_cast<double>(size));
                    if (m_volumeHistory[symbol].size() > MAX_HISTORY_LENGTH) {
                        m_volumeHistory[symbol].pop_front();
                    }
                }
                break;
            case TickType::VOLUME:
                rtQuote.volume = static_cast<double>(size);
                break;
            default:
                break;
        }
        rtQuote.lastUpdateTime = std::chrono::system_clock::now().time_since_epoch().count();
    }

    void API_Functions::handleTickString(TickerId tickerId, TickType tickType, const std::string& value) {
        // Handle specialized string tick data (e.g. timestamp, news headlines)
        // This would be implementation-specific based on what string data you're interested in
    }

    void API_Functions::handleTickGeneric(TickerId tickerId, TickType tickType, double value) {
        // Handle generic tick types (specialized data from exchange)
    }

    // *** MARKET DEPTH (L2) ***
    void API_Functions::handleUpdateMktDepth(TickerId id, int position, int operation, int side,
                                    double price, Decimal size) {
        std::lock_guard<std::mutex> lk(m_dataMutex);

        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(id);
        if (symbolIt == m_reqIdToSymbol.end())
            return;

        const std::string& symbol = symbolIt->second;
        MktDepthEntry entry;
        entry.price = price;
        entry.size = static_cast<int>(size);
        entry.isBid = (side == 1);  // 0=ask, 1=bid
        entry.position = position;
        entry.marketMaker = "UNKNOWN";

        // Handle different operations: 0=insert, 1=update, 2=delete
        auto& depthData = m_depthData[id];
        
        switch (operation) {
            case 0: // Insert
                depthData.push_back(entry);
                break;
            case 1: // Update
                for (auto& existingEntry : depthData) {
                    if (existingEntry.position == position && existingEntry.isBid == entry.isBid) {
                        existingEntry = entry;
                        break;
                    }
                }
                break;
            case 2: // Delete
                depthData.erase(
                    std::remove_if(depthData.begin(), depthData.end(),
                        [&](const MktDepthEntry& e) {
                            return e.position == position && e.isBid == entry.isBid;
                        }),
                    depthData.end()
                );
                break;
        }
    }

    void API_Functions::handleUpdateMktDepthL2(TickerId id, int position, const std::string& marketMaker,
                                      int operation, int side, double price, Decimal size, bool isSmartDepth) {
        std::lock_guard<std::mutex> lk(m_dataMutex);

        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(id);
        if (symbolIt == m_reqIdToSymbol.end())
            return;

        const std::string& symbol = symbolIt->second;
        MktDepthEntry entry;
        entry.price = price;
        entry.size = static_cast<int>(size);
        entry.isBid = (side == 1);  // 0=ask, 1=bid
        entry.position = position;
        entry.marketMaker = marketMaker;

        // Handle different operations: 0=insert, 1=update, 2=delete
        auto& depthData = m_depthData[id];
        
        switch (operation) {
            case 0: // Insert
                depthData.push_back(entry);
                break;
            case 1: // Update
                for (auto& existingEntry : depthData) {
                    if (existingEntry.position == position && 
                        existingEntry.isBid == entry.isBid &&
                        existingEntry.marketMaker == marketMaker) {
                        existingEntry = entry;
                        break;
                    }
                }
                break;
            case 2: // Delete
                depthData.erase(
                    std::remove_if(depthData.begin(), depthData.end(),
                        [&](const MktDepthEntry& e) {
                            return e.position == position && 
                                   e.isBid == entry.isBid &&
                                   e.marketMaker == marketMaker;
                        }),
                    depthData.end()
                );
                break;
        }
    }

    // *** REAL-TIME BARS ***
    void API_Functions::handleRealtimeBar(TickerId reqId, long time, double open, double high,
                                 double low, double close, Decimal volume, Decimal wap,
                                 int count) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        
        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(reqId);
        if (symbolIt == m_reqIdToSymbol.end())
            return;
        
        const std::string& symbol = symbolIt->second;
        
        // Store bar data
        Bar bar;
        bar.time = std::to_string(time);
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        bar.volume = volume;
        bar.wap = wap;
        bar.count = count;
        
        // Add to historical data
        m_historicalData[reqId].push_back(bar);
        if (m_historicalData[reqId].size() > MAX_HISTORY_LENGTH) {
            m_historicalData[reqId].pop_front();
        }
        
        // Update price history
        m_priceHistory[symbol].push_back(close);
        if (m_priceHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_priceHistory[symbol].pop_front();
        }
        
        // Update high-low-close history
        m_hlcHistory[symbol].push_back(std::make_pair(high, low));
        if (m_hlcHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_hlcHistory[symbol].pop_front();
        }
        
        // Update volume history
        double vol = static_cast<double>(volume);
        m_volumeHistory[symbol].push_back(vol);
        if (m_volumeHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_volumeHistory[symbol].pop_front();
        }
    }

    // *** HISTORICAL DATA (for bigger timeframe indicators) ***
    void API_Functions::handleHistoricalData(TickerId reqId, const Bar& bar) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        
        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(reqId);
        if (symbolIt == m_reqIdToSymbol.end())
            return;
        
        const std::string& symbol = symbolIt->second;
        
        // Store historical bar data
        m_historicalData[reqId].push_back(bar);
        if (m_historicalData[reqId].size() > MAX_HISTORY_LENGTH) {
            m_historicalData[reqId].pop_front();
        }
        
        // Update price, volume, and high-low-close histories
        m_priceHistory[symbol].push_back(bar.close);
        if (m_priceHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_priceHistory[symbol].pop_front();
        }
        
        m_hlcHistory[symbol].push_back(std::make_pair(bar.high, bar.low));
        if (m_hlcHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_hlcHistory[symbol].pop_front();
        }
        
        double vol = static_cast<double>(bar.volume);
        m_volumeHistory[symbol].push_back(vol);
        if (m_volumeHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_volumeHistory[symbol].pop_front();
        }
    }

    void API_Functions::handleHistoricalDataEnd(int reqId, const std::string& start, const std::string& end) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        
        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(reqId);
        if (symbolIt == m_reqIdToSymbol.end())
            return;
        
        const std::string& symbol = symbolIt->second;
        
        std::cout << "[HIST] End for " << symbol 
                  << " from " << start << " to " << end 
                  << " with " << m_historicalData[reqId].size() << " bars" << std::endl;
    }

    // *** TICK-BY-TICK CALLBACKS (NEAR REAL-TIME) ***
    void API_Functions::handleTickByTickAllLast(int reqId, int tickType, time_t time,
                                       double price, Decimal size,
                                       const TickAttribLast& tickAttribLast,
                                       const std::string& exchange,
                                       const std::string& specialConditions) {
        std::lock_guard<std::mutex> lk(m_dataMutex);

        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(reqId);
        if (symbolIt == m_reqIdToSymbol.end())
            return;
        
        const std::string& symbol = symbolIt->second;
        
        // Update quote data for this symbol
        for (auto& pair : m_quoteDataByReqId) {
            auto& rtQuote = pair.second;
            auto reqSymbolIt = m_reqIdToSymbol.find(pair.first);
            if (reqSymbolIt != m_reqIdToSymbol.end() && reqSymbolIt->second == symbol) {
                rtQuote.lastPrice = price;
                rtQuote.lastSize = static_cast<int>(size);
                rtQuote.exchange = exchange;
                rtQuote.lastUpdateTime = std::chrono::system_clock::now().time_since_epoch().count();
            }
        }
        
        // Update price history
        m_priceHistory[symbol].push_back(price);
        if (m_priceHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_priceHistory[symbol].pop_front();
        }
        
        // Update volume history
        double vol = static_cast<double>(size);
        m_volumeHistory[symbol].push_back(vol);
        if (m_volumeHistory[symbol].size() > MAX_HISTORY_LENGTH) {
            m_volumeHistory[symbol].pop_front();
        }
        
        // Log the trade
        std::cout << "[TBT] Trade for " << symbol 
                  << " Time=" << time 
                  << " Price=" << price
                  << " Size=" << static_cast<double>(size)
                  << " Exchange=" << exchange
                  << std::endl;
    }

    void API_Functions::handleTickByTickBidAsk(int reqId, time_t time,
                                      double bidPrice, double askPrice,
                                      Decimal bidSize, Decimal askSize,
                                      const TickAttribBidAsk& tickAttribBidAsk) {
        std::lock_guard<std::mutex> lk(m_dataMutex);

        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(reqId);
        if (symbolIt == m_reqIdToSymbol.end())
            return;
        
        const std::string& symbol = symbolIt->second;
        
        // Update quote data for this symbol
        for (auto& pair : m_quoteDataByReqId) {
            auto& rtQuote = pair.second;
            auto reqSymbolIt = m_reqIdToSymbol.find(pair.first);
            if (reqSymbolIt != m_reqIdToSymbol.end() && reqSymbolIt->second == symbol) {
                rtQuote.bidPrice = bidPrice;
                rtQuote.bidSize = static_cast<int>(bidSize);
                rtQuote.askPrice = askPrice;
                rtQuote.askSize = static_cast<int>(askSize);
                rtQuote.lastUpdateTime = std::chrono::system_clock::now().time_since_epoch().count();
            }
        }
        
        // Log the bid/ask
        std::cout << "[TBT] BidAsk for " << symbol
                  << " Time=" << time
                  << " Bid=" << bidPrice << " x " << static_cast<double>(bidSize)
                  << " Ask=" << askPrice << " x " << static_cast<double>(askSize)
                  << std::endl;
    }

    void API_Functions::handleTickByTickMidPoint(int reqId, time_t time, double midPoint) {
        std::lock_guard<std::mutex> lk(m_dataMutex);
        
        // Find the associated symbol
        auto symbolIt = m_reqIdToSymbol.find(reqId);
        if (symbolIt == m_reqIdToSymbol.end())
            return;
        
        const std::string& symbol = symbolIt->second;
        
        std::cout << "[TBT] MidPoint for " << symbol
                  << " Time=" << time
                  << " Mid=" << midPoint
                  << std::endl;
    }
    
    // Error handling
    void API_Functions::handleError(int id, int errorCode, const std::string& errorString) {
        std::cerr << "Error " << errorCode << " for request " << id << ": " << errorString << std::endl;
    }
    
    // Order execution tracking for latency measurement
    void API_Functions::handleOrderStatus(OrderId orderId, const std::string& status, Decimal filled,
                          Decimal remaining, double avgFillPrice, long long permId, int parentId,
                          double lastFillPrice, int clientId, const std::string& whyHeld,
                          double mktCapPrice) {
        // Record execution latency
        if (status == "Filled" || status == "PartiallyFilled") {
            auto now = std::chrono::high_resolution_clock::now();
            auto nowMs = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
            
            // In a real implementation, you'd track the time when order was submitted
            // and calculate actual fill latency here
            
            // For demo, just add a sample latency value
            double latency = 15.0 + (rand() % 10);  // 15-25ms sample latency
            m_latencyMetrics.executionLatencyMs = latency;
            m_latencyMetrics.recentLatencies.push_back(latency);
            
            if (m_latencyMetrics.recentLatencies.size() > m_latencyMetrics.maxLatenciesToTrack) {
                m_latencyMetrics.recentLatencies.pop_front();
            }
        }
    }
    
    void API_Functions::handleExecDetails(int reqId, const Contract& contract, const Execution& execution) {
        // Additional execution details handling if needed
    }

} // namespace ibkr_api_functions