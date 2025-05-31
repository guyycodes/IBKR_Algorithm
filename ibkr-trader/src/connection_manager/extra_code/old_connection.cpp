// #include "connection.hpp"
// #include "../connection_cache/connection_cache.hpp"
// #include "../../models/model_manager.hpp"  // Include ModelManager for direct access
// #include <iostream>
// #include <chrono>
// #include <sstream>
// #include <iomanip>  // For std::hex, std::setw, etc.
// #include "../decoder/decoder.hpp"  // Include IBKRDecoder for special size value handling
// #include "../decoder/frame_analyzer.hpp"  // Include FrameAnalyzer for tick string field 48 analysis

// namespace connection {
//     //----------------------------------------------------------------------------------------------------------------
//     //  Connection parameters
//     //----------------------------------------------------------------------------------------------------------------
//     /// Hostname used to reach the IB Gateway container running on the same machine.
//     const char* HOST = "host.docker.internal";

//     /// TWS / IB Gateway port for *paper* trading (live trading would be 7496).
//     int PORT = 4002;
//     int client_id = 0;  // Default client ID - we'll override this in connect()
//     //----------------------------------------------------------------------------------------------------------------
//     //  Scalping‑specific tunables (milliseconds)
//     //----------------------------------------------------------------------------------------------------------------
//     int ACCOUNT_REFRESH_INTERVAL_MS   = 30'000;  ///< Interval between account‑summary refreshes
//     int REFRESH_THREAD_SLEEP_MS       = 100;     ///< Sleep granularity inside the refresh worker
    
//     //================================================================================================================
//     //  IBKRTrader – ctor / dtor
//     //================================================================================================================

//     IBKRTrader::IBKRTrader()
//         : m_osSignal(2000),
//           m_client(new EClientSocket(this, &m_osSignal)),            // registers *this* as the EWrapper callback
//           m_frameAnalyzer(std::make_unique<ibkr_frame_analyzer::FrameAnalyzer>()),
//           m_decoder(std::make_unique<ibkr_decoder::IBKRDecoder>(*m_frameAnalyzer)),
//           m_connectionCache(std::make_unique<ConnectionCache>(*m_decoder)),
//           m_modelManager(nullptr),
//           m_requestId(-1),
//           m_lastPrice(0.0),
//           m_lastShareSize(0.0),
//           m_symbol(""),
//           m_contract()
//     {
//     }
    
//     IBKRTrader::~IBKRTrader()
//     {
//         // Ensure any streaming subscriptions are cancelled before tearing down.
//         stopScalpingDataStream();

//         delete m_client;  // EClientSocket is raw‑owned (consistent with IBKR sample code).
//     }
    
//     /**
//      * Establish a connection to the IB Gateway and remember the *symbol* / *contract* combination for the initial
//      * market‑data subscription triggered by `startDataStream()`.
//      *
//      * @param clientId User‑supplied client‑ID or < 0 to use the default.
//      * @return         `true` if the low‑level TCP connection is up (handshake completion is async).
//      */
//     bool IBKRTrader::connect(int clientId, const std::string& symbol, const Contract& contract)
//     {
//         m_client->setConnectOptions("+PACEAPI");  // enables quicker pacing limits & extra features

//         const int actualClientId = (clientId >= 0) ? clientId : client_id;
//         const bool success      = m_client->eConnect(HOST, PORT, actualClientId, /*extraAuth=*/false);

//         if (success) {
//             std::cout << "[INFO] Connection initiated to " << HOST << ':' << PORT
//                       << " with client ID: " << actualClientId << std::endl;

//             // Give the network stack a brief moment; subsequent requests otherwise race occasionally.
//             std::this_thread::sleep_for(std::chrono::seconds(2));

//             m_symbol   = symbol;
//             m_contract = contract;
//         }
//         else {
//             std::cerr << "[ERROR] Failed to connect to IBKR." << std::endl;
//         }
//         return success;
//     }
    
//     /** Convenience wrapper – kicks off the scalping feed using the symbol remembered in `connect()`. */
//     void IBKRTrader::startDataStream()
//     {
//         startScalpingDataStream(m_symbol, m_contract);
//     }
    
//     /** Injects a ModelManager pointer for direct tick routing. */
//     void IBKRTrader::setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol)
//     {
//         m_modelManager = modelManager;
//         m_symbol       = symbol;
//         std::cout << "[IBKRTrader] Set ModelManager for symbol: " << m_symbol << std::endl;
//     }

//     //================================================================================================================
//     //  EReader – message pump helpers
//     //================================================================================================================

//     /**
//      * Factory – create a dedicated EReader (IBKR's background dispatcher) that pulls messages off the socket.
//      * Memory is RAII‑managed via `std::unique_ptr`.
//      */
//     std::unique_ptr<EReader> IBKRTrader::createReader()
//     {
//         auto reader = std::make_unique<EReader>(m_client, &m_osSignal);
//         reader->start();
//         return reader;
//     }
    
//     /**
//      * Spins up a thread that waits on the reader's condition variable and processes messages as soon as they arrive.
//      * The lambda captures `this` to forward the callbacks back onto the IBKRTrader (our EWrapper implementation).
//      */
//     std::thread IBKRTrader::startMessageProcessing(std::unique_ptr<EReader>& reader)
//     {
//         return std::thread([this, &reader]() {
//             while (m_client->isConnected()) {
//                 m_osSignal.waitForSignal();   // block until EClientSocket notifies that data is ready
//                 reader->processMsgs();        // decode & dispatch –> EWrapper v‑table
//             }
//         });
//     }
    
//     //================================================================================================================
//     //  Disconnect & cleanup
//     //================================================================================================================

//     void IBKRTrader::disconnect()
//     {
//         // 1) Stop outstanding streams first (guards against call‑backs hitting deleted resources).
//         stopScalpingDataStream();

//         // 2) Cancel an account summary subscription if active.
//         if (m_client && m_client->isConnected() && m_lastAccountSummaryReqId > 9000) {
//             std::cout << "[INFO] Canceling active account summary request with reqId: "
//                       << m_lastAccountSummaryReqId << std::endl;
//             m_client->cancelAccountSummary(m_lastAccountSummaryReqId);
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));  // give IBKR a tick
//         }

//         // 3) Reset any locally cached account data.
//         m_accountSummaryManager.clear();

//         // 4) Finally drop the TCP session.
//         if (m_client) {
//             m_client->eDisconnect();
//         }
//         std::cout << "[INFO] Disconnected from IBKR." << std::endl;
//     }

//     //================================================================================================================
//     //  Scalping data helpers
//     //================================================================================================================

//     /**
//      * Launches all subscriptions required by the scalping strategy *and* spawns a background thread that refreshes
//      * account‑level information every `ACCOUNT_REFRESH_INTERVAL_MS`.
//      */
//     void IBKRTrader::startScalpingDataStream(const std::string& symbol, const Contract& contract)
//     {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot start scalping data stream: not connected" << std::endl;
//             return;
//         }

//         // Cancel any previous stream so we do not leak threads or duplicate subscriptions.
//         stopScalpingDataStream();

