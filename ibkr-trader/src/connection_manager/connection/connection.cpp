#include "connection.hpp"
#include "../connection_cache/connection_cache.hpp"
#include "../../models/model_manager.hpp"  // Include ModelManager for direct access
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>  // For std::hex, std::setw, etc.
#include <thread>   // For std::this_thread::get_id()
#include "../decoder/decoder.hpp"  // Include IBKRDecoder for special size value handling
#include "../decoder/frame_analyzer.hpp"  // Include FrameAnalyzer for tick string field 48 analysis

namespace connection {
// reqMarketData: will setup the tickString, tickGeneric, tickPrice, tickSize callbacks every 250ms
// reqTickByTick: will get the ticks as they come in
// realTimeBars: will get the 5-second bar updates
    // Define connection constants
    const char* HOST = "host.docker.internal";
    // Paper Trading port is 4002, Live Trading would be 7496
    // We're using Paper Trading for testing
    int PORT = 4002;  // Paper Trading port
    int client_id = 0;  // Default client ID - we'll override this in connect()
    
    // Constructor implementation
    IBKRTrader::IBKRTrader() 
        : m_osSignal(2000),
          m_client(new EClientSocket(this, &m_osSignal)), // "register" your callback handler
          m_frameAnalyzer(std::make_unique<ibkr_frame_analyzer::FrameAnalyzer>()),
          m_decoder(std::make_unique<ibkr_decoder::IBKRDecoder>(*m_frameAnalyzer)),
          m_connectionCache(std::make_unique<ConnectionCache>(*m_decoder)),
          m_modelManager(nullptr),
          m_requestId(-1),
          m_lastPrice(0.0),
          m_symbol(""),
          m_contract()
    {
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    std::cout << "[Connection][Constructor] [thread_id: " << threadIdStr.str() << "] Created IBKRTrader" << std::endl;
    }
    
    // Destructor implementation
    IBKRTrader::~IBKRTrader() {
        // Stop any running data stream
        stopScalpingDataStream();
        // Clean up the client
        delete m_client;
    }
    
    // Connect to IBKR Gateway :: Called by model_manager.cpp to make initial conneection and request initial market data
    bool IBKRTrader::connect(int clientId, const std::string& symbol, const Contract& contract) {
        // Set the connection options to enable API extensions
        m_client->setConnectOptions("+PACEAPI");
        
        // Use provided client ID or fall back to default
        int actualClientId = (clientId >= 0) ? clientId : client_id;
        
        bool success = m_client->eConnect(HOST, PORT, actualClientId, /*extraAuth=*/false);
        if (success) {
            std::cout << "[INFO] Connection initiated to " << HOST << ":" << PORT 
                      << " with client ID: " << actualClientId << std::endl;
            
            // Wait a bit for connection to stabilize
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // set the symbol and contract to the class variables
            m_symbol = symbol;
            m_contract = contract;

        } else {
            std::cerr << "[ERROR] Failed to connect to IBKR." << std::endl;
        }
        return success;
    }
    