//         m_stopDataStream    = false;
//         m_lastAccountUpdate = std::chrono::high_resolution_clock::now();

//         std::cout << "[INFO] Making initial data subscriptions for " << symbol << std::endl;
//         requestScalpingData(symbol, contract);

//         //------------------------------------------------------------------------------------------------------------
//         //  Background refresh worker – solely responsible for periodic account‑summary updates
//         //------------------------------------------------------------------------------------------------------------
//         m_dataRefreshThread = std::thread([this]() {
//             std::cout << "[INFO] Starting account data refresh thread" << std::endl;

//             while (!m_stopDataStream && m_client->isConnected()) {
//                 const auto now = std::chrono::high_resolution_clock::now();

//                 const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastAccountUpdate)
//                                           .count();

//                 if (elapsed >= ACCOUNT_REFRESH_INTERVAL_MS) {
//                     std::cout << "[Timer] Refreshing account data" << std::endl;
//                     requestAccountSummary();
//                     m_lastAccountUpdate = now;
//                 }

//                 std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_THREAD_SLEEP_MS));
//             }

//             std::cout << "[INFO] Account data refresh thread stopped" << std::endl;
//         });
//     }

//     /**
//      * Subscribes *once* to all market‑data feeds needed by the scalping model.  Continuous streams (tick‑by‑tick,
//      * market‑depth, real‑time bars) are left running until `stopScalpingDataStream()` is invoked.
//      */
//     void IBKRTrader::requestScalpingData(const std::string& symbol, const Contract& contract)
//     {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request scalping data: not connected" << std::endl;
//             return;
//         }

//         std::cout << "\n==================================================\n"
//                   << "SUBSCRIBING TO SCALPING DATA FOR: " << symbol << "\n"
//                   << "==================================================" << std::endl;

//         //--------------------------------------------------------------------
//         // 0) Ensure we receive REAL‑TIME rather than delayed quotes
//         //--------------------------------------------------------------------
//         m_client->reqMarketDataType(1);  // 1 = REALTIME (3 = DELAYED)

//         //--------------------------------------------------------------------
//         // 1) Account summary – will be periodically refreshed by the timer
//         //--------------------------------------------------------------------
//         std::cout << "[1] Requesting Account Information" << std::endl;
//         requestAccountSummary();

//         //--------------------------------------------------------------------
//         // 2) Tick‑by‑tick data (Last + Bid/Ask) – continuous
//         // reqTickByTickData triggers: tickByTickAllLast, tickByTickBidAsk, tickByTickMidPoint
//         //--------------------------------------------------------------------
//         static int tickRequestId = 7'001;
//         std::cout << "[2] Subscribing to Tick‑by‑Tick Data for " << symbol << std::endl;
//         requestTickByTickData(tickRequestId++, symbol, "AllLast", 0, /*ignoreSize=*/true, contract);
//         requestTickByTickData(tickRequestId++, symbol, "BidAsk",  0, /*ignoreSize=*/true, contract);

//         //--------------------------------------------------------------------
//         // 3) Level‑II / Market depth – continuous
//         //--------------------------------------------------------------------
//         static int depthReqId = 8'001;
//         std::cout << "[3] Subscribing to Market Depth for " << symbol << std::endl;
//         m_client->reqMktDepth(depthReqId++, contract, /*numRows=*/5, /*isSmartDepth=*/false, {});

//         //--------------------------------------------------------------------
//         // 4) Real‑time bars (5‑second OHLC) – continuous
//         //--------------------------------------------------------------------
//         static int barRequestId = 6'001;
//         std::cout << "[4] Subscribing to Real‑time Bar Data for " << symbol << std::endl;
//         m_client->reqRealTimeBars(barRequestId++, contract, /*barSize=*/5, "TRADES", /*useRTH=*/true, {});

//         std::cout << "\n==================================================\n"
//                   << "DATA SUBSCRIPTIONS ACTIVE - RECEIVING CALLBACKS\n"
//                   << "==================================================" << std::endl;
//     }
    
//     // // Stop the data refresh thread
//     void IBKRTrader::stopScalpingDataStream() {
//         // Signal the thread to stop
//         m_stopDataStream = true;
        
//         // Join the thread if it's running
//         if (m_dataRefreshThread.joinable()) {
//             std::cout << "[INFO] Waiting for data refresh thread to stop..." << std::endl;
//             m_dataRefreshThread.join();
//             std::cout << "[INFO] Data refresh thread stopped" << std::endl;
//         }
//     }
    
//         // Route tick market data to ModelManager/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::routeTickToModelManager(double price, double volume, uint64_t timestamp,
//                                            double bid, double ask, double bidSize, double askSize,
//                                            const std::string& exchange, const std::string& specialConditions,
//                                            double open, double high, double low, double close, double wap, double lastPrice, double lastShareSize) {
//         // Only process if we have a ModelManager
//         if (!m_modelManager) {
//             return;
//         }
        
//         // If timestamp is 0, use current time
//         if (timestamp == 0) {
//             timestamp = std::chrono::system_clock::now().time_since_epoch().count();
//         }
        
//         // Process potential special size values using the ConnectionCache
//         volume = m_connectionCache->decodeSpecialValue(volume, static_cast<int>(TickType::VOLUME));
//         bidSize = m_connectionCache->decodeSpecialValue(bidSize, static_cast<int>(TickType::BID_SIZE));
//         askSize = m_connectionCache->decodeSpecialValue(askSize, static_cast<int>(TickType::ASK_SIZE));
        
//         // Use the ConnectionCache to merge new data with cached data
//         stock_data_tick::StockData stockData = m_connectionCache->mergeWithCache(
//             m_symbol,
//             timestamp,
//             price,
//             volume,
//             bid,
//             ask,
//             bidSize,
//             askSize,
//             exchange,
//             open,
//             high,
//             low,
//             close,
//             wap
//         );
        
//         // Handle individual trade data from tickByTickAllLast separately
//         if (lastPrice > 0 && lastShareSize > 0) {
//             std::cout << "[IndividualTrade][" << m_symbol << "] "
//                       << "Processing individual trade: " << lastShareSize << " shares at $" << lastPrice 
//                       << " (Conditions: " << (!specialConditions.empty() ? specialConditions : "none") << ")" << std::endl;
            
//             // Send individual trade data directly to ModelManager (separate from cache)
//             m_modelManager->addIndividualTrade(lastPrice, lastShareSize);
//             return; // Early return - don't process as regular market data
//         }
        
//         // Prune old entries from the cache (keep only last 60 minutes)
//         m_connectionCache->pruneOldEntries(60);
        
//         // If we have a new "last" price but no bid/ask, use it to update the last price
//         if (price > 0 && stockData.bid == 0 && stockData.ask == 0) {
//             m_lastPrice = price;
//         }
        
//         // If we have bid/ask but no last price, use midpoint
//         if (stockData.last == 0 && bid > 0 && ask > 0) {
//             stockData.last = (bid + ask) / 2.0;
//         } else if (stockData.last == 0 && m_lastPrice > 0) {
//             // If no new price and no bid/ask, use the last known price
//             stockData.last = m_lastPrice;
//         }
        
//         // Check if we have a complete data element before processing
//         bool isComplete = (stockData.last > 0 && 
//                           stockData.bid > 0 && 
//                           stockData.ask > 0 && 
//                           stockData.bidSize > 0 && 
//                           stockData.askSize > 0);
        
//         if (isComplete) {
        
//         // Get thread ID for logging
//         std::stringstream threadIdStr;
//         threadIdStr << std::this_thread::get_id();
        
//         // Log the data being sent to ModelManager (compact format)
//         std::cout << "[Data][" << m_symbol << "] "
//                   << "L:" << (stockData.last > 0 ? std::to_string(stockData.last) : "-") << " "
//                   << "B:" << (stockData.bid > 0 ? std::to_string(stockData.bid) : "-") << " "
//                   << "A:" << (stockData.ask > 0 ? std::to_string(stockData.ask) : "-") << " "
//                   << "V:" << (stockData.volume > 0 ? std::to_string(stockData.volume) : "-") << " "
//                   << "BS:" << (stockData.bidSize > 0 ? std::to_string(stockData.bidSize) : "-") << " "
//                   << "AS:" << (stockData.askSize > 0 ? std::to_string(stockData.askSize) : "-") << " "
//                   << "WAP:" << (stockData.wap > 0 ? std::to_string(stockData.wap) : "-") << " "
//                   << "OHLC:" << (stockData.open > 0 ? std::to_string(stockData.open) : "-") << "/"
//                             << (stockData.high > 0 ? std::to_string(stockData.high) : "-") << "/"
//                             << (stockData.low > 0 ? std::to_string(stockData.low) : "-") << "/"
//                             << (stockData.close > 0 ? std::to_string(stockData.close) : "-") << " "
//                   << "Ex:" << (!stockData.exchange.empty() ? stockData.exchange : "-") << " "
//                   << "Cond:" << (!specialConditions.empty() ? specialConditions : "-") << " " 
//                   << "Time:" << (stockData.timestamp > 0 ? std::to_string(stockData.timestamp) : "-") << " "
//                   << "WAP:" << (stockData.wap > 0 ? std::to_string(stockData.wap) : "-") << "\n ready to send" << "\n" << std::endl;
        
//         // Add debug output to see if we have any OHLC data
//         if (stockData.open > 0 || stockData.high > 0 || stockData.low > 0 || stockData.close > 0) {
//             std::cout << "[DEBUG][OHLC] Received valid OHLC data: " 
//                       << stockData.open << "/" << stockData.high << "/" << stockData.low << "/" << stockData.close << std::endl;
//         }
        
//         // Add debug output for WAP
//         if (stockData.wap > 0) {
//             std::cout << "[DEBUG][WAP] StockData with WAP: " << stockData.wap << " ready to send" << std::endl;
//         }
        
//         // Send to ModelManager
//         m_modelManager->addTick(stockData);
//         } else {
//             // Log that we're skipping incomplete data
//             std::cout << "[Data][" << m_symbol << "] Incomplete data, waiting for more fields before processing" << std::endl;
//         }
//     }