    // // Set the model manager for direct market data routing
    void IBKRTrader::setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol) {
        m_modelManager = modelManager;
        m_symbol = symbol;
        std::cout << "[IBKRTrader] Set ModelManager for symbol: " << m_symbol << std::endl;
    }

    //     //  createReader() is responsible for making a reader for retrieving messages from the IBKR connection, std::unique_ptr<EReader> (smart pointer that automatically manages memory
    std::unique_ptr<EReader> IBKRTrader::createReader() {
        std::unique_ptr<EReader> reader(new EReader(m_client, &m_osSignal));
        reader->start();
        return reader;
    }
    
    // Start message processing in a separate thread
    std::thread IBKRTrader::startMessageProcessing(std::unique_ptr<EReader>& reader) {
    // When your code makes a request (like requestMarketData (from inside the model_manager.cpp)), 
    // the IBKR server will respond with data. The EReader in the message 
    // processing thread will parse these responses  and call the appropriate 
    // EWrapper methods on your IBKRTrader instance.
        return std::thread([this, &reader]() {
            while (m_client->isConnected()) {
                m_osSignal.waitForSignal(); // waits for a notification that messages are available
                reader->processMsgs(); // processes any received messages
            }
        });
    }
    
    // Disconnect from IBKR Gateway
    void IBKRTrader::disconnect() {
        // First stop any data streams
        stopScalpingDataStream();
        
        // Cancel any active account summary request
        if (m_client && m_client->isConnected() && m_lastAccountSummaryReqId > 9000) {
            std::cout << "[INFO] Canceling active account summary request with reqId: " 
                      << m_lastAccountSummaryReqId << std::endl;
            m_client->cancelAccountSummary(m_lastAccountSummaryReqId);
            
            // Give the cancellation a moment to process
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Clear account summary data
        m_accountSummaryManager.clear();
        
        // Then disconnect from the API
        if (m_client) {
            m_client->eDisconnect();
        }
        std::cout << "[INFO] Disconnected from IBKR.\n";
    }

    // Start continuous data stream for scalping with timed updates
    void IBKRTrader::startDataStream() { 
        std::cout << "[INFO] Automatically requesting scalping data for symbol: " << m_symbol << " and contract: " << m_contract.symbol << std::endl;
        startScalpingDataStream(m_symbol, m_contract);
    }

         // Start continuous data stream for scalping with timed updates
    void IBKRTrader::startScalpingDataStream(const std::string& symbol, const Contract& contract) {
        if (!m_client || !m_client->isConnected()) {
            std::cerr << "[ERROR] Cannot start scalping data stream: not connected" << std::endl;
            return;
        }
        
        // Stop any existing data stream thread
        stopScalpingDataStream();
        
        // Reset timing variables
        m_stopDataStream = false;
        m_lastAccountUpdate = std::chrono::high_resolution_clock::now();
        
        // Make initial data subscriptions
        std::cout << "[INFO] Making initial data subscriptions for " << symbol << std::endl;
        requestScalpingData(symbol, contract);
        
        // Start the refresh thread for account data only
        m_dataRefreshThread = std::thread([this]() {
            std::cout << "[INFO] Starting account data refresh thread" << std::endl;
            
            while (!m_stopDataStream && m_client->isConnected()) {
                auto now = std::chrono::high_resolution_clock::now();
                
                // Account data refresh (every 30 seconds)
                auto accountElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastAccountUpdate).count();
                
                if (accountElapsed >= 30000) { // 30 seconds
                    std::cout << "[Timer] Refreshing account data" << std::endl;
                    // requestAccountSummary();
                    m_lastAccountUpdate = now;
                }
                
                // Sleep to prevent CPU hogging (100ms)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            std::cout << "[INFO] Account data refresh thread stopped" << std::endl;
        });
    }

    // Request only essential data for scalping algorithms - these are one-time subscriptions
    void IBKRTrader::requestScalpingData(const std::string& symbol, const Contract& contract) {
        if (!m_client || !m_client->isConnected()) {
            std::cerr << "[ERROR] Cannot request scalping data: not connected" << std::endl;
            return;
        }
        // Print thread ID
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        
        std::cout << "\n==================================================\n";
        std::cout << "SUBSCRIBING TO SCALPING DATA FOR: " << symbol << "\n";
        std::cout << "[connection] [ThreadID: " << threadIdStr.str() << "] " << m_symbol << std::endl;
        std::cout << "==================================================\n";
        
        // Set to use real-time data instead of delayed
        m_client->reqMarketDataType(1); // 1 = REALTIME (was 3 = DELAYED)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // 1. Account Information (will be refreshed periodically by timer)
        std::cout << "[1] Requesting Account Information\n";
        // requestAccountSummary();

        /////////////////////////////////////////////////////////////////////////////
        // reqmarketdata is being called in model manager for tickString data
        // line 164 model_manager:  client->reqMktData(m_requestId, contract, genericTicks, snapshot, regulatorySnapshot, mktDataOptions);
        /////////////////////////////////////////////////////////////////////////////

        // 3. Tick-by-Tick Data (continuous subscription)

        std::cout << "[3] Subscribing to Tick-by-Tick Data for " << symbol << "\n";
        static int tickRequestId = 7001;

        requestTickByTickData(tickRequestId++, symbol, "AllLast", 0, false, contract); // Last trades

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        requestTickByTickData(tickRequestId++, symbol, "BidAsk", 0, false, contract);

        // 4. Market Depth (continuous subscription)
        std::cout << "[4] Subscribing to Market Depth for " << symbol << "\n";
        static int depthReqId = 8001;

        // m_client->reqMktDepth(depthReqId++, contract, 5, false, {});
    
        // 6. Add Real-time Bar data (5-second bars) to get continuous OHLC updates
        std::cout << "[6] Subscribing to Real-time Bar Data for " << symbol << "\n";
        static int barRequestId = 6001;
        
        
        // 5 = 5 seconds, "TRADES" = based on trade data, useRTH = false (include after-hours data)
        m_client->reqRealTimeBars(barRequestId++, contract, 5, "TRADES", false, {});

        
        std::cout << "\n==================================================\n";
        std::cout << "DATA SUBSCRIPTIONS ACTIVE - RECEIVING CALLBACKS\n";
        std::cout << "==================================================\n";
    }
    
    // // Stop the data refresh thread
    void IBKRTrader::stopScalpingDataStream() {
        // Signal the thread to stop
        m_stopDataStream = true;
        
        // Join the thread if it's running
        if (m_dataRefreshThread.joinable()) {
            std::cout << "[INFO] Waiting for data refresh thread to stop..." << std::endl;
            m_dataRefreshThread.join();
            std::cout << "[INFO] Data refresh thread stopped" << std::endl;
        }
    }
    
    void IBKRTrader::requestTickByTickData(TickerId reqId, const std::string& symbol, 
                                         const std::string& tickType, int numberOfTicks, 
                                         bool ignoreSize, const Contract& contract) {
        if (!m_client || !m_client->isConnected()) {
            std::cerr << "[ERROR] Cannot request tick-by-tick data: not connected" << std::endl;
            return;
        }
                  
        // Note: Tick-by-tick data requires market data subscription in IB
        // For delayed data users, this will either not work or provide delayed data
        m_client->reqTickByTickData(reqId, contract, tickType, numberOfTicks, ignoreSize);
    }
    
    // Route tick market data to ModelManager///////midPoint is pre-calcualted//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::routeTickToModelManager(double midPoint, double volume, uint64_t timestamp,
                                           double bid, double ask, double bidSize, double askSize,
                                           const std::string& exchange, const std::string& specialConditions,
                                           double open, double high, double low, double close, double vwap) {
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        std::cout << "[Connection][routeTickToModelManager] [thread_id: " << threadIdStr.str() << "] " << m_symbol << std::endl;
                                    
        // Only process if we have a ModelManager
        if (!m_modelManager) {
            return;
        }
        
        // If timestamp is 0, use current time
        if (timestamp == 0) {
            timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        }
        
        // Use the enhanced ConnectionCache method to merge data and track tick-by-tick changes
        auto cacheResult = m_connectionCache->mergeWithCacheAndTrackChanges(
            m_symbol,
            timestamp,
            midPoint,
            1, // trade volume is handeled by the volume_profile_map and added to the data model later
            bid,
            ask,
            bidSize,
            askSize,
            exchange,
            open,
            high,
            low,
            close,
            vwap
        );
        
        // Prune old entries from the cache (keep only last 60 minutes)
        m_connectionCache->pruneOldEntries(60);
        
        // Handle fallback pricing logic
        stock_data_tick::StockData stockData = cacheResult.data;
        
        // Store the midPoint for future fallback use if needed
        if (midPoint > 0) {
            m_lastPrice = midPoint;
        }
        
        // Only use fallback if cache couldn't establish a last price AND we have no current data
        if (stockData.last == 0 && m_lastPrice > 0) {
            // Use the last known price as final fallback
            stockData.last = m_lastPrice;
            std::cout << "[Connection] Using last known midPoint price as final fallback for " << m_symbol << std::endl;
        }
        
        // NEW LOGIC: Send to ModelManager when data is complete AND/OR tick-by-tick data changed
        bool shouldSendData = cacheResult.isComplete && cacheResult.tickByTickChanged;
        
        if (shouldSendData) {
            // Send to ModelManager
            m_modelManager->addTick(stockData);
        } else {
            // Log why we're not sending
            if (!cacheResult.isComplete) {
                std::cout << "[Data][" << m_symbol << "] Incomplete data, waiting for more fields before processing" << std::endl;
            } else if (!cacheResult.tickByTickChanged) {
                std::cout << "[Data][" << m_symbol << "] Data complete but no tick-by-tick changes, skipping" << std::endl;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////
    // STRING DATA CALLBACK : Sends VWAP & VOLUME(Total Market) to ModelManager ✅
    // (Individual trade volume is handeled by the volume_profile_map)
    // Processes string data (primarily timestamps) and routes relevant info to ModelManager
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::tickString(TickerId tickerId, TickType field, const std::string& value) {
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        std::cout << "[Connection][tickString] [thread_id: " << threadIdStr.str() << "] " << m_symbol << std::endl;
        // Delegate to frame analyzer for clean processing
        if (m_frameAnalyzer) {
            auto result = m_frameAnalyzer->analyzeTickStringData(tickerId, field, value);
            
            // Print what we decoded instead of routing to ModelManager
            if (result.hasDecodedData) {
                if (result.volume > 0.0 || result.vwap > 0.0) {
                    // std::cout << "[TickString] Decoded:\n"
                    //             << "Type: " << result.dataType
                    //             << "[Volume (Total Market): " << std::fixed << std::setprecision(2) << result.volume 
                    //             << "M, VWAP: $" << std::fixed << std::setprecision(5) << result.vwap << "]" << std::endl;
                }
            }
            
            // Route to ModelManager (moved inside the if block where result is available)
            // m_requestId is set in model_manager and passed in when initial connection is made: see model_manager.cpp line 164
            if (m_modelManager && tickerId == m_requestId) {
                routeTickToModelManager(0, result.volume, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, result.vwap);
            }
        }
    }

     ///////////////////////////////////////////////////////////////////////////
     // REAL-TIME BAR DATA CALLBACK : Passes OHLC to model manager for every 5 seconds ✅
     // Processes OHLC, volume and WAP from 5-second bars and routes to ModelManager
     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::realtimeBar(TickerId reqId, long time, double open, double high, 
                             double low, double close, Decimal volume, Decimal wap, int count) {
        
        // Step 1: Convert the callback parameters to our clean structure using the frame analyzer
        time_t currentTime = std::time(nullptr);
        // Get analyzed data instead of just calling analyze
        auto analyzedData = m_frameAnalyzer->analyzeRealtimeBarData(
            reqId, 
            static_cast<int>(time), 
            open, 
            high, 
            low, 
            close, 
            DecimalFunctions::decimalToDouble(volume), 
            DecimalFunctions::decimalToDouble(wap), 
            count
        );
                // Step 2: Create raw message simulation for decoder processing
        // Note: In a real implementation, this would be the actual message bytes
        // For now, we simulate by creating a data structure
        ibkr_decoder::IBKRDecoder::RealtimeBarData barData;
        barData.version = 1; // Default version
        barData.reqId = reqId;
        barData.time = static_cast<int>(time);
        barData.open = open;
        barData.high = high;
        barData.low = low;
        barData.close = close;
        barData.volume = volume;
        barData.wap = wap;
        barData.count = count;


        // Step 3: Process through decoder for validation and additional analysis
        // In a real implementation, this would decode from raw bytes
        // std::cout << "[Connection] Decoder processing complete for realtime bar data" << std::endl;
        
        // Step 4: Print the beautiful analyzed data in the callback
        // std::cout << "\n========== REALTIME BAR ANALYSIS ==========" << std::endl;
        // std::cout << "Symbol: " << m_symbol << " | Request ID: " << analyzedData.reqId << std::endl;
        // std::cout << "Bar Time: " << analyzedData.formattedTime << " (epoch: " << analyzedData.epochTime << ")" << std::endl;
        // std::cout << "Time Difference: " << (currentTime - analyzedData.epochTime) << " seconds" << std::endl;
        // std::cout << "Open: $" << std::fixed << std::setprecision(4) << analyzedData.open << std::endl;
        // std::cout << "High: $" << std::fixed << std::setprecision(4) << analyzedData.high << std::endl;
        // std::cout << "Low: $" << std::fixed << std::setprecision(4) << analyzedData.low << std::endl;
        // std::cout << "Close: $" << std::fixed << std::setprecision(4) << analyzedData.close << std::endl;
        // std::cout << "Volume: " << std::fixed << std::setprecision(0) << analyzedData.volume << " shares" << std::endl;
        // std::cout << "WAP: $" << std::fixed << std::setprecision(6) << analyzedData.wap << std::endl;
        // std::cout << "Trade Count: " << analyzedData.count << std::endl;
        
        // Print calculated metrics if valid
        // if (analyzedData.hasValidPriceChange) {
        //     std::cout << "Price Change: $" << std::fixed << std::setprecision(4) << analyzedData.priceChange 
        //               << " (" << std::showpos << std::fixed << std::setprecision(2) << analyzedData.percentChange << "%)" << std::noshowpos << std::endl;
        // }
        
        // if (analyzedData.hasValidRange) {
        //     std::cout << "Bar Range: $" << std::fixed << std::setprecision(4) << analyzedData.barRange << std::endl;
        // }
        
        // std::cout << "===========================================" << std::endl;
        
        // Step 5: Keep routing commented out as requested
        if (m_modelManager && (reqId >= 6000 && reqId < 7000)) {
            routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", analyzedData.open, analyzedData.high, analyzedData.low, analyzedData.close, 0);
        }
    }


    ////////////////////////////////////////////////////////////////////////////////////
    // TICK-BY-TICK TRADE DATA CALLBACK : USED FOR VOLUME PROFILE, NOT MODEL MANAGER ✅
    // Processes detailed trade information and routes to ModelManager
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
                                    Decimal size, const TickAttribLast& tickAttribLast, 
                                    const std::string& exchange, const std::string& specialConditions) {
        
        // Step 1: Convert the callback parameters to our clean structure using the frame analyzer
        // Get analyzed data instead of just calling analyze
        auto analyzedData = m_frameAnalyzer->analyzeTickByTickData(
            reqId, 
            tickType, 
            time, 
            price, 
            DecimalFunctions::decimalToDouble(size),  // Convert Decimal to double for analysis
            exchange, 
            specialConditions, 
            tickAttribLast.pastLimit, 
            tickAttribLast.unreported
        );
        
        // Step 2: Create raw message simulation for decoder processing
        // Note: In a real implementation, this would be the actual message bytes
        // For now, we simulate by creating a data structure
        ibkr_decoder::IBKRDecoder::TickByTickData tickData;
        tickData.reqId = reqId;
        tickData.tickType = tickType;
        tickData.time = time;
        tickData.price = price;
        tickData.size = size;
        tickData.attrMask = (tickAttribLast.pastLimit ? 1 : 0) | (tickAttribLast.unreported ? 2 : 0);
        tickData.exchange = exchange;
        tickData.specialConditions = specialConditions;
        tickData.pastLimit = tickAttribLast.pastLimit;
        tickData.unreported = tickAttribLast.unreported;
        
        // Step 3: Process through decoder for validation and additional analysis
        // In a real implementation, this would decode from raw bytes
        // std::cout << "[Connection] Decoder processing complete for tick-by-tick data" << std::endl;
        
        // Step 4: Print the beautiful analyzed data in the callback
        // std::cout << "\n========== TICK-BY-TICK TRADE ANALYSIS ==========" << std::endl;
        // std::cout << "Symbol: " << m_symbol << " | Request ID: " << analyzedData.reqId << std::endl;
        // std::cout << "Trade Time: " << analyzedData.formattedTime << " (epoch: " << analyzedData.epochTime << ")" << std::endl;
        // std::cout << "Tick Type: " << analyzedData.tickType << " (" << (analyzedData.tickType == 1 ? "Last" : "AllLast") << ")" << std::endl;
        // std::cout << "Price: $" << std::fixed << std::setprecision(4) << analyzedData.price << std::endl;
        // std::cout << "Volume: " << std::fixed << std::setprecision(0) << analyzedData.volume << " shares" << std::endl;
        // std::cout << "Exchange: " << (analyzedData.exchange.empty() ? "N/A" : analyzedData.exchange) << std::endl;
        
        // Print special conditions if present
        // if (!analyzedData.specialConditions.empty()) {
        //     std::cout << "Special Conditions: " << analyzedData.specialConditions << std::endl;
        // }
        
        // // Print flags if set
        // if (analyzedData.pastLimit) {
        //     std::cout << "⚠️ Trade Past Limit" << std::endl;
        // }
        // if (analyzedData.unreported) {
        //     std::cout << "⚠️ Unreported Trade" << std::endl;
        // }
        
        // Print calculated metrics if valid
        // if (analyzedData.hasValidTrade) {
        //     std::cout << "Dollars Traded: $" << std::fixed << std::setprecision(2) << analyzedData.dollarsTraded << std::endl;
        // }
        
        // std::cout << "===============================================" << std::endl;
        
        // Step 5: Route tick-by-tick trade data to ModelManager for volume profile
        // Note: Tick-by-tick data uses different request IDs (7001) than market data (10000+)
        if (m_modelManager && reqId >= 7000 && reqId < 8000) {
             // Handle individual trade data from tickByTickAllLast to build the volume profile in model manager
            if (price > 0 && analyzedData.volume > 0) {
                // std::cout << "[IndividualTrade][" << m_symbol << "] "
                //         << "Processing individual trade: " << analyzedData.volume << " shares at $" << price 
                //         << " (Conditions: " << (!specialConditions.empty() ? specialConditions : "none") << ")" << std::endl;
                
                // Send individual trade data directly to ModelManager (separate from cache)
                m_modelManager->addTradeTick(analyzedData.price, analyzedData.volume);
            }
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////
    // TICK-BY-TICK BID-ASK DATA CALLBACK : Provides Model Manager - BID, ASK, BID SIZE, ASK SIZE & EPOCH TIME ✅
    // Processes detailed bid-ask information and routes to ModelManager
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, 
                                    Decimal bidSize, Decimal askSize, 
                                    const TickAttribBidAsk& tickAttribBidAsk) {
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        std::cout << "[Connection][tickByTickBidAsk] [thread_id: " << threadIdStr.str() << "] " << m_symbol << std::endl;
        // Step 1: Convert the callback parameters to our clean structure using the frame analyzer
        // Get analyzed data instead of just calling analyze
        auto analyzedData = m_frameAnalyzer->analyzeTickByTickBidAskData(
            reqId, 
            time, 
            bidPrice, 
            askPrice,
            DecimalFunctions::decimalToDouble(bidSize),  // Convert Decimal to double for analysis
            DecimalFunctions::decimalToDouble(askSize),  // Convert Decimal to double for analysis
            tickAttribBidAsk.bidPastLow, 
            tickAttribBidAsk.askPastHigh
        );
        
        
        // Step 3: Process through decoder for validation and additional analysis
        // In a real implementation, this would decode from raw bytes
        // std::cout << "[Connection] Decoder processing complete for tick-by-tick bid-ask data" << std::endl;
        
        // Step 4: Print the beautiful analyzed data in the callback
        // std::cout << "\n========== TICK-BY-TICK BID-ASK ANALYSIS ==========" << std::endl;
        // std::cout << "Symbol: " << m_symbol << " | Request ID: " << analyzedData.reqId << std::endl;
        // std::cout << "Quote Time: " << analyzedData.formattedTime << " (epoch: " << analyzedData.epochTime << ")" << std::endl;
        // std::cout << "Bid: $" << std::fixed << std::setprecision(4) << analyzedData.bidPrice 
        //           << " x " << std::fixed << std::setprecision(0) << analyzedData.bidSize << std::endl;
        // std::cout << "Ask: $" << std::fixed << std::setprecision(4) << analyzedData.askPrice 
        //           << " x " << std::fixed << std::setprecision(0) << analyzedData.askSize << std::endl;
        
        // Print flags if set
        // if (analyzedData.bidPastLow) {
        //     std::cout << "⚠️ Bid Past Low" << std::endl;
        // }
        // if (analyzedData.askPastHigh) {
        //     std::cout << "⚠️ Ask Past High" << std::endl;
        // }
        
        // Print calculated metrics if valid
        if (analyzedData.hasValidSpread) {
            // std::cout << "Spread: $" << std::fixed << std::setprecision(4) << analyzedData.spread 
            //           << " (" << std::fixed << std::setprecision(2) << analyzedData.spreadPercent << "%)" << std::endl;
        }
        
        if (analyzedData.hasValidMidPoint) {
            // std::cout << "Mid Point: $" << std::fixed << std::setprecision(4) << analyzedData.midPoint << std::endl;
        }
        
        // std::cout << "===============================================" << std::endl;
        
        // Step 5: Route tick-by-tick bid-ask data to ModelManager
        // Note: Tick-by-tick data uses different request IDs (7002) than market data (10000+)
        // so we check for the tick-by-tick ID range instead of m_requestId
        if (m_modelManager && reqId >= 7000 && reqId < 8000) {
            routeTickToModelManager(analyzedData.midPoint, 0, analyzedData.epochTime, analyzedData.bidPrice, analyzedData.askPrice, analyzedData.bidSize , analyzedData.askSize, "", "", 0, 0, 0, 0, 0);
        }
    }

    void IBKRTrader::error(int id, long errorTime, int errorCode,
                           const std::string& errorString,
                           const std::string& advancedOrderRejectJson) {
        std::cerr << "[ERROR] ReqId: " << id 
                  << " Code: " << errorCode 
                  << " Msg: " << errorString << std::endl;
                  
        // Simplified error handling - remove delayed data fallbacks
        if (errorCode == 162) {
            std::cerr << "[INFO] Historical data request error." << std::endl;
        } else if (errorCode == 200) {
            std::cerr << "[INFO] No security definition found. "
                      << "Check the contract details and symbol." << std::endl;
        } else if (errorCode == 10092) {
            std::cerr << "[INFO] Deep market data is not supported for this security. "
                      << "Using top-of-book data instead." << std::endl;
            
            // If it's a market depth error, try to fall back to regular market data
            if (id >= 8000 && id < 9000) { // Market depth request IDs are in this range
                // First get the symbol from the error ID context (this is simplified)
                std::string symbolForFallback = m_symbol;
                
                if (!symbolForFallback.empty()) {
                    std::cout << "[FALLBACK] Cannot get market depth for " << symbolForFallback 
                              << ". Using regular market data instead." << std::endl;
                    
                    // Request basic market data as fallback
                    // static int fallbackId = 7500;
                    // requestMarketData(symbolForFallback);
                }
            }
        } else if (errorCode == 10189) {
            std::cerr << "[INFO] Failed to request tick-by-tick data. "
                      << "Verify your market data subscription." << std::endl;
            
            // If it's a tick-by-tick error, try to fall back to regular market data
            if (id >= 7000 && id < 8000) { // Tick-by-tick request IDs are in this range
                // Get the symbol from the context
                std::string symbolForFallback = m_symbol;
                
                if (!symbolForFallback.empty()) {
                    std::cout << "[FALLBACK] Cannot get tick-by-tick data for " << symbolForFallback 
                              << ". Using regular market data instead." << std::endl;
                }
            }
        } else if (errorCode == 321) {
            std::cerr << "[INFO] Historical data request format error. "
                      << "Check duration format (should be 'X Y' where X is integer and Y is unit)." << std::endl;
            
            // If it's a historical data error, log it and move on
            if (id >= 4000 && id < 5000) { // Historical data request IDs are in this range
                std::cout << "[FALLBACK] Historical data request failed. "
                          << "Continuing with available real-time data." << std::endl;
            }
        }
                  
        // If we have a ModelManager, log symbol-specific errors
        if (m_modelManager && id == m_requestId) {
            std::cerr << "[ERROR] Error for symbol " << m_symbol << ": " << errorString << std::endl;
        }
    }

} // namespace connection


// Legend:
// “owns →” arrows denote “parent thread creates and manages the lifecycle of the child thread.”
// Left‐pointing arrows ← denote data or control flowing from one component into another.
// All components listed under a thread box execute on that thread ID.
// 🧵  Main Thread
// │
// ├─ main.cpp / main()    ←──────────────────────── input_manager (program entry-point logic - receives user input from input_manager)
// │
// └──app_state.cpp / main()   ←───manager classes   (recieve thread requests like an api from manager classes - manages thread lifcycle in one place)
//         │
//         └── owns ──▶ 🧵 Thread #1
//                 │
//                 ├─ InputManager              ←──────────────────────── Local API
//                 │
//                 ├─  Local API (HTTP server)  ←──────────────────────────────── External_clients
//                 │
//                 └─ ModelManagerFactory (factory pattern generates model manager)
//                         │
//                         └── owns ──▶ 🧵 Thread #2
//                                 │
//                                 └─ model_manager(singleton) ←──────────────────────── time_ordered_tick_buffer, app_state, connection_manager, raw_data_model, volume_profile_map , ring_buffer_trade_handler
//                                         │
//                                         ├─ stock_data_tick ←────────────────────────  connection
//                                         │
//                                         ├─time_ordered_tick_buffer  ←──────────────────────── stock_data_tick
//                                         │
//                                         ├─ time_ordered_tick_buffer (signal generation) ←────────── ring_buffer_trade_handler
//                                         │
//                                         ├─ ring_buffer_trade_handler             ←──────────────────────── stock_data_tick, time_ordered_tick_buffer
//                                         │
//                                         ├─ connection
//                                         │     └──connection.cpp         ←──────────────────────── connection_cache, frame_analyzer, decoder, account_summary
//                                         │
//                                         ├─ connection_manager
//                                         │     └──connection_manager.cpp  ←──────────── connection
//                                         │
//                                         ├─ connection_cache
//                                         │       └──connection_cache.cpp  ←──────────────────────── stock_data_tick
//                                         │
//                                         ├─ decoder
//                                         │     ├─ frame_analyzer.cpp  ←──────────────────────── decoder
//                                         │     └──decoder.cpp  ←──────────────────────── frame_analyzer
//                                         │
//                                         ├─ models
//                                         │     ├─ stock_data_tick  
//                                         │     ├─ volume_profile_map                                 
//                                         │     └─ raw_data_model     ←──────────────────────── STK_Q, stock_data_tick
//                                         ├─ STK_Q
//                                         │
//                                         └─ position_handler (risk manager / P&L / Logs)  ←──────────────────────── ring_buffer_trade_handler 
                                                                