//     ///////////////////////////////////////////////////////////////////////////
//     // STRING DATA CALLBACK (GIVE US VWAP & TOTAL MARKET VOLUME) ✅
//     // Processes string data (primarily timestamps) and routes relevant info to ModelManager
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::tickString(TickerId tickerId, TickType field, const std::string& value) {
//         // Delegate to frame analyzer for clean processing
//         if (m_frameAnalyzer) {
//             auto result = m_frameAnalyzer->analyzeTickStringData(tickerId, field, value);
            
//             // Print what we decoded instead of routing to ModelManager
//             if (result.hasDecodedData) {
//                 if (result.volume > 0.0 || result.vwap > 0.0) {
//                     std::cout << "[TickString] Decoded:\n"
//                                 << "Type: " << result.dataType
//                                 << "[Volume (Total Market): " << std::fixed << std::setprecision(2) << result.volume 
//                                 << "M, VWAP: $" << std::fixed << std::setprecision(5) << result.vwap << "]" << std::endl;
//                 }
                
//                 std::cout << std::endl;
//             }
//         }
        
//         // Comment out the routing to ModelManager for now
//         // if (m_modelManager && tickerId == m_requestId) {
//         //     routeTickToModelManager(0, 0, lastTimestamp, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         // }
//     }

//     ///////////////////////////////////////////////////////////////////////////
//     // PRICE DATA CALLBACK
//     // Processes price updates (BID, ASK, LAST) and routes them to FrameAnalyzer
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
//         // Only process live BID and ASK data - ignore delayed and OHLC
//         if (field != TickType::BID && field != TickType::ASK && field != TickType::LAST) {
//             return;
//         }
        
//         // Delegate to frame analyzer for clean processing
//         if (m_frameAnalyzer) {
//             auto result = m_frameAnalyzer->analyzeTickPriceData(tickerId, field, price, attrib);
            
//             // Print what we decoded instead of routing to ModelManager
//             if (result.hasDecodedData) {
//                 std::cout << "[TickPrice] Decoded: "
//                           << "Type: " << result.dataType
//                           << " Price: $" << std::fixed << std::setprecision(5) << result.decodedPrice
//                           << "\nTickerId: " << result.tickerId << std::endl;
                
//                 std::cout << std::endl;
//             }
//         }
        
//         // Comment out all routing to ModelManager for now
//         // if (m_modelManager && tickerId == m_requestId) {
//         //     // Store the last price we've seen if it's a LAST price type
//         //     if (field == TickType::LAST) {
//         //         m_lastPrice = price;
//         //     }
//         //     
//         //     // Route data based on field type - COMMENTED OUT
//         //     // if (field == TickType::BID) {
//         //     //     routeTickToModelManager(0, 0, 0, price, 0, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     // }
//         //     // else if (field == TickType::ASK) {
//         //     //     routeTickToModelManager(0, 0, 0, 0, price, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     // }
//         //     // else if (field == TickType::LAST) {
//         //     //     routeTickToModelManager(price, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     // }
//         // }
//     }

//     void IBKRTrader::tickSize(TickerId tickerId, TickType field, Decimal size) {
//         // Only process fields we care about for scalping (BID_SIZE, ASK_SIZE, LAST_SIZE, VOLUME)
//         bool isRelevantField = (field == TickType::BID_SIZE || field == TickType::ASK_SIZE || 
//                                field == TickType::LAST_SIZE || field == TickType::VOLUME ||
//                                // Keep delayed types as fallback
//                                field == TickType::DELAYED_BID_SIZE || field == TickType::DELAYED_ASK_SIZE || 
//                                field == TickType::DELAYED_LAST_SIZE || field == TickType::DELAYED_VOLUME);
        
//         if (!isRelevantField) {
//             return; // Early return for irrelevant fields
//         }
        
//         // Delegate to frame analyzer for clean processing
//         if (m_frameAnalyzer) {
//             auto result = m_frameAnalyzer->analyzeTickSizeData(tickerId, static_cast<int>(field), static_cast<double>(size));
            
//             // Print what we decoded
//             if (result.hasDecodedData) {
//                 std::cout << "[TickSize] Decoded: "
//                           << "Type: " << result.dataType
//                           << " Size: " << std::fixed << std::setprecision(2) << result.decodedSize
//                           << " TickerId: " << result.tickerId << std::endl;
//             }
//         }
        
//         // Comment out the routing to ModelManager for now - just print decoded results
//         // if (m_modelManager && tickerId == m_requestId) {
//         //     double sizeValue = static_cast<double>(size);
//         //     
//         //     // Check if this is a special size value
//         //     if (m_decoder->isSpecialSizeValue(sizeValue)) {
//         //         sizeValue = m_decoder->interpretSizeValue(sizeValue, field);
//         //     }
//         //     
//         //     // Route data based on the field type
//         //     if (field == TickType::BID_SIZE || field == TickType::DELAYED_BID_SIZE) {
//         //         routeTickToModelManager(0, 0, 0, 0, 0, sizeValue, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     }
//         //     else if (field == TickType::ASK_SIZE || field == TickType::DELAYED_ASK_SIZE) {
//         //         routeTickToModelManager(0, 0, 0, 0, 0, 0, sizeValue, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     }
//         //     else if (field == TickType::LAST_SIZE || field == TickType::DELAYED_LAST_SIZE) {
//         //         routeTickToModelManager(m_lastPrice, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     }
//         //     else if (field == TickType::VOLUME || field == TickType::DELAYED_VOLUME) {
//         //         routeTickToModelManager(m_lastPrice, sizeValue, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     }
//         // }
//     }

//     void IBKRTrader::tickGeneric(TickerId tickerId, TickType field, double value) {
//         std::cout << "\n===== TICK GENERIC DATA =====\n";
//         std::cout << "tickerId: " << tickerId << "\n";
//         std::cout << "field (code): " << static_cast<int>(field);
        
//         // Map field code to readable name
//         std::string fieldName;
//         switch (field) {
//             case TickType::HALTED: fieldName = "HALTED"; break;
//             case TickType::AVG_VOLUME: fieldName = "AVG_VOLUME"; break;
//             case TickType::SHORTABLE: fieldName = "SHORTABLE"; break;
//             // WAP - weighted average price (field code typically 14)
//             case 14: fieldName = "WAP"; break;
//             // Delayed WAP (field code 64 + 14 = 78)
//             case 78: fieldName = "DELAYED_WAP"; break;
//             default: fieldName = "UNKNOWN_GENERIC_" + std::to_string(field); break;
//         }
        
//         std::cout << " (" << fieldName << ")\n";
//         std::cout << "value: " << value << "\n";
//         std::cout << "===========================\n";
        
//         // Original code
//         std::cout << "[IBKRTrader::tickGeneric] Raw callback - ID: " << tickerId
//                   << ", Field: " << static_cast<int>(field)
//                   << ", Value: " << value << std::endl;
                  
//         // Check if this is for our ModelManager's request ID
//         if (m_modelManager && tickerId == m_requestId) {
//             // Process WAP value (field code = 14, Delayed WAP = 78)
//             if (field == 14 || field == 78) {
//                 // Decode WAP if needed
//                 double decodedWap = value;
                
//                 // If WAP appears to be encoded (large exponent values)
//                 if (m_decoder->isSpecialSizeValue(decodedWap)) {
//                     std::cout << "[DEBUG] Decoding special WAP value in tickGeneric: " << decodedWap << std::endl;
//                     decodedWap = m_decoder->interpretSizeValue(decodedWap, field);
//                 }
                
//                 // Add debug output for WAP
//                 std::cout << "[DEBUG][WAP] Generic tick provided WAP: " << decodedWap 
//                           << " for " << m_symbol << " (original: " << value << ")" << std::endl;
                
//                 // Pass as WAP parameter
//                 routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, decodedWap, 0, 0);
//             }
//             // Also handle HALTED, AVG_VOLUME and SHORTABLE if needed in the future
//             else {
//                 // Pass other generic tick data
//                 routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//             }
//         }
//     }

//     //     ///////////////////////////////////////////////////////////////////////////
// //     // REAL-TIME BAR DATA CALLBACK
// //     // Processes OHLC, volume and WAP from 5-second bars and routes to ModelManager
// //     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::realtimeBar(TickerId reqId, long time, double open, double high, 
//                              double low, double close, Decimal volume, Decimal wap, int count) {
//         // Format time as human-readable
//         time_t epochTime = time;
//         char timeStr[30];
//         struct tm* timeinfo = localtime(&epochTime);
//         strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
//         std::cout << "\n======== REALTIME BAR DATA (" << reqId << ") ========\n";
//         std::cout << "TIME: " << timeStr << "\n";
//         std::cout << "OPEN: " << open << "\n";
//         std::cout << "HIGH: " << high << "\n";
//         std::cout << "LOW: " << low << "\n";
//         std::cout << "CLOSE: " << close << "\n";
//         std::cout << "VOLUME: " << static_cast<double>(volume) << "\n";
//         std::cout << "WAP: " << static_cast<double>(wap) << "\n";
//         std::cout << "COUNT: " << count << "\n";
//         std::cout << "==========================================\n";
        
//         // Original code continues...
//         std::cout << "[IBKRTrader::realtimeBar] Raw callback - ID: " << reqId
//                   << ", Time: " << timeStr
//                   << ", OHLC: " << open << "/" << high << "/" << low << "/" << close
//                   << ", Volume: " << static_cast<double>(volume)
//                   << ", WAP: " << static_cast<double>(wap)
//                   << ", Count: " << count << std::endl;
                  
//         // If we have a ModelManager, process the data
//         if (m_modelManager && (reqId >= 6000 && reqId < 7000)) {
//             // Create timestamp from the provided time_t
//             uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
//             // Process WAP value - decode if needed
//             double decodedWap = static_cast<double>(wap);
            
//             // If WAP appears to be encoded (like with the large exponent values we're seeing)
//             if (m_decoder->isSpecialSizeValue(decodedWap)) {
//                 std::cout << "[DEBUG] Decoding special WAP value in realtime bar: " << decodedWap << std::endl;
//                 // Use the size decoder since WAP appears to be encoded like sizes
//                 decodedWap = m_decoder->interpretSizeValue(decodedWap, 14); // 14 is WAP field code
//             }
            
//             // Route OHLC, WAP and other data - all at once for complete data
//             routeTickToModelManager(
//                 close,                         // Use close as the last price
//                 static_cast<double>(volume),   // Volume
//                 timestamp,                     // Timestamp
//                 0,                             // No bid in bar data
//                 0,                             // No ask in bar data
//                 0,                             // No bidSize in bar data
//                 0,                             // No askSize in bar data
//                 "",                            // No exchange info in bar data
//                 "",                            // No special conditions in bar data
//                 open,                          // Open
//                 high,                          // High
//                 low,                           // Low
//                 close,                         // Close
//                 decodedWap,                    // Decoded WAP
//                 0,                             // Last price
//                 0                              // Last share size
//             );

//             // Add debug output for WAP
//             if (decodedWap > 0) {
//                 std::cout << "[DEBUG][WAP] Real-time bar provided WAP: " << decodedWap 
//                           << " for " << m_symbol << " (original: " << static_cast<double>(wap) << ")" << std::endl;
//             }
//         }
//     }

// //     ///////////////////////////////////////////////////////////////////////////
// //     // TICK-BY-TICK TRADE DATA CALLBACK
// //     // Processes detailed trade information and routes to ModelManager
// //     // THIS PROVIDES REAL-TIME INDIVIDUAL TRADE VOLUMES (not cumulative)
// //     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
//                                     Decimal size, const TickAttribLast& tickAttribLast, 
//                                     const std::string& exchange, const std::string& specialConditions) {
        
//         // DECODE the BID64 Decimal to get actual volume using the abstracted decoder
//         double actualVolume = m_decoder->decodeTradeVolume(size);
        
//         // ANALYZE the tick-by-tick data using the abstracted analyzer
//         m_frameAnalyzer->analyzeTickByTickData(
//             reqId, tickType, time, price, 
//             static_cast<uint64_t>(size), actualVolume,
//             exchange, specialConditions,
//             tickAttribLast.pastLimit, tickAttribLast.unreported
//         );
        
//         // Check if this is for our ModelManager's request ID
//         if (m_modelManager && reqId == m_requestId) {
//             // Store the last price we've seen
//             m_lastPrice = price;
//             m_lastShareSize = actualVolume;
//             // Create timestamp from the provided time_t
//             // uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
//             // Route trade data through our central point with special conditions
//             routeTickToModelManager(
//                 0,
//                 0,  // Use the properly decoded volume
//                 0,
//                 0, // bid
//                 0, // ask
//                 0, // bidSize
//                 0, // askSize
//                 0,
//                 specialConditions, // pass special conditions
//                 0, // open
//                 0, // high
//                 0, // low
//                 0, // close
//                 0, // wap
//                 m_lastPrice, // last price
//                 m_lastShareSize // last share size
//             );
//         }
//     }

// //     // Implement this if you suspect your connection handling isn't set up correctly
// //     void IBKRTrader::managedAccounts(const std::string& accountsList) {
// //         std::cout << "[INFO] Managed accounts: " << accountsList << std::endl;
// //     }

// //     //------------------------------------------
// //     // Account and Portfolio Methods
// //     //------------------------------------------

//     void IBKRTrader::requestAccountSummary() {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request account summary: not connected" << std::endl;
//             return;
//         }
        
//         // Cancel any previous account summary request to avoid hitting API limits
//         if (m_lastAccountSummaryReqId > 9000) {
//             std::cout << "[INFO] Canceling previous account summary request with reqId: " 
//                       << m_lastAccountSummaryReqId << std::endl;
//             m_client->cancelAccountSummary(m_lastAccountSummaryReqId);
            
//             // Allow a brief pause for the cancellation to process
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         }
        
//         // Increment request ID for the new request
//         m_lastAccountSummaryReqId++;
        
//         std::cout << "[INFO] Requesting account summary with reqId: " << m_lastAccountSummaryReqId << std::endl;
        
//         // Get all available tags for account summary
//         std::string tags = "AccountType,NetLiquidation,TotalCashValue,SettledCash,AccruedCash,";
//         tags += "BuyingPower,EquityWithLoanValue,PreviousEquityWithLoanValue,GrossPositionValue,";
//         tags += "ReqTEquity,ReqTMargin,SMA,InitMarginReq,MaintMarginReq,AvailableFunds,";
//         tags += "ExcessLiquidity,Cushion,FullInitMarginReq,FullMaintMarginReq,FullAvailableFunds,";
//         tags += "FullExcessLiquidity,LookAheadNextChange,LookAheadInitMarginReq,";
//         tags += "LookAheadMaintMarginReq,LookAheadAvailableFunds,LookAheadExcessLiquidity,";
//         tags += "HighestSeverity,DayTradesRemaining,Leverage";
        
//         // Request summary for all accounts
//         m_client->reqAccountSummary(m_lastAccountSummaryReqId, "All", tags);
//     }
    
    
//     void IBKRTrader::requestTickByTickData(TickerId reqId, const std::string& symbol, 
//                                          const std::string& tickType, int numberOfTicks, 
//                                          bool ignoreSize, const Contract& contract) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request tick-by-tick data: not connected" << std::endl;
//             return;
//         }
        
//         // // Create contract object
//         // Contract contract;
//         // contract.symbol = symbol;
//         // contract.secType = "STK";
//         // contract.currency = "USD";
//         // contract.exchange = "SMART";
        
//         // IBKR API expects specific string values for tick types
//         std::string apiTickType = "AllLast"; // Default
        
//         if (tickType == "Last") {
//             apiTickType = "Last";
//         } else if (tickType == "AllLast") {
//             apiTickType = "AllLast";
//         } else if (tickType == "BidAsk") {
//             apiTickType = "BidAsk";
//         } else if (tickType == "MidPoint") {
//             apiTickType = "MidPoint";
//         }
        
//         std::cout << "[INFO] Requesting tick-by-tick data for " << symbol 
//                   << " with reqId: " << reqId 
//                   << ", type: " << apiTickType << std::endl;
                  
//         // Note: Tick-by-tick data requires market data subscription in IB
//         // For delayed data users, this will either not work or provide delayed data
//         m_client->reqTickByTickData(reqId, contract, apiTickType, numberOfTicks, ignoreSize);
//     }
    
// //     //------------------------------------------
// //     // Account and Portfolio Callbacks
// //     //------------------------------------------
    
//     void IBKRTrader::accountSummary(int reqId, const std::string& account, const std::string& tag, 
//                                   const std::string& value, const std::string& currency) {
//         // Store the account summary data in our manager
//         m_accountSummaryManager.updateAccountSummary(reqId, account, tag, value, currency);
        
//         // Log important account values (compact format)
//         if (tag == "NetLiquidation" || tag == "BuyingPower" || tag == "AvailableFunds" || tag == "ExcessLiquidity") {
//             std::cout << "[Account] " << account << " " << tag << ": " << value << " " << currency << std::endl;
//             return;
//         }
        
//         // For other tags, use more detailed logging
//         std::cout << "[AccountSummary] reqId:" << reqId 
//                   << " account:" << account
//                   << " tag:" << tag
//                   << " value:" << value
//                   << " currency:" << currency << std::endl;
//     }
    

//     ///////////////////////////////////////////////////////////////////////////
//     // MARKET DEPTH CALLBACKS
//     // Processes Level 1 market depth updates and routes to ModelManager
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::updateMktDepth(TickerId id, int position, int operation, int side, 
//                                     double price, Decimal size) {
//         // Define constants for operation types and sides
//         const int INSERT = 0;
//         const int UPDATE = 1;
//         const int DELETE = 2;
//         const int BID_SIDE = 0;
//         const int ASK_SIDE = 1;
        
//         // Convert operation to readable string for logging
//         std::string opStr;
//         switch (operation) {
//             case INSERT: opStr = "INSERT"; break;
//             case UPDATE: opStr = "UPDATE"; break;
//             case DELETE: opStr = "DELETE"; break;
//             default: opStr = "UNKNOWN_" + std::to_string(operation);
//         }
        
//         // Log in compact format
//         std::cout << "[Depth] ID:" << id 
//                   << " Pos:" << position 
//                   << " Side:" << (side == BID_SIDE ? "BID" : "ASK") 
//                   << " Op:" << opStr
//                   << " Price:" << price 
//                   << " Size:" << static_cast<double>(size) << std::endl;
        
//         // Check if this is for our ModelManager's request ID range (market depth uses 8000+ IDs)
//         // if (m_modelManager && (id >= 8000 && id < 9000)) {
//         //     // Set size to 0 for DELETE operations
//         //     double actualSize = (operation == DELETE) ? 0 : static_cast<double>(size);
            
//         //     // Decode special size values if needed
//         //     if (m_decoder->isSpecialSizeValue(actualSize)) {
//         //         actualSize = m_decoder->interpretSizeValue(actualSize, (side == BID_SIDE) ? 
//         //             static_cast<int>(TickType::BID_SIZE) : static_cast<int>(TickType::ASK_SIZE));
//         //     }
            
//         //     // Route market depth data based on side
//         //     if (side == BID_SIDE) {
//         //         // Update bid side
//         //         routeTickToModelManager(0, 0, 0, price, 0, actualSize, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     } else {
//         //         // Update ask side
//         //         routeTickToModelManager(0, 0, 0, 0, price, 0, actualSize, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     }
//         // }
//     }
    
//     ///////////////////////////////////////////////////////////////////////////
//     // MARKET DEPTH L2 CALLBACKS
//     // Processes Level 2 market depth with market maker information
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, 
//                                       int operation, int side, double price, Decimal size, 
//                                       bool isSmartDepth) {
//         // Define constants for operation types and sides
//         const int INSERT = 0;
//         const int UPDATE = 1;
//         const int DELETE = 2;
//         const int BID_SIDE = 0;
//         const int ASK_SIDE = 1;
        
//         // Convert operation to readable string for logging
//         std::string opStr;
//         switch (operation) {
//             case INSERT: opStr = "INSERT"; break;
//             case UPDATE: opStr = "UPDATE"; break;
//             case DELETE: opStr = "DELETE"; break;
//             default: opStr = "UNKNOWN_" + std::to_string(operation);
//         }
        
//         // Log in compact format with market maker info
//         std::cout << "[DepthL2] ID:" << id 
//                   << " Pos:" << position 
//                   << " MM:" << marketMaker
//                   << " Side:" << (side == BID_SIDE ? "BID" : "ASK") 
//                   << " Op:" << opStr
//                   << " Price:" << price 
//                   << " Size:" << static_cast<double>(size)
//                   << " Smart:" << (isSmartDepth ? "Y" : "N") << std::endl;
        
//         // Check if this is for our ModelManager's request ID range (market depth uses 8000+ IDs)
//         // if (m_modelManager && (id >= 8000 && id < 9000)) {
//         //     // Set size to 0 for DELETE operations
//         //     double actualSize = (operation == DELETE) ? 0 : static_cast<double>(size);
            
//         //     // Decode special size values if needed
//         //     if (m_decoder->isSpecialSizeValue(actualSize)) {
//         //         actualSize = m_decoder->interpretSizeValue(actualSize, (side == BID_SIDE) ? 
//         //             static_cast<int>(TickType::BID_SIZE) : static_cast<int>(TickType::ASK_SIZE));
//         //     }
            
//         //     // Route market depth L2 data based on side (same as L1 for now, could be enhanced)
//         //     if (side == BID_SIDE) {
//         //         // Update bid side
//         //         routeTickToModelManager(0, 0, 0, price, 0, actualSize, 0, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     } else {
//         //         // Update ask side
//         //         routeTickToModelManager(0, 0, 0, 0, price, 0, actualSize, "", "", 0, 0, 0, 0, 0, 0, 0);
//         //     }
//         }

//         void IBKRTrader::error(int id, long errorTime, int errorCode,
//                            const std::string& errorString,
//                            const std::string& advancedOrderRejectJson) {
//             std::cerr << "[ERROR] ReqId: " << id 
//                     << " Code: " << errorCode 
//                     << " Msg: " << errorString << std::endl;
                    
//             // Simplified error handling - remove delayed data fallbacks
//             if (errorCode == 162) {
//                 std::cerr << "[INFO] Historical data request error." << std::endl;
//             } else if (errorCode == 200) {
//                 std::cerr << "[INFO] No security definition found. "
//                         << "Check the contract details and symbol." << std::endl;
//             } else if (errorCode == 10092) {
//                 std::cerr << "[INFO] Deep market data is not supported for this security. "
//                         << "Using top-of-book data instead." << std::endl;
                
//                 // If it's a market depth error, try to fall back to regular market data
//                 if (id >= 8000 && id < 9000) { // Market depth request IDs are in this range
//                     // First get the symbol from the error ID context (this is simplified)
//                     std::string symbolForFallback = m_symbol;
                    
//                     if (!symbolForFallback.empty()) {
//                         std::cout << "[FALLBACK] Cannot get market depth for " << symbolForFallback 
//                                 << ". Using regular market data instead." << std::endl;
                        
//                         // Request basic market data as fallback
//                         // static int fallbackId = 7500;
//                         // requestMarketData(symbolForFallback);
//                     }
//                 }
//             } else if (errorCode == 10189) {
//                 std::cerr << "[INFO] Failed to request tick-by-tick data. "
//                         << "Verify your market data subscription." << std::endl;
                
//                 // If it's a tick-by-tick error, try to fall back to regular market data
//                 if (id >= 7000 && id < 8000) { // Tick-by-tick request IDs are in this range
//                     // Get the symbol from the context
//                     std::string symbolForFallback = m_symbol;
                    
//                     if (!symbolForFallback.empty()) {
//                         std::cout << "[FALLBACK] Cannot get tick-by-tick data for " << symbolForFallback 
//                                 << ". Using regular market data instead." << std::endl;
//                     }
//                 }
//             } else if (errorCode == 321) {
//                 std::cerr << "[INFO] Historical data request format error. "
//                         << "Check duration format (should be 'X Y' where X is integer and Y is unit)." << std::endl;
                
//                 // If it's a historical data error, log it and move on
//                 if (id >= 4000 && id < 5000) { // Historical data request IDs are in this range
//                     std::cout << "[FALLBACK] Historical data request failed. "
//                             << "Continuing with available real-time data." << std::endl;
//                 }
//             }
                    
//             // If we have a ModelManager, log symbol-specific errors
//             if (m_modelManager && id == m_requestId) {
//                 std::cerr << "[ERROR] Error for symbol " << m_symbol << ": " << errorString << std::endl;
//             }
//         }

//         //     void IBKRTrader::requestPositions() {
// //         if (!m_client || !m_client->isConnected()) {
// //             std::cerr << "[ERROR] Cannot request positions: not connected" << std::endl;
// //             return;
// //         }
        
// //         std::cout << "[INFO] Requesting all positions" << std::endl;
// //         m_client->reqPositions(); // Request all positions for all accounts
// //     }
    
// //     void IBKRTrader::requestPnL() {
// //         if (!m_client || !m_client->isConnected()) {
// //             std::cerr << "[ERROR] Cannot request PnL: not connected" << std::endl;
// //             return;
// //         }
        
// //         static int reqId = 7001;
// //         reqId++;
        
// //         // First we need an account ID, which should be received from managedAccounts callback
// //         // For demo purposes using a placeholder - in real implementation, store and use actual account ID
// //         std::string account = "DU12345"; // Replace with actual account from managedAccounts
        
// //         std::cout << "[INFO] Requesting PnL for account " << account << " with reqId: " << reqId << std::endl;
// //         m_client->reqPnL(reqId, account, "");
// //     }
    
// //     void IBKRTrader::requestAccountUpdates(const std::string& account) {
// //         if (!m_client || !m_client->isConnected()) {
// //             std::cerr << "[ERROR] Cannot request account updates: not connected" << std::endl;
// //             return;
// //         }
        
// //         std::string accountToUse = account;
// //         if (accountToUse.empty()) {
// //             // Use the first account from managedAccounts callback
// //             // In real implementation, store and use actual account ID
// //             accountToUse = "DU12345"; // Replace with actual account
// //         }
        
// //         std::cout << "[INFO] Requesting account updates for " << accountToUse << std::endl;
// //         m_client->reqAccountUpdates(true, accountToUse);
// //     }
    
// //     void IBKRTrader::requestMarketData(const std::string& symbol, const std::string& secType,
// //                                      const std::string& currency, const std::string& exchange) {
// //         if (!m_client || !m_client->isConnected()) {
// //             std::cerr << "[ERROR] Cannot request market data: not connected" << std::endl;
// //             return;
// //         }
        
// //         m_requestId++; // Use instance variable to track request IDs
        
// //         // Create contract object
// //         Contract contract;
// //         contract.symbol = symbol;
// //         contract.secType = secType;
// //         contract.currency = currency;
// //         contract.exchange = exchange;
        
// //         // Store symbol for this request ID
// //         m_symbol = symbol;
        
// //         // Change this line to request WAP explicitly
// //         std::string genericTicks = "14"; // Request WAP (field code 14)
// //         bool snapshot = false;         // Continuous updates instead of snapshot
// //         bool regulatorySnapshot = false;
        
// //         // Create empty options list (remove DELAYED tag)
// //         TagValueListSPtr mktDataOptions(new TagValueList());
        
// //         std::cout << "[INFO] Requesting market data for " << symbol 
// //                   << " with reqId: " << m_requestId << " (REALTIME)" << std::endl;
        
// //         m_client->reqMktData(m_requestId, contract, genericTicks, snapshot, 
// //                            regulatorySnapshot, mktDataOptions);
// //     }
    
// //     void IBKRTrader::cancelMarketData(TickerId tickerId) {
// //         if (!m_client || !m_client->isConnected()) {
// //             std::cerr << "[ERROR] Cannot cancel market data: not connected" << std::endl;
// //             return;
// //         }
        
// //         std::cout << "[INFO] Canceling market data for reqId: " << tickerId << std::endl;
// //         m_client->cancelMktData(tickerId);
// //     }
    
// //     void IBKRTrader::requestHistoricalData(const std::string& symbol, const std::string& duration,
// //                                          const std::string& barSize, const std::string& whatToShow) {
// //         if (!m_client || !m_client->isConnected()) {
// //             std::cerr << "[ERROR] Cannot request historical data: not connected" << std::endl;
// //             return;
// //         }
        
// //         static int reqId = 4001;
// //         reqId++;
        
// //         // Use real-time market data type
// //         m_client->reqMarketDataType(1); // 1 = REALTIME (was 3 = DELAYED)
        
// //         // Create contract object
// //         Contract contract;
// //         contract.symbol = symbol;
// //         contract.secType = "STK";
// //         contract.currency = "USD";
// //         contract.exchange = "SMART";
        
// //         // Current time formatted as YYYYMMDD HH:MM:SS
// //         auto now = std::chrono::system_clock::now();
// //         auto in_time_t = std::chrono::system_clock::to_time_t(now);
// //         std::stringstream endDateTime;
// //         struct tm timeinfo;
// // #ifdef _WIN32
// //         localtime_s(&timeinfo, &in_time_t);
// // #else
// //         localtime_r(&in_time_t, &timeinfo);
// // #endif
// //         endDateTime << std::put_time(&timeinfo, "%Y%m%d %H:%M:%S");
        
// //         bool useRTH = true; // Regular Trading Hours only
// //         int formatDate = 1;  // Format date as yyyyMMdd HH:mm:ss
        
// //         // Create an empty options list - IMPORTANT: don't use "DELAYED" tag as it's not valid
// //         TagValueListSPtr chartOptions(new TagValueList());
        
// //         // Ensure duration format is correct: integer{SPACE}unit
// //         // Fix the format if needed (e.g., "10min" -> "10 min")
// //         std::string fixedDuration = duration;
// //         // Check if there's a space between number and unit
// //         bool hasSpace = false;
// //         for (size_t i = 0; i < duration.length(); i++) {
// //             if (duration[i] == ' ') {
// //                 hasSpace = true;
// //                 break;
// //             }
// //             // If we find a non-digit, and there's no space before it, insert one
// //             if (!std::isdigit(duration[i]) && i > 0 && std::isdigit(duration[i-1])) {
// //                 fixedDuration.insert(i, " ");
// //                 hasSpace = true;
// //                 break;
// //             }
// //         }
        
// //         // If we didn't find a space and there are digits, assume format needs fixing
// //         if (!hasSpace && !duration.empty() && std::isdigit(duration[0])) {
// //             // Find position where digits end
// //             size_t pos = 0;
// //             while (pos < duration.length() && std::isdigit(duration[pos])) {
// //                 pos++;
// //             }
// //             // Insert space between number and unit
// //             if (pos < duration.length()) {
// //                 fixedDuration.insert(pos, " ");
// //             }
// //         }
        
// //         std::cout << "[INFO] Requesting historical data for " << symbol 
// //                   << " with reqId: " << reqId 
// //                   << ", duration: " << fixedDuration 
// //                   << ", barSize: " << barSize 
// //                   << ", whatToShow: " << whatToShow
// //                   << " (Using real-time data)" << std::endl;
                  
// //         m_client->reqHistoricalData(reqId, contract, endDateTime.str(), fixedDuration, 
// //                                   barSize, whatToShow, useRTH, formatDate, false, chartOptions);
// //     }

// //     void IBKRTrader::accountSummaryEnd(int reqId) {
// //         std::cout << "[AccountSummaryEnd] reqId:" << reqId << " - Account summary data complete" << std::endl;
        
// //         // If this is an old request that's finally completing, cancel it to ensure clean state
// //         if (reqId < m_lastAccountSummaryReqId && reqId > 9000) {
// //             std::cout << "[INFO] Cleaning up old account summary request: " << reqId << std::endl;
// //             m_client->cancelAccountSummary(reqId);
// //         }
// //     }
    
// //     void IBKRTrader::position(const std::string& account, const Contract& contract, 
// //                             Decimal position, double avgCost) {
// //         std::cout << "\n===== POSITION DATA =====\n";
// //         std::cout << "account: " << account << "\n";
// //         std::cout << "symbol: " << contract.symbol << "\n";
// //         std::cout << "secType: " << contract.secType << "\n";
// //         std::cout << "currency: " << contract.currency << "\n";
// //         std::cout << "exchange: " << contract.exchange << "\n";
// //         std::cout << "position: " << static_cast<double>(position) << "\n";
// //         std::cout << "avgCost: " << avgCost << "\n";
// //         std::cout << "=======================\n";
        
// //         std::cout << "[IBKRTrader::position] Account: " << account
// //                  << ", Symbol: " << contract.symbol
// //                  << ", Position: " << static_cast<double>(position)
// //                  << ", AvgCost: " << avgCost << std::endl;
// //     }
    
// //     void IBKRTrader::positionEnd() {
// //         std::cout << "[IBKRTrader::positionEnd] All positions received" << std::endl;
// //     }
    
// //     void IBKRTrader::pnl(int reqId, double dailyPnL, double unrealizedPnL, double realizedPnL) {
// //         std::cout << "\n===== PNL DATA =====\n";
// //         std::cout << "reqId: " << reqId << "\n";
// //         std::cout << "dailyPnL: " << dailyPnL << "\n";
// //         std::cout << "unrealizedPnL: " << unrealizedPnL << "\n";
// //         std::cout << "realizedPnL: " << realizedPnL << "\n";
// //         std::cout << "===================\n";
        
// //         std::cout << "[IBKRTrader::pnl] Daily PnL: " << dailyPnL
// //                  << ", Unrealized: " << unrealizedPnL
// //                  << ", Realized: " << realizedPnL << std::endl;
// //     }
    
// //     void IBKRTrader::pnlSingle(int reqId, Decimal pos, double dailyPnL, double unrealizedPnL, 
// //                              double realizedPnL, double value) {
// //         std::cout << "\n===== SINGLE POSITION PNL DATA =====\n";
// //         std::cout << "reqId: " << reqId << "\n";
// //         std::cout << "position: " << static_cast<double>(pos) << "\n";
// //         std::cout << "dailyPnL: " << dailyPnL << "\n";
// //         std::cout << "unrealizedPnL: " << unrealizedPnL << "\n";
// //         std::cout << "realizedPnL: " << realizedPnL << "\n";
// //         std::cout << "position value: " << value << "\n";
// //         std::cout << "==================================\n";
        
// //         std::cout << "[IBKRTrader::pnlSingle] Position: " << static_cast<double>(pos)
// //                  << ", Daily PnL: " << dailyPnL
// //                  << ", Unrealized: " << unrealizedPnL
// //                  << ", Realized: " << realizedPnL
// //                  << ", Value: " << value << std::endl;
// //     }
    
// //     void IBKRTrader::updateAccountValue(const std::string& key, const std::string& val, 
// //                                       const std::string& currency, const std::string& accountName) {
// //         std::cout << "\n===== ACCOUNT VALUE UPDATE =====\n";
// //         std::cout << "account: " << accountName << "\n";
// //         std::cout << "key: " << key << "\n";
// //         std::cout << "value: " << val << "\n";
// //         std::cout << "currency: " << currency << "\n";
// //         std::cout << "==============================\n";
        
// //         std::cout << "[IBKRTrader::updateAccountValue] Account: " << accountName
// //                  << ", Key: " << key
// //                  << ", Value: " << val
// //                  << ", Currency: " << currency << std::endl;
// //     }
    
// //     void IBKRTrader::updatePortfolio(const Contract& contract, Decimal position, double marketPrice,
// //                                    double marketValue, double averageCost, double unrealizedPNL,
// //                                    double realizedPNL, const std::string& accountName) {
// //         std::cout << "\n===== PORTFOLIO UPDATE =====\n";
// //         std::cout << "account: " << accountName << "\n";
// //         std::cout << "symbol: " << contract.symbol << "\n";
// //         std::cout << "secType: " << contract.secType << "\n";
// //         std::cout << "position: " << static_cast<double>(position) << "\n";
// //         std::cout << "marketPrice: " << marketPrice << "\n";
// //         std::cout << "marketValue: " << marketValue << "\n";
// //         std::cout << "averageCost: " << averageCost << "\n";
// //         std::cout << "unrealizedPNL: " << unrealizedPNL << "\n";
// //         std::cout << "realizedPNL: " << realizedPNL << "\n";
// //         std::cout << "==========================\n";
        
// //         std::cout << "[IBKRTrader::updatePortfolio] Account: " << accountName
// //                  << ", Symbol: " << contract.symbol
// //                  << ", Position: " << static_cast<double>(position)
// //                  << ", Market Value: " << marketValue
// //                  << ", Unrealized PnL: " << unrealizedPNL << std::endl;
// //     }
    
// //     void IBKRTrader::updateAccountTime(const std::string& timeStamp) {
// //         std::cout << "[IBKRTrader::updateAccountTime] Time: " << timeStamp << std::endl;
// //     }
    
// //     void IBKRTrader::accountDownloadEnd(const std::string& accountName) {
// //         std::cout << "[IBKRTrader::accountDownloadEnd] Account: " << accountName << std::endl;
// //     }

    

// //     // Cancel any active account summary requests
// //     void IBKRTrader::cancelAccountSummaryRequests() {
// //         if (!m_client || !m_client->isConnected()) {
// //             std::cerr << "[ERROR] Cannot cancel account summary requests: not connected" << std::endl;
// //             return;
// //         }
        
// //         if (m_lastAccountSummaryReqId > 9000) {
// //             std::cout << "[INFO] Canceling account summary request with reqId: " 
// //                       << m_lastAccountSummaryReqId << std::endl;
// //             m_client->cancelAccountSummary(m_lastAccountSummaryReqId);
            
// //             // Give the cancellation a moment to process
// //             std::this_thread::sleep_for(std::chrono::milliseconds(100));
// //         }
// //     }

// //     ///////////////////////////////////////////////////////////////////////////
// //     // TICK-BY-TICK BID/ASK CALLBACK
// //     // Processes detailed bid/ask updates with timestamps
// //     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// //     void IBKRTrader::tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, 
// //                                    Decimal bidSize, Decimal askSize, 
// //                                    const TickAttribBidAsk& tickAttribBidAsk) {
// //         // Log compact bid/ask update
// //         std::cout << "[BidAsk] ID:" << reqId 
// //                   << " Time:" << time
// //                   << " Bid:" << bidPrice << "x" << static_cast<double>(bidSize)
// //                   << " Ask:" << askPrice << "x" << static_cast<double>(askSize) << std::endl;
        
// //         // If we have a ModelManager, process the data
// //         if (m_modelManager && reqId == m_requestId) {
// //             // Create timestamp from the provided time_t
// //             uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
// //             // Route complete bid/ask data through our central point
// //             routeTickToModelManager(
// //                 0, // price (no last price in this update)
// //                 0, // volume
// //                 timestamp,
// //                 bidPrice,
// //                 askPrice,
// //                 static_cast<double>(bidSize),
// //                 static_cast<double>(askSize),
// //                 "", // exchange not provided in this callback
// //                 "" // no special conditions
// //             );
// //         }
// //     }
    

// } // namespace connection