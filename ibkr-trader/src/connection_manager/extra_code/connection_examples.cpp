// #include "connection.hpp"
// #include "../../models/model_manager.hpp"  // Include ModelManager for direct access
// #include <iostream>
// #include <chrono>
// #include <sstream>

// namespace connection {

//     // Define connection constants
//     const char* HOST = "host.docker.internal";
//     // Paper Trading port is 4002, Live Trading would be 7496
//     // We're using Paper Trading for testing
//     int PORT = 4002;  // Paper Trading port
//     int client_id = 0;  // Default client ID - we'll override this in connect()
    
//     // Constructor implementation
//     IBKRTrader::IBKRTrader() 
//         : m_osSignal(2000)
//         , m_client(new EClientSocket(this, &m_osSignal))
//         , m_modelManager(nullptr)
//         , m_requestId(-1)
//         , m_lastPrice(0.0)
//     {
//     }
    
//     // Destructor implementation
//     IBKRTrader::~IBKRTrader() {
//         delete m_client;
//     }
    
//     // Connect to IBKR Gateway
//     bool IBKRTrader::connect(int clientId) {
//         // Set the connection options to enable API extensions
//         m_client->setConnectOptions("+PACEAPI");
        
//         // Use provided client ID or fall back to default
//         int actualClientId = (clientId >= 0) ? clientId : client_id;
        
//         bool success = m_client->eConnect(HOST, PORT, actualClientId, /*extraAuth=*/false);
//         if (success) {
//             std::cout << "[INFO] Connection initiated to " << HOST << ":" << PORT 
//                       << " with client ID: " << actualClientId << std::endl;
            
//             // Wait a bit for connection to stabilize
//             std::this_thread::sleep_for(std::chrono::seconds(1));
            
//             // Automatically request all available data for IONQ to test enhanced data collection
//             std::cout << "[INFO] Automatically requesting all available data for IONQ to test data feeds..." << std::endl;
//             requestAllAvailableData("IONQ");
//         } else {
//             std::cerr << "[ERROR] Failed to connect to IBKR." << std::endl;
//         }
//         return success;
//     }
    
//     // Set the model manager for direct market data routing
//     void IBKRTrader::setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol) {
//         m_modelManager = modelManager;
//         m_symbol = symbol;
//         std::cout << "[IBKRTrader] Set ModelManager for symbol: " << m_symbol << std::endl;
//     }
    
//     // Disconnect from IBKR Gateway
//     void IBKRTrader::disconnect() {
//         m_client->eDisconnect();
//         std::cout << "[INFO] Disconnected from IBKR.\n";
//     }
    
//     //  createReader() is responsible for making a reader for retrieving messages from the IBKR connection, std::unique_ptr<EReader> (smart pointer that automatically manages memory
//     std::unique_ptr<EReader> IBKRTrader::createReader() {
//         std::unique_ptr<EReader> reader(new EReader(m_client, &m_osSignal));
//         reader->start();
//         return reader;
//     }
    
//     // Start message processing in a separate thread
//     std::thread IBKRTrader::startMessageProcessing(std::unique_ptr<EReader>& reader) {
//         return std::thread([this, &reader]() {
//             while (m_client->isConnected()) {
//                 m_osSignal.waitForSignal(); // waits for a notification that messages are available
//                 reader->processMsgs(); // processes any received messages
//             }
//         });
//     }
    
//     // Send a ping and return the request ID
//     int IBKRTrader::sendPing() {
//         std::lock_guard<std::mutex> lock(m_pingMutex);
//         int pingId = m_nextPingId++;
        
//         // Record the time we sent the request
//         m_pingRequests[pingId] = std::chrono::high_resolution_clock::now();
        
//         // Use reqCurrentTime as our ping mechanism
//         m_client->reqCurrentTime();
        
//         return pingId;
//     }
    
//     // Measure average latency over multiple pings
//     double IBKRTrader::measureAverageLatency(int numPings) {
//         double totalLatency = 0;
//         int successfulPings = 0;
        
//         for (int i = 0; i < numPings; i++) {
//             sendPing();
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         }
        
//         // Wait for responses
//         std::this_thread::sleep_for(std::chrono::seconds(1));
        
//         // Calculate average from completed pings (placeholder example)
//         std::lock_guard<std::mutex> lock(m_pingMutex);
//         // In a real scenario, you'd track each ping's round-trip precisely.
//         return successfulPings > 0 ? totalLatency / successfulPings : -1.0;
//     }

//     /***************************************************
//      * EWrapper overrides for connection handling
//      ***************************************************/

//     void IBKRTrader::nextValidId(OrderId orderId) {
//         std::cout << "[INFO] nextValidId: " << orderId << std::endl;
//         m_nextOrderId = orderId;
//     }

//     void IBKRTrader::error(int id, long errorTime, int errorCode,
//                            const std::string& errorString,
//                            const std::string& advancedOrderRejectJson) {
//         std::cerr << "[ERROR] ReqId: " << id 
//                   << " Code: " << errorCode 
//                   << " Msg: " << errorString << std::endl;
                  
//         // Add specific handling for common error codes related to data access
//         if (errorCode == 162) {
//             std::cerr << "[INFO] Historical data request may require market data subscription. "
//                       << "Using delayed data where possible." << std::endl;
//         } else if (errorCode == 200) {
//             std::cerr << "[INFO] No security definition found. "
//                       << "Check the contract details and symbol." << std::endl;
//         } else if (errorCode == 10167) {
//             std::cerr << "[INFO] Requested market data not subscribed. "
//                       << "Using delayed data instead." << std::endl;
//         } else if (errorCode == 10148) {
//             std::cerr << "[INFO] Data farm connection is inactive, delayed data unavailable. "
//                       << "Try again when connection is active." << std::endl;
//         }
                  
//         // If we have a ModelManager, log symbol-specific errors
//         if (m_modelManager && id == m_requestId) {
//             std::cerr << "[ERROR] Error for symbol " << m_symbol << ": " << errorString << std::endl;
//         }
//     }

//     void IBKRTrader::connectAck() {
//         std::cout << "[INFO] connectAck: Connected to IBKR" << std::endl;
//     }

//     void IBKRTrader::connectionClosed() {
//         std::cout << "[INFO] Connection closed." << std::endl;
//     }

//     // This is called when server responds to reqCurrentTime() (for pings)
//     void IBKRTrader::currentTime(long time) {
//         auto now = std::chrono::high_resolution_clock::now();
//         std::lock_guard<std::mutex> lock(m_pingMutex);
        
//         if (!m_pingRequests.empty()) {
//             auto it = m_pingRequests.begin(); // FIFO
//             auto sendTime = it->second;
//             auto latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(now - sendTime).count();
//             double latencyMs = latencyUs / 1000.0;

//             std::cout << "[Ping] latency: " << latencyMs << " ms (Server time: " << time << ")" << std::endl;
            
//             m_pingRequests.erase(it);
//         }
//     }

//     // Route tick market data to ModelManager
//     void IBKRTrader::routeTickToModelManager(double price, double volume, uint64_t timestamp) {
//         // Only process if we have a ModelManager
//         if (!m_modelManager) {
//             return;
//         }
        
//         // If timestamp is 0, use current time
//         if (timestamp == 0) {
//             timestamp = std::chrono::system_clock::now().time_since_epoch().count();
//         }
        
//         // Note large volume values but preserve them in scientific notation
//         if (volume > 1.0e10) {  // If volume is unrealistically large (>10 billion)
//             std::cout << "[IBKRTrader] Note: Extremely large volume detected: " << std::scientific 
//                       << volume << std::fixed << " (preserved as-is)" << std::endl;
//             // We don't cap the volume anymore - we preserve the original value
//             // These are real values from the IBKR API that might be unrealistic
//             // but are kept for accuracy
//         }
        
//         // Create a MarketDataTick object
//         raw_data_model::MarketDataTick tick;
//         tick.price = price;
//         tick.volume = volume;
//         tick.timestamp = timestamp;
        
//         // Get thread ID for logging
//         std::stringstream threadIdStr;
//         threadIdStr << std::this_thread::get_id();
        
//         // Log the tick with thread ID
//         std::cout << "[IBKRTrader][ThreadID: " << threadIdStr.str() << "][Symbol: " << m_symbol << "] "
//                   << "Routing tick - Price: " << price << ", Volume: " << (volume > 1.0e10 ? std::scientific : std::fixed) 
//                   << volume << std::fixed << std::endl;
        
//         // Add directly to ModelManager
//         m_modelManager->addTick(tick);
//     }

//     // Tick data callbacks now route directly to ModelManager
//     void IBKRTrader::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
//         std::cout << "\n===== TICK PRICE DATA =====\n";
//         std::cout << "tickerId: " << tickerId << "\n";
//         std::cout << "field (code): " << static_cast<int>(field);
        
//         // Map field code to readable name
//         std::string fieldName;
//         switch (field) {
//             // Regular market data fields
//             case TickType::BID: fieldName = "BID"; break;
//             case TickType::ASK: fieldName = "ASK"; break;
//             case TickType::LAST: fieldName = "LAST"; break;
//             case TickType::HIGH: fieldName = "HIGH"; break;
//             case TickType::LOW: fieldName = "LOW"; break;
//             case TickType::CLOSE: fieldName = "CLOSE"; break;
//             case TickType::OPEN: fieldName = "OPEN"; break;
            
//             // Delayed market data fields
//             case TickType::DELAYED_BID: fieldName = "DELAYED_BID"; break;
//             case TickType::DELAYED_ASK: fieldName = "DELAYED_ASK"; break;
//             case TickType::DELAYED_LAST: fieldName = "DELAYED_LAST"; break;
//             case TickType::DELAYED_HIGH: fieldName = "DELAYED_HIGH"; break;
//             case TickType::DELAYED_LOW: fieldName = "DELAYED_LOW"; break;
//             case TickType::DELAYED_CLOSE: fieldName = "DELAYED_CLOSE"; break;
//             case TickType::DELAYED_OPEN: fieldName = "DELAYED_OPEN"; break;
            
//             default: fieldName = "UNKNOWN_" + std::to_string(field); break;
//         }
        
//         std::cout << " (" << fieldName << ")\n";
//         std::cout << "price: " << price << "\n";
//         std::cout << "attrib.canAutoExecute: " << (attrib.canAutoExecute ? "true" : "false") << "\n";
//         std::cout << "attrib.pastLimit: " << (attrib.pastLimit ? "true" : "false") << "\n";
//         std::cout << "attrib.preOpen: " << (attrib.preOpen ? "true" : "false") << "\n";
//         std::cout << "=========================\n";
        
//         // Original code continues...
//         std::cout << "[IBKRTrader::tickPrice] Raw callback - ID: " << tickerId 
//                   << ", Field: " << static_cast<int>(field) 
//                   << ", Price: " << price 
//                   << ", TickAttrib: " << (attrib.canAutoExecute ? "autoexec" : "non-eligible") 
//                   << std::endl;
        
//         // Check if this is for our ModelManager's request ID
//         if (m_modelManager && tickerId == m_requestId) {
//             // Store the last price we've seen
//             m_lastPrice = price;
            
//             // If it's a price we care about (including delayed versions), route it to the model manager
//             if (field == TickType::LAST || field == TickType::BID || field == TickType::ASK ||
//                 field == TickType::DELAYED_LAST || field == TickType::DELAYED_BID || field == TickType::DELAYED_ASK) {
//                 std::cout << "[IBKRTrader] Routing " << fieldName << " price to ModelManager for " << m_symbol << std::endl;
//                 routeTickToModelManager(price, 0);  // Zero volume for price-only updates
//             }
//         }
//     }

//     void IBKRTrader::tickSize(TickerId tickerId, TickType field, Decimal size) {
//         std::cout << "\n===== TICK SIZE DATA =====\n";
//         std::cout << "tickerId: " << tickerId << "\n";
//         std::cout << "field (code): " << static_cast<int>(field);
        
//         // Map field code to readable name
//         std::string fieldName;
//         switch (field) {
//             // Regular market data size fields
//             case TickType::BID_SIZE: fieldName = "BID_SIZE"; break;
//             case TickType::ASK_SIZE: fieldName = "ASK_SIZE"; break;
//             case TickType::LAST_SIZE: fieldName = "LAST_SIZE"; break;
//             case TickType::VOLUME: fieldName = "VOLUME"; break;
            
//             // Delayed market data size fields
//             case TickType::DELAYED_BID_SIZE: fieldName = "DELAYED_BID_SIZE"; break;
//             case TickType::DELAYED_ASK_SIZE: fieldName = "DELAYED_ASK_SIZE"; break;
//             case TickType::DELAYED_LAST_SIZE: fieldName = "DELAYED_LAST_SIZE"; break;
//             case TickType::DELAYED_VOLUME: fieldName = "DELAYED_VOLUME"; break;
            
//             default: fieldName = "UNKNOWN_SIZE_" + std::to_string(field); break;
//         }
        
//         std::cout << " (" << fieldName << ")\n";
//         std::cout << "size (raw): " << size << "\n";
//         std::cout << "size (double): " << static_cast<double>(size) << "\n";
//         std::cout << "=========================\n";
        
//         // Original code continues...
//         std::cout << "[IBKRTrader::tickSize] Raw callback - ID: " << tickerId 
//                   << ", Field: " << static_cast<int>(field)
//                   << ", Size: " << static_cast<double>(size) 
//                   << std::endl;
        
//         // Check if this is for our ModelManager's request ID
//         if (m_modelManager && tickerId == m_requestId) {
//             double sizeValue = static_cast<double>(size);
            
//             // Process both regular and delayed volume ticks with the last price
//             if ((field == TickType::VOLUME || field == TickType::DELAYED_VOLUME) && m_lastPrice > 0) {
//                 std::cout << "[IBKRTrader] Routing " << fieldName << " to ModelManager for " << m_symbol << std::endl;
//                 routeTickToModelManager(m_lastPrice, sizeValue);
//             }
            
//             // Process both regular and delayed last_size ticks with the last price
//             if ((field == TickType::LAST_SIZE || field == TickType::DELAYED_LAST_SIZE) && m_lastPrice > 0) {
//                 std::cout << "[IBKRTrader] Routing " << fieldName << " to ModelManager for " << m_symbol << std::endl;
//                 routeTickToModelManager(m_lastPrice, sizeValue);
//             }
//         }
//     }

//     void IBKRTrader::tickString(TickerId tickerId, TickType field, const std::string& value) {
//         std::cout << "\n===== TICK STRING DATA =====\n";
//         std::cout << "tickerId: " << tickerId << "\n";
//         std::cout << "field (code): " << static_cast<int>(field);
        
//         // Map field code to readable name
//         std::string fieldName;
//         switch (field) {
//             // Fix to use only existing tick types
//             case TickType::LAST_TIMESTAMP: fieldName = "LAST_TIMESTAMP"; break;
//             case TickType::LAST_REG_TIME: fieldName = "LAST_REG_TIME"; break;
//             default: fieldName = "UNKNOWN_STRING_" + std::to_string(field); break;
//         }
        
//         std::cout << " (" << fieldName << ")\n";
//         std::cout << "value: " << value << "\n";
//         std::cout << "==========================\n";
        
//         // Original code
//         std::cout << "[IBKRTrader::tickString] Raw callback - ID: " << tickerId
//                   << ", Field: " << static_cast<int>(field)
//                   << ", Value: " << value << std::endl;
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
//             // Remove non-existent tick types
//             default: fieldName = "UNKNOWN_GENERIC_" + std::to_string(field); break;
//         }
        
//         std::cout << " (" << fieldName << ")\n";
//         std::cout << "value: " << value << "\n";
//         std::cout << "===========================\n";
        
//         // Original code
//         std::cout << "[IBKRTrader::tickGeneric] Raw callback - ID: " << tickerId
//                   << ", Field: " << static_cast<int>(field)
//                   << ", Value: " << value << std::endl;
//     }

//     void IBKRTrader::tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
//                                     Decimal size, const TickAttribLast& tickAttribLast, 
//                                     const std::string& exchange, const std::string& specialConditions) {
//         std::cout << "\n===== TICK BY TICK ALL LAST DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
        
//         // Map tick type to readable name
//         std::string tickTypeStr;
//         switch (tickType) {
//             case 1: tickTypeStr = "LAST"; break;
//             case 2: tickTypeStr = "ALL_LAST"; break;
//             default: tickTypeStr = "UNKNOWN_" + std::to_string(tickType); break;
//         }
        
//         std::cout << "tickType: " << tickType << " (" << tickTypeStr << ")\n";
        
//         // Format time as human-readable
//         char timeStr[30];
//         struct tm* timeinfo = localtime(&time);
//         strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
//         std::cout << "time: " << time << " (" << timeStr << ")\n";
//         std::cout << "price: " << price << "\n";
//         std::cout << "size (raw): " << size << "\n";
//         std::cout << "size (double): " << static_cast<double>(size) << "\n";
//         std::cout << "tickAttribLast.pastLimit: " << (tickAttribLast.pastLimit ? "true" : "false") << "\n";
//         std::cout << "tickAttribLast.unreported: " << (tickAttribLast.unreported ? "true" : "false") << "\n";
//         std::cout << "exchange: " << exchange << "\n";
//         std::cout << "specialConditions: " << specialConditions << "\n";
//         std::cout << "=====================================\n";
        
//         // Original code continues...
//         std::cout << "[IBKRTrader::tickByTickAllLast] Raw callback - ID: " << reqId
//                  << ", Type: " << tickType
//                  << ", Exchange: " << exchange
//                  << ", Price: " << price
//                  << ", Size: " << static_cast<double>(size) << std::endl;
                 
//         // Check if this is for our ModelManager's request ID
//         if (m_modelManager && reqId == m_requestId) {
//             // Store the last price we've seen
//             m_lastPrice = price;
            
//             // Create timestamp from the provided time_t
//             uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
//             // Tick-by-tick data is most granular and includes both price and size
//             routeTickToModelManager(price, static_cast<double>(size), timestamp);
//         }
//     }

//     // Add missing handlers for more tick types
//     void IBKRTrader::tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, 
//                                    Decimal bidSize, Decimal askSize, 
//                                    const TickAttribBidAsk& tickAttribBidAsk) {
//         std::cout << "\n===== TICK BY TICK BID/ASK DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
        
//         // Format time as human-readable
//         char timeStr[30];
//         struct tm* timeinfo = localtime(&time);
//         strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
//         std::cout << "time: " << time << " (" << timeStr << ")\n";
//         std::cout << "bidPrice: " << bidPrice << "\n";
//         std::cout << "askPrice: " << askPrice << "\n";
//         std::cout << "bidSize (raw): " << bidSize << "\n";
//         std::cout << "bidSize (double): " << static_cast<double>(bidSize) << "\n";
//         std::cout << "askSize (raw): " << askSize << "\n";
//         std::cout << "askSize (double): " << static_cast<double>(askSize) << "\n";
//         std::cout << "tickAttribBidAsk.bidPastLow: " << (tickAttribBidAsk.bidPastLow ? "true" : "false") << "\n";
//         std::cout << "tickAttribBidAsk.askPastHigh: " << (tickAttribBidAsk.askPastHigh ? "true" : "false") << "\n";
//         std::cout << "====================================\n";
        
//         std::cout << "[IBKRTrader::tickByTickBidAsk] Raw callback - ID: " << reqId
//                   << ", Time: " << timeStr
//                   << ", Bid: " << bidPrice << " x " << static_cast<double>(bidSize)
//                   << ", Ask: " << askPrice << " x " << static_cast<double>(askSize) << std::endl;
                  
//         // If we have a ModelManager, process the data
//         if (m_modelManager && reqId == m_requestId) {
//             // Create timestamp from the provided time_t
//             uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
//             // Route bid
//             m_lastPrice = bidPrice;
//             std::cout << "[IBKRTrader] Routing BID price to ModelManager for " << m_symbol << std::endl;
//             routeTickToModelManager(bidPrice, static_cast<double>(bidSize), timestamp);
            
//             // Route ask
//             m_lastPrice = askPrice;
//             std::cout << "[IBKRTrader] Routing ASK price to ModelManager for " << m_symbol << std::endl;
//             routeTickToModelManager(askPrice, static_cast<double>(askSize), timestamp);
//         }
//     }

//     void IBKRTrader::tickByTickMidPoint(int reqId, time_t time, double midPoint) {
//         std::cout << "\n===== TICK BY TICK MIDPOINT DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
        
//         // Format time as human-readable
//         char timeStr[30];
//         struct tm* timeinfo = localtime(&time);
//         strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
//         std::cout << "time: " << time << " (" << timeStr << ")\n";
//         std::cout << "midPoint: " << midPoint << "\n";
//         std::cout << "======================================\n";
        
//         std::cout << "[IBKRTrader::tickByTickMidPoint] Raw callback - ID: " << reqId
//                   << ", Time: " << timeStr
//                   << ", MidPoint: " << midPoint << std::endl;
                  
//         // If we have a ModelManager, process the data
//         if (m_modelManager && reqId == m_requestId) {
//             // Create timestamp from the provided time_t
//             uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
//             // Route mid-point as price with zero size
//             m_lastPrice = midPoint;
//             std::cout << "[IBKRTrader] Routing MIDPOINT price to ModelManager for " << m_symbol << std::endl;
//             routeTickToModelManager(midPoint, 0, timestamp);
//         }
//     }

//     void IBKRTrader::tickOptionComputation(TickerId tickerId, TickType tickType, int tickAttrib,
//                                         double impliedVol, double delta, double optPrice,
//                                         double pvDividend, double gamma, double vega,
//                                         double theta, double undPrice) {
//         std::cout << "\n===== TICK OPTION COMPUTATION DATA =====\n";
//         std::cout << "tickerId: " << tickerId << "\n";
//         std::cout << "tickType (code): " << static_cast<int>(tickType);
        
//         // Map tick type to readable name
//         std::string tickTypeStr;
//         switch (tickType) {
//             case TickType::BID_OPTION_COMPUTATION: tickTypeStr = "BID_OPTION_COMPUTATION"; break;
//             case TickType::ASK_OPTION_COMPUTATION: tickTypeStr = "ASK_OPTION_COMPUTATION"; break;
//             case TickType::LAST_OPTION_COMPUTATION: tickTypeStr = "LAST_OPTION_COMPUTATION"; break;
//             // Remove MODEL_OPTION_COMPUTATION since it doesn't exist
//             default: tickTypeStr = "UNKNOWN_OPTION_" + std::to_string(tickType); break;
//         }
        
//         std::cout << " (" << tickTypeStr << ")\n";
//         std::cout << "tickAttrib: " << tickAttrib << "\n";
//         std::cout << "impliedVol: " << impliedVol << "\n";
//         std::cout << "delta: " << delta << "\n";
//         std::cout << "optPrice: " << optPrice << "\n";
//         std::cout << "pvDividend: " << pvDividend << "\n";
//         std::cout << "gamma: " << gamma << "\n";
//         std::cout << "vega: " << vega << "\n";
//         std::cout << "theta: " << theta << "\n";
//         std::cout << "undPrice: " << undPrice << "\n";
//         std::cout << "======================================\n";
        
//         std::cout << "[IBKRTrader::tickOptionComputation] Raw callback - ID: " << tickerId
//                   << ", Type: " << static_cast<int>(tickType)
//                   << ", ImpliedVol: " << impliedVol
//                   << ", Delta: " << delta
//                   << ", OptPrice: " << optPrice
//                   << ", UndPrice: " << undPrice << std::endl;
//     }

//     void IBKRTrader::realtimeBar(TickerId reqId, long time, double open, double high, 
//                               double low, double close, Decimal volume, 
//                               Decimal wap, int count) {
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
//         if (m_modelManager && reqId == m_requestId) {
//             // Create timestamp from the provided time_t
//             uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
//             // Route close price with volume
//             m_lastPrice = close;
//             std::cout << "[IBKRTrader] Routing bar close price to ModelManager for " << m_symbol << std::endl;
//             routeTickToModelManager(close, static_cast<double>(volume), timestamp);
//         }
//     }

//     void IBKRTrader::historicalTicks(int reqId, const std::vector<HistoricalTick>& ticks, bool done) {
//         std::cout << "\n===== HISTORICAL TICKS DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "ticks count: " << ticks.size() << "\n";
        
//         for (const auto& tick : ticks) {
//             time_t epochTime = tick.time;
//             char timeStr[30];
//             struct tm* timeinfo = localtime(&epochTime);
//             strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
            
//             std::cout << "  Time: " << timeStr 
//                       << ", Price: " << tick.price
//                       << ", Size: " << static_cast<double>(tick.size) << "\n";
//         }
        
//         std::cout << "done: " << (done ? "true" : "false") << "\n";
//         std::cout << "===============================\n";
        
//         std::cout << "[IBKRTrader::historicalTicks] Raw callback - ID: " << reqId
//                   << ", Ticks count: " << ticks.size()
//                   << ", Done: " << (done ? "true" : "false") << std::endl;
        
//         // If we have a ModelManager and it is for our symbol, process the data
//         if (m_modelManager && reqId == m_requestId && !ticks.empty()) {
//             for (const auto& tick : ticks) {
//                 // Create timestamp from the provided time_t
//                 uint64_t timestamp = static_cast<uint64_t>(tick.time) * 1000; // Convert to ms
                
//                 // Route historical tick
//                 std::cout << "[IBKRTrader] Routing historical tick to ModelManager for " << m_symbol << std::endl;
//                 routeTickToModelManager(tick.price, static_cast<double>(tick.size), timestamp);
//             }
//         }
//     }

//     void IBKRTrader::historicalTicksBidAsk(int reqId, const std::vector<HistoricalTickBidAsk>& ticks, bool done) {
//         std::cout << "\n===== HISTORICAL TICKS BID/ASK DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "ticks count: " << ticks.size() << "\n";
        
//         for (const auto& tick : ticks) {
//             time_t epochTime = tick.time;
//             char timeStr[30];
//             struct tm* timeinfo = localtime(&epochTime);
//             strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
            
//             std::cout << "  Time: " << timeStr
//                       // Remove references to the non-existent 'mask' field
//                       << ", PriceBid: " << tick.priceBid 
//                       << ", PriceAsk: " << tick.priceAsk
//                       << ", SizeBid: " << static_cast<double>(tick.sizeBid)
//                       << ", SizeAsk: " << static_cast<double>(tick.sizeAsk) << "\n";
//         }
        
//         std::cout << "done: " << (done ? "true" : "false") << "\n";
//         std::cout << "======================================\n";
        
//         std::cout << "[IBKRTrader::historicalTicksBidAsk] Raw callback - ID: " << reqId
//                   << ", Ticks count: " << ticks.size()
//                   << ", Done: " << (done ? "true" : "false") << std::endl;
        
//         // If we have a ModelManager and it is for our symbol, process the data
//         if (m_modelManager && reqId == m_requestId && !ticks.empty()) {
//             for (const auto& tick : ticks) {
//                 // Create timestamp from the provided time_t
//                 uint64_t timestamp = static_cast<uint64_t>(tick.time) * 1000; // Convert to ms
                
//                 // Route bid price
//                 std::cout << "[IBKRTrader] Routing historical bid tick to ModelManager for " << m_symbol << std::endl;
//                 routeTickToModelManager(tick.priceBid, static_cast<double>(tick.sizeBid), timestamp);
                
//                 // Route ask price
//                 std::cout << "[IBKRTrader] Routing historical ask tick to ModelManager for " << m_symbol << std::endl;
//                 routeTickToModelManager(tick.priceAsk, static_cast<double>(tick.sizeAsk), timestamp);
//             }
//         }
//     }

//     void IBKRTrader::historicalTicksLast(int reqId, const std::vector<HistoricalTickLast>& ticks, bool done) {
//         std::cout << "\n===== HISTORICAL TICKS LAST DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "ticks count: " << ticks.size() << "\n";
        
//         for (const auto& tick : ticks) {
//             time_t epochTime = tick.time;
//             char timeStr[30];
//             struct tm* timeinfo = localtime(&epochTime);
//             strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
            
//             std::cout << "  Time: " << timeStr
//                       // Remove references to the non-existent 'mask' field
//                       << ", Price: " << tick.price
//                       << ", Size: " << static_cast<double>(tick.size)
//                       << ", Exchange: " << tick.exchange
//                       << ", SpecialConditions: " << tick.specialConditions << "\n";
//         }
        
//         std::cout << "done: " << (done ? "true" : "false") << "\n";
//         std::cout << "====================================\n";
        
//         std::cout << "[IBKRTrader::historicalTicksLast] Raw callback - ID: " << reqId
//                   << ", Ticks count: " << ticks.size()
//                   << ", Done: " << (done ? "true" : "false") << std::endl;
        
//         // If we have a ModelManager and it is for our symbol, process the data
//         if (m_modelManager && reqId == m_requestId && !ticks.empty()) {
//             for (const auto& tick : ticks) {
//                 // Create timestamp from the provided time_t
//                 uint64_t timestamp = static_cast<uint64_t>(tick.time) * 1000; // Convert to ms
                
//                 // Route last price
//                 std::cout << "[IBKRTrader] Routing historical last tick to ModelManager for " << m_symbol << std::endl;
//                 routeTickToModelManager(tick.price, static_cast<double>(tick.size), timestamp);
//             }
//         }
//     }

//     // Implement this if you suspect your connection handling isn't set up correctly
//     void IBKRTrader::managedAccounts(const std::string& accountsList) {
//         std::cout << "[INFO] Managed accounts: " << accountsList << std::endl;
//     }

//     // Utility method to test all available data types for any symbol
//     // This can be called manually to get all data types for a specific symbol
//     void IBKRTrader::testAllDataTypes(const std::string& symbol) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot test data types: not connected" << std::endl;
//             return;
//         }
        
//         std::cout << "\n[INFO] Testing all available data types for: " << symbol << "\n";
        
//         // Call our comprehensive data request method
//         requestAllAvailableData(symbol);
        
//         /* This method will request:
//          * 1. Account information
//          * 2. Basic market data (delayed)
//          * 3. Historical data at multiple timeframes (1min to weekly bars)
//          * 4. Historical ticks
//          * 5. Real-time bars (5-second bars, delayed)
//          * 6. Tick-by-tick data
//          * 7. Option chain data
//          * 8. Fundamental data
//          * 
//          * All using delayed data to avoid requiring a market data subscription.
//          */
//     }
    
//     // Quick utility method to request only bar data
//     // Usage examples:
//     //   requestBars("AAPL", "1 D", "1 min", "TRADES");  // 1-minute bars for 1 day
//     //   requestBars("MSFT", "1 W", "1 hour", "TRADES"); // Hourly bars for 1 week
//     //   requestBars("AMZN", "1 M", "1 day", "TRADES");  // Daily bars for 1 month
//     void IBKRTrader::requestBars(const std::string& symbol, const std::string& duration, 
//                                const std::string& barSize, const std::string& whatToShow) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "Client not connected. Cannot request bars." << std::endl;
//             return;
//         }

//         std::cout << "Requesting " << barSize << " bars for " << symbol 
//                   << " over " << duration << " (" << whatToShow << ")" << std::endl;
        
//         // Set to delayed data type
//         m_client->reqMarketDataType(3); // 3 = Delayed data
        
//         // Request historical data which contains the bars
//         requestHistoricalData(symbol, duration, barSize, whatToShow);
        
//         std::cout << "Bar data request submitted. Bars will be received via the historicalData callback." << std::endl;
//         std::cout << "Usage examples:" << std::endl;
//         std::cout << "  requestBars(\"IONQ\", \"1 D\", \"1 min\", \"TRADES\"); // 1-minute bars for today" << std::endl;
//         std::cout << "  requestBars(\"IONQ\", \"1 W\", \"1 hour\", \"TRADES\"); // Hourly bars for the week" << std::endl;
//         std::cout << "  requestBars(\"IONQ\", \"1 M\", \"1 day\", \"TRADES\"); // Daily bars for the month" << std::endl;
//     }

//     //------------------------------------------
//     // Account and Portfolio Methods
//     //------------------------------------------

//     void IBKRTrader::requestAccountSummary() {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request account summary: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 9001; // Use a unique ID for account summary requests
//         reqId++;
        
//         std::cout << "[INFO] Requesting account summary with reqId: " << reqId << std::endl;
        
//         // Get all available tags for account summary
//         std::string tags = "AccountType,NetLiquidation,TotalCashValue,SettledCash,AccruedCash,";
//         tags += "BuyingPower,EquityWithLoanValue,PreviousEquityWithLoanValue,GrossPositionValue,";
//         tags += "ReqTEquity,ReqTMargin,SMA,InitMarginReq,MaintMarginReq,AvailableFunds,";
//         tags += "ExcessLiquidity,Cushion,FullInitMarginReq,FullMaintMarginReq,FullAvailableFunds,";
//         tags += "FullExcessLiquidity,LookAheadNextChange,LookAheadInitMarginReq,";
//         tags += "LookAheadMaintMarginReq,LookAheadAvailableFunds,LookAheadExcessLiquidity,";
//         tags += "HighestSeverity,DayTradesRemaining,Leverage";
        
//         // Request summary for all accounts
//         m_client->reqAccountSummary(reqId, "All", tags);
//     }
    
//     void IBKRTrader::requestPositions() {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request positions: not connected" << std::endl;
//             return;
//         }
        
//         std::cout << "[INFO] Requesting all positions" << std::endl;
//         m_client->reqPositions(); // Request all positions for all accounts
//     }
    
//     void IBKRTrader::requestPnL() {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request PnL: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 7001;
//         reqId++;
        
//         // First we need an account ID, which should be received from managedAccounts callback
//         // For demo purposes using a placeholder - in real implementation, store and use actual account ID
//         std::string account = "DU12345"; // Replace with actual account from managedAccounts
        
//         std::cout << "[INFO] Requesting PnL for account " << account << " with reqId: " << reqId << std::endl;
//         m_client->reqPnL(reqId, account, "");
//     }
    
//     void IBKRTrader::requestAccountUpdates(const std::string& account) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request account updates: not connected" << std::endl;
//             return;
//         }
        
//         std::string accountToUse = account;
//         if (accountToUse.empty()) {
//             // Use the first account from managedAccounts callback
//             // In real implementation, store and use actual account ID
//             accountToUse = "DU12345"; // Replace with actual account
//         }
        
//         std::cout << "[INFO] Requesting account updates for " << accountToUse << std::endl;
//         m_client->reqAccountUpdates(true, accountToUse);
//     }
    
//     //------------------------------------------
//     // Contract and Market Data Methods
//     //------------------------------------------
    
//     void IBKRTrader::requestContractDetails(const std::string& symbol, const std::string& secType,
//                                           const std::string& currency, const std::string& exchange) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request contract details: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 5001;
//         reqId++;
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = secType;
//         contract.currency = currency;
//         contract.exchange = exchange;
        
//         std::cout << "[INFO] Requesting contract details for " << symbol 
//                   << " (" << secType << ") with reqId: " << reqId << std::endl;
        
//         m_client->reqContractDetails(reqId, contract);
//     }
    
//     void IBKRTrader::requestMarketData(const std::string& symbol, const std::string& secType,
//                                      const std::string& currency, const std::string& exchange) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request market data: not connected" << std::endl;
//             return;
//         }
        
//         m_requestId++; // Use instance variable to track request IDs
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = secType;
//         contract.currency = currency;
//         contract.exchange = exchange;
        
//         // Store symbol for this request ID
//         m_symbol = symbol;
        
//         std::string genericTicks = ""; // Additional generic tick types
//         bool snapshot = false;         // Continuous updates instead of snapshot
//         bool regulatorySnapshot = false;
        
//         // Request delayed data (essential for accounts without market data subscription)
//         TagValueListSPtr mktDataOptions(new TagValueList());
//         // Add tag to request delayed data explicitly
//         mktDataOptions->push_back(std::make_shared<TagValue>("DELAYED", "1"));
        
//         std::cout << "[INFO] Requesting market data for " << symbol 
//                   << " with reqId: " << m_requestId << " (DELAYED)" << std::endl;
        
//         m_client->reqMktData(m_requestId, contract, genericTicks, snapshot, 
//                            regulatorySnapshot, mktDataOptions);
//     }
    
//     void IBKRTrader::cancelMarketData(TickerId tickerId) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot cancel market data: not connected" << std::endl;
//             return;
//         }
        
//         std::cout << "[INFO] Canceling market data for reqId: " << tickerId << std::endl;
//         m_client->cancelMktData(tickerId);
//     }
    
//     void IBKRTrader::requestHistoricalData(const std::string& symbol, const std::string& duration,
//                                          const std::string& barSize, const std::string& whatToShow) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request historical data: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 4001;
//         reqId++;
        
//         // First, set the market data type to delayed (3)
//         m_client->reqMarketDataType(3); // 3 = DELAYED
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = "STK";
//         contract.currency = "USD";
//         contract.exchange = "SMART";
        
//         // Current time formatted as YYYYMMDD HH:MM:SS
//         auto now = std::chrono::system_clock::now();
//         auto in_time_t = std::chrono::system_clock::to_time_t(now);
//         std::stringstream endDateTime;
//         struct tm timeinfo;
// #ifdef _WIN32
//         localtime_s(&timeinfo, &in_time_t);
// #else
//         localtime_r(&in_time_t, &timeinfo);
// #endif
//         endDateTime << std::put_time(&timeinfo, "%Y%m%d %H:%M:%S");
        
//         bool useRTH = true; // Regular Trading Hours only
//         int formatDate = 1;  // Format date as yyyyMMdd HH:mm:ss
        
//         // Create an empty options list - IMPORTANT: don't use "DELAYED" tag as it's not valid
//         TagValueListSPtr chartOptions(new TagValueList());
//         // Don't add any tags that are not valid
        
//         std::cout << "[INFO] Requesting historical data for " << symbol 
//                   << " with reqId: " << reqId 
//                   << ", duration: " << duration 
//                   << ", barSize: " << barSize 
//                   << ", whatToShow: " << whatToShow
//                   << " (Using delayed data)" << std::endl;
                  
//         m_client->reqHistoricalData(reqId, contract, endDateTime.str(), duration, 
//                                   barSize, whatToShow, useRTH, formatDate, false, chartOptions);
//     }
    
//     void IBKRTrader::requestRealtimeBars(const std::string& symbol) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request realtime bars: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 5001;
//         reqId++;
        
//         // First, set the market data type to delayed (3)
//         m_client->reqMarketDataType(3); // 3 = DELAYED
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = "STK";
//         contract.currency = "USD";
//         contract.exchange = "SMART";
        
//         // Request 5-second bars with TRADES as data type
//         // useRTH=true means only during regular trading hours
//         std::cout << "[INFO] Requesting realtime bars for " << symbol 
//                   << " with reqId: " << reqId 
//                   << " (Using delayed data)" << std::endl;
        
//         // Create an empty options list - IMPORTANT: don't use "DELAYED" tag
//         TagValueListSPtr barOptions(new TagValueList());
        
//         // The last parameter is for options
//         m_client->reqRealTimeBars(reqId, contract, 5, "TRADES", true, barOptions);
//     }
    
//     void IBKRTrader::requestOptionChain(const std::string& symbol, const std::string& exchange) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request option chain: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 6001;
//         reqId++;
        
//         // Create contract object for the underlying
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = "OPT"; // Options
//         contract.exchange = exchange;
//         contract.currency = "USD";
        
//         std::cout << "[INFO] Requesting option chain for " << symbol 
//                   << " on " << exchange << " with reqId: " << reqId << std::endl;
                  
//         m_client->reqContractDetails(reqId, contract);
//     }
    
//     void IBKRTrader::requestTickByTickData(TickerId reqId, const std::string& symbol, 
//                                          const std::string& tickType, int numberOfTicks, 
//                                          bool ignoreSize) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request tick-by-tick data: not connected" << std::endl;
//             return;
//         }
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = "STK";
//         contract.currency = "USD";
//         contract.exchange = "SMART";
        
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
    
//     void IBKRTrader::requestNewsBulletins(bool allMessages) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request news bulletins: not connected" << std::endl;
//             return;
//         }
        
//         std::cout << "[INFO] Requesting " << (allMessages ? "all" : "new") << " news bulletins" << std::endl;
//         m_client->reqNewsBulletins(allMessages);
//     }
    
//     void IBKRTrader::requestFundamentalData(const std::string& symbol) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request fundamental data: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 8001;
//         reqId++;
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = "STK";
//         contract.currency = "USD";
//         contract.exchange = "SMART";
        
//         std::string reportType = "ReportsFinSummary"; // Financial summary report
//         TagValueListSPtr miscOptions(new TagValueList());
        
//         std::cout << "[INFO] Requesting fundamental data for " << symbol 
//                   << " with reqId: " << reqId 
//                   << ", type: " << reportType << std::endl;
                  
//         // Note: Fundamental data requires market data subscription in IB
//         m_client->reqFundamentalData(reqId, contract, reportType, miscOptions);
//     }
    
//     //------------------------------------------
//     // Account and Portfolio Callbacks
//     //------------------------------------------
    
//     void IBKRTrader::accountSummary(int reqId, const std::string& account, const std::string& tag, 
//                                   const std::string& value, const std::string& currency) {
//         std::cout << "\n===== ACCOUNT SUMMARY DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "account: " << account << "\n";
//         std::cout << "tag: " << tag << "\n";
//         std::cout << "value: " << value << "\n";
//         std::cout << "currency: " << currency << "\n";
//         std::cout << "===============================\n";
        
//         std::cout << "[IBKRTrader::accountSummary] AccountId: " << account
//                  << ", Tag: " << tag
//                  << ", Value: " << value
//                  << ", Currency: " << currency << std::endl;
//     }
    
//     void IBKRTrader::accountSummaryEnd(int reqId) {
//         std::cout << "[IBKRTrader::accountSummaryEnd] reqId: " << reqId << std::endl;
//     }
    
//     void IBKRTrader::position(const std::string& account, const Contract& contract, 
//                             Decimal position, double avgCost) {
//         std::cout << "\n===== POSITION DATA =====\n";
//         std::cout << "account: " << account << "\n";
//         std::cout << "symbol: " << contract.symbol << "\n";
//         std::cout << "secType: " << contract.secType << "\n";
//         std::cout << "currency: " << contract.currency << "\n";
//         std::cout << "exchange: " << contract.exchange << "\n";
//         std::cout << "position: " << static_cast<double>(position) << "\n";
//         std::cout << "avgCost: " << avgCost << "\n";
//         std::cout << "=======================\n";
        
//         std::cout << "[IBKRTrader::position] Account: " << account
//                  << ", Symbol: " << contract.symbol
//                  << ", Position: " << static_cast<double>(position)
//                  << ", AvgCost: " << avgCost << std::endl;
//     }
    
//     void IBKRTrader::positionEnd() {
//         std::cout << "[IBKRTrader::positionEnd] All positions received" << std::endl;
//     }
    
//     void IBKRTrader::pnl(int reqId, double dailyPnL, double unrealizedPnL, double realizedPnL) {
//         std::cout << "\n===== PNL DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "dailyPnL: " << dailyPnL << "\n";
//         std::cout << "unrealizedPnL: " << unrealizedPnL << "\n";
//         std::cout << "realizedPnL: " << realizedPnL << "\n";
//         std::cout << "===================\n";
        
//         std::cout << "[IBKRTrader::pnl] Daily PnL: " << dailyPnL
//                  << ", Unrealized: " << unrealizedPnL
//                  << ", Realized: " << realizedPnL << std::endl;
//     }
    
//     void IBKRTrader::pnlSingle(int reqId, Decimal pos, double dailyPnL, double unrealizedPnL, 
//                              double realizedPnL, double value) {
//         std::cout << "\n===== SINGLE POSITION PNL DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "position: " << static_cast<double>(pos) << "\n";
//         std::cout << "dailyPnL: " << dailyPnL << "\n";
//         std::cout << "unrealizedPnL: " << unrealizedPnL << "\n";
//         std::cout << "realizedPnL: " << realizedPnL << "\n";
//         std::cout << "position value: " << value << "\n";
//         std::cout << "==================================\n";
        
//         std::cout << "[IBKRTrader::pnlSingle] Position: " << static_cast<double>(pos)
//                  << ", Daily PnL: " << dailyPnL
//                  << ", Unrealized: " << unrealizedPnL
//                  << ", Realized: " << realizedPnL
//                  << ", Value: " << value << std::endl;
//     }
    
//     void IBKRTrader::updateAccountValue(const std::string& key, const std::string& val, 
//                                       const std::string& currency, const std::string& accountName) {
//         std::cout << "\n===== ACCOUNT VALUE UPDATE =====\n";
//         std::cout << "account: " << accountName << "\n";
//         std::cout << "key: " << key << "\n";
//         std::cout << "value: " << val << "\n";
//         std::cout << "currency: " << currency << "\n";
//         std::cout << "==============================\n";
        
//         std::cout << "[IBKRTrader::updateAccountValue] Account: " << accountName
//                  << ", Key: " << key
//                  << ", Value: " << val
//                  << ", Currency: " << currency << std::endl;
//     }
    
//     void IBKRTrader::updatePortfolio(const Contract& contract, Decimal position, double marketPrice,
//                                    double marketValue, double averageCost, double unrealizedPNL,
//                                    double realizedPNL, const std::string& accountName) {
//         std::cout << "\n===== PORTFOLIO UPDATE =====\n";
//         std::cout << "account: " << accountName << "\n";
//         std::cout << "symbol: " << contract.symbol << "\n";
//         std::cout << "secType: " << contract.secType << "\n";
//         std::cout << "position: " << static_cast<double>(position) << "\n";
//         std::cout << "marketPrice: " << marketPrice << "\n";
//         std::cout << "marketValue: " << marketValue << "\n";
//         std::cout << "averageCost: " << averageCost << "\n";
//         std::cout << "unrealizedPNL: " << unrealizedPNL << "\n";
//         std::cout << "realizedPNL: " << realizedPNL << "\n";
//         std::cout << "==========================\n";
        
//         std::cout << "[IBKRTrader::updatePortfolio] Account: " << accountName
//                  << ", Symbol: " << contract.symbol
//                  << ", Position: " << static_cast<double>(position)
//                  << ", Market Value: " << marketValue
//                  << ", Unrealized PnL: " << unrealizedPNL << std::endl;
//     }
    
//     void IBKRTrader::updateAccountTime(const std::string& timeStamp) {
//         std::cout << "[IBKRTrader::updateAccountTime] Time: " << timeStamp << std::endl;
//     }
    
//     void IBKRTrader::accountDownloadEnd(const std::string& accountName) {
//         std::cout << "[IBKRTrader::accountDownloadEnd] Account: " << accountName << std::endl;
//     }
    
//     //------------------------------------------
//     // Contract Details Callbacks
//     //------------------------------------------
    
//     void IBKRTrader::contractDetails(int reqId, const ContractDetails& contractDetails) {
//         std::cout << "\n===== CONTRACT DETAILS =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "symbol: " << contractDetails.contract.symbol << "\n";
//         std::cout << "secType: " << contractDetails.contract.secType << "\n";
//         std::cout << "exchange: " << contractDetails.contract.exchange << "\n";
//         std::cout << "currency: " << contractDetails.contract.currency << "\n";
//         std::cout << "longName: " << contractDetails.longName << "\n";
//         std::cout << "industry: " << contractDetails.industry << "\n";
//         std::cout << "category: " << contractDetails.category << "\n";
//         std::cout << "subcategory: " << contractDetails.subcategory << "\n";
//         std::cout << "timeZoneId: " << contractDetails.timeZoneId << "\n";
//         std::cout << "tradingHours: " << contractDetails.tradingHours << "\n";
//         std::cout << "liquidHours: " << contractDetails.liquidHours << "\n";
//         std::cout << "marketName: " << contractDetails.marketName << "\n";
//         std::cout << "minTick: " << contractDetails.minTick << "\n";
//         std::cout << "priceMagnifier: " << contractDetails.priceMagnifier << "\n";
//         std::cout << "orderTypes: " << contractDetails.orderTypes << "\n";
//         std::cout << "validExchanges: " << contractDetails.validExchanges << "\n";
//         std::cout << "===========================\n";
        
//         std::cout << "[IBKRTrader::contractDetails] Symbol: " << contractDetails.contract.symbol
//                  << ", Exchange: " << contractDetails.contract.exchange
//                  << ", LongName: " << contractDetails.longName << std::endl;
                 
//         // If this is an option contract, output additional option details
//         if (contractDetails.contract.secType == "OPT") {
//             std::cout << "OPTION DETAILS:\n";
//             std::cout << "  Strike: " << contractDetails.contract.strike << "\n";
//             std::cout << "  Right: " << contractDetails.contract.right << "\n";
//             std::cout << "  Expiry: " << contractDetails.contract.lastTradeDateOrContractMonth << "\n";
//             std::cout << "  Multiplier: " << contractDetails.contract.multiplier << "\n";
//         }
//     }
    
//     void IBKRTrader::contractDetailsEnd(int reqId) {
//         std::cout << "[IBKRTrader::contractDetailsEnd] reqId: " << reqId << std::endl;
//     }
    
//     //------------------------------------------
//     // Historical Data Callbacks
//     //------------------------------------------
    
//     void IBKRTrader::historicalData(TickerId reqId, const Bar& bar) {
//         std::cout << "\n======== HISTORICAL BAR DATA (" << reqId << ") ========\n";
//         std::cout << "TIME: " << bar.time << "\n";
//         std::cout << "OPEN: " << bar.open << "\n";
//         std::cout << "HIGH: " << bar.high << "\n";
//         std::cout << "LOW: " << bar.low << "\n";
//         std::cout << "CLOSE: " << bar.close << "\n";
//         std::cout << "VOLUME: " << static_cast<double>(bar.volume) << "\n";
//         std::cout << "COUNT: " << bar.count << "\n";
//         std::cout << "WAP: " << static_cast<double>(bar.wap) << "\n";
//         std::cout << "==============================================\n";
        
//         // Original code continues...
//         std::cout << "[IBKRTrader::historicalData] Time: " << bar.time
//                  << ", OHLC: " << bar.open << "/" << bar.high << "/" << bar.low << "/" << bar.close
//                  << ", Volume: " << static_cast<double>(bar.volume) << std::endl;
                 
//         // If we have a model manager, we could also route this data
//         if (m_modelManager) {
//             // Create a timestamp (convert from string if needed)
//             uint64_t timestamp = 0;
            
//             // For simplicity, just use the close price with volume
//             std::cout << "[IBKRTrader] Routing historical bar close price to ModelManager" << std::endl;
//             routeTickToModelManager(bar.close, static_cast<double>(bar.volume), timestamp);
//         }
//     }
    
//     void IBKRTrader::historicalDataEnd(int reqId, const std::string& startDateStr, const std::string& endDateStr) {
//         std::cout << "\n======== HISTORICAL DATA END (" << reqId << ") ========\n";
//         std::cout << "START DATE: " << startDateStr << "\n";
//         std::cout << "END DATE: " << endDateStr << "\n";
//         std::cout << "==============================================\n";
        
//         std::cout << "[IBKRTrader::historicalDataEnd] reqId: " << reqId 
//                  << ", Period: " << startDateStr << " - " << endDateStr << std::endl;
//     }
    
//     //------------------------------------------
//     // News and Fundamental Data Callbacks
//     //------------------------------------------
    
//     void IBKRTrader::updateNewsBulletin(int msgId, int msgType, const std::string& newsMessage, 
//                                       const std::string& originExch) {
//         std::cout << "\n===== NEWS BULLETIN =====\n";
//         std::cout << "msgId: " << msgId << "\n";
//         std::cout << "msgType: " << msgType << "\n";
//         std::cout << "exchange: " << originExch << "\n";
//         std::cout << "message: " << newsMessage << "\n";
//         std::cout << "========================\n";
        
//         std::cout << "[IBKRTrader::updateNewsBulletin] ID: " << msgId
//                  << ", Type: " << msgType
//                  << ", Exchange: " << originExch
//                  << ", Message: " << newsMessage << std::endl;
//     }
    
//     void IBKRTrader::fundamentalData(TickerId reqId, const std::string& data) {
//         std::cout << "\n===== FUNDAMENTAL DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
        
//         // Truncate the data if it's too long for console output
//         std::string truncatedData = data;
//         if (truncatedData.length() > 1000) {
//             truncatedData = truncatedData.substr(0, 997) + "...";
//         }
        
//         std::cout << "data: " << truncatedData << "\n";
//         std::cout << "===========================\n";
        
//         std::cout << "[IBKRTrader::fundamentalData] reqId: " << reqId
//                  << ", Data length: " << data.length() << " bytes" << std::endl;
//     }

//     // Use this method to request all available data types from the IBKR API
//     void IBKRTrader::requestAllAvailableData(const std::string& symbol) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request data: not connected" << std::endl;
//             return;
//         }
        
//         std::cout << "\n==================================================\n";
//         std::cout << "REQUESTING ALL AVAILABLE DATA FOR: " << symbol << "\n";
//         std::cout << "==================================================\n";
        
//         // Set to use delayed data explicitly
//         m_client->reqMarketDataType(3); // 3 = DELAYED, 4 = DELAYED_FROZEN
        
//         // 1. Account Information (these don't need symbols)
//         std::cout << "\n[1] Requesting Account Information\n";
//         requestAccountSummary();
//         requestPositions();
        
//         // 2. Market Data (these need symbols)
//         std::cout << "\n[2] Requesting Market Data for " << symbol << "\n";
        
//         // Request delayed market data
//         requestMarketData(symbol);
        
//         // Request contract details
//         requestContractDetails(symbol);
        
//         // 3. Historical Data - multiple timeframes and types
//         std::cout << "\n[3] Requesting Historical Data for " << symbol << "\n";
        
//         // 3.1 OHLC Bars - multiple timeframes
//         std::cout << "  [3.1] OHLC Bar Data\n";
//         // Intraday data
//         requestHistoricalData(symbol, "1 D", "1 min", "TRADES");    // 1-minute bars for 1 day
//         requestHistoricalData(symbol, "2 D", "5 mins", "TRADES");   // 5-minute bars for 2 days
//         requestHistoricalData(symbol, "1 W", "15 mins", "TRADES");  // 15-minute bars for 1 week
//         requestHistoricalData(symbol, "1 W", "30 mins", "TRADES");  // 30-minute bars for 1 week
//         requestHistoricalData(symbol, "1 W", "1 hour", "TRADES");   // 1-hour bars for 1 week
        
//         // Daily and weekly data
//         requestHistoricalData(symbol, "1 M", "1 day", "TRADES");    // 1-day bars for 1 month
//         requestHistoricalData(symbol, "3 M", "1 day", "TRADES");    // 1-day bars for 3 months
//         requestHistoricalData(symbol, "1 Y", "1 week", "TRADES");   // 1-week bars for 1 year
        
//         // Try different data types
//         requestHistoricalData(symbol, "1 W", "1 hour", "BID");      // Bid prices
//         requestHistoricalData(symbol, "1 W", "1 hour", "ASK");      // Ask prices
//         requestHistoricalData(symbol, "1 W", "1 hour", "MIDPOINT"); // Midpoint prices
        
//         // 3.2 Historical Ticks
//         std::cout << "  [3.2] Historical Tick Data\n";
//         // Get current time minus 1 hour
//         auto now = std::chrono::system_clock::now();
//         auto oneHourAgo = now - std::chrono::hours(1);
//         auto timePoint = std::chrono::system_clock::to_time_t(oneHourAgo);
//         std::stringstream timeStr;
//         struct tm timeinfo;
// #ifdef _WIN32
//         localtime_s(&timeinfo, &timePoint);
// #else
//         localtime_r(&timePoint, &timeinfo);
// #endif
//         timeStr << std::put_time(&timeinfo, "%Y%m%d %H:%M:%S");
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = "STK";
//         contract.currency = "USD";
//         contract.exchange = "SMART";
        
//         // Create empty options - don't use "DELAYED" tag as it's not valid for historical ticks
//         TagValueListSPtr options(new TagValueList());
        
//         static int htReqId = 6001;
//         m_client->reqHistoricalTicks(htReqId++, contract, "", timeStr.str(), 100, "TRADES", 1, true, options);
//         m_client->reqHistoricalTicks(htReqId++, contract, "", timeStr.str(), 100, "BID_ASK", 1, true, options);
//         m_client->reqHistoricalTicks(htReqId++, contract, "", timeStr.str(), 100, "MIDPOINT", 1, true, options);
        
//         // 4. Real-time Bars (5-second bars)
//         std::cout << "\n[4] Requesting Real-time Bars for " << symbol << "\n";
//         requestRealtimeBars(symbol);
        
//         // 5. Tick-by-Tick Data
//         std::cout << "\n[5] Requesting Tick-by-Tick Data for " << symbol << "\n";
        
//         static int tickRequestId = 7001;
        
//         // Request different types of tick data
//         requestTickByTickData(tickRequestId++, symbol, "AllLast", 0, false);
//         requestTickByTickData(tickRequestId++, symbol, "BidAsk", 0, false);
//         requestTickByTickData(tickRequestId++, symbol, "MidPoint", 0, false);
        
//         // 6. Option Chain (if this is a stock)
//         /*
//         if (symbol != "") {
//             std::cout << "\n[6] Requesting Option Chain for " << symbol << "\n";
//             requestOptionChain(symbol, "SMART");
//         }
//         */
        
//         // 7. Fundamental Data and News
//         std::cout << "\n[7] Requesting Fundamental Data and News\n";
//         requestFundamentalData(symbol);
//         requestNewsBulletins(true);
        
//         std::cout << "\n==================================================\n";
//         std::cout << "ALL DATA REQUESTS SUBMITTED. Check callbacks for results.\n";
//         std::cout << "==================================================\n";
//     }


    
    
   

    ///////////////////////////////////////////////////////////////////////////
    // MARKET DEPTH CALLBACK (LEVEL 1)
    // Processes market depth updates for bid/ask book and routes to ModelManager
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // void IBKRTrader::updateMktDepth(TickerId id, int position, int operation, int side, 
    //                              double price, Decimal size) {
    //     // Define constants for operation types and sides
    //     const int INSERT = 0;
    //     const int UPDATE = 1;
    //     const int DELETE = 2;
    //     const int BID_SIDE = 0;
    //     const int ASK_SIDE = 1;
        
    //     // Convert operation to readable string for logging
    //     std::string opStr;
    //     switch (operation) {
    //         case INSERT: opStr = "INSERT"; break;
    //         case UPDATE: opStr = "UPDATE"; break;
    //         case DELETE: opStr = "DELETE"; break;
    //         default: opStr = "UNKNOWN_" + std::to_string(operation);
    //     }
        
    //     // Log in compact format
    //     std::cout << "[Depth] ID:" << id 
    //               << " Pos:" << position 
    //               << " Side:" << (side == BID_SIDE ? "BID" : "ASK") 
    //               << " Op:" << opStr
    //               << " Price:" << price 
    //               << " Size:" << static_cast<double>(size) << std::endl;
        
    //     // If we have a ModelManager, process the data
    //     if (m_modelManager && id == m_requestId) {
    //         // Set size to 0 for DELETE operations
    //         double actualSize = (operation == DELETE) ? 0 : static_cast<double>(size);
            
    //         // Check if this is a special size value using the IBKRDecoder
    //         if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(actualSize)) {
    //             actualSize = ibkr_decoder::IBKRDecoder::interpretSizeValue(actualSize, (side == BID_SIDE) ? 
    //                 TickType::BID_SIZE : TickType::ASK_SIZE);
    //         }
            
    //         // Create a StockData object
    //         stock_data_tick::StockData stockData;
    //         stockData.symbol = m_symbol;
    //         stockData.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            
    //         // Update the depth data
    //         stockData.updateDepth(side == BID_SIDE, position, price, actualSize);
            
    //         // Route the data to the model manager
    //         m_modelManager->addTick(stockData);
    //     }
    // }
    
    ///////////////////////////////////////////////////////////////////////////
    // MARKET DEPTH CALLBACK (LEVEL 2)
    // Processes detailed market depth with market maker information
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // void IBKRTrader::updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, 
    //                                int operation, int side, double price, Decimal size, 
    //                                bool isSmartDepth) {
    //     // Define constants for operation types and sides
    //     const int INSERT = 0;
    //     const int UPDATE = 1;
    //     const int DELETE = 2;
    //     const int BID_SIDE = 0;
    //     const int ASK_SIDE = 1;
        
    //     // Convert operation to readable string for logging
    //     std::string opStr;
    //     switch (operation) {
    //         case INSERT: opStr = "INSERT"; break;
    //         case UPDATE: opStr = "UPDATE"; break;
    //         case DELETE: opStr = "DELETE"; break;
    //         default: opStr = "UNKNOWN_" + std::to_string(operation);
    //     }
        
    //     // Log in compact format
    //     std::cout << "[DepthL2] ID:" << id 
    //               << " Pos:" << position 
    //               << " Side:" << (side == BID_SIDE ? "BID" : "ASK") 
    //               << " Op:" << opStr
    //               << " MM:" << marketMaker
    //               << " Price:" << price 
    //               << " Size:" << static_cast<double>(size)
    //               << " Smart:" << (isSmartDepth ? "Y" : "N") << std::endl;
        
    //     // If we have a ModelManager, process the data
    //     if (m_modelManager && id == m_requestId) {
    //         // Set size to 0 for DELETE operations
    //         double actualSize = (operation == DELETE) ? 0 : static_cast<double>(size);
            
    //         // Check if this is a special size value using the IBKRDecoder
    //         if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(actualSize)) {
    //             actualSize = ibkr_decoder::IBKRDecoder::interpretSizeValue(actualSize, (side == BID_SIDE) ? 
    //                 TickType::BID_SIZE : TickType::ASK_SIZE);
    //         }
            
    //         // Create a StockData object
    //         stock_data_tick::StockData stockData;
    //         stockData.symbol = m_symbol;
    //         stockData.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            
    //         // Update the depth data with market maker info
    //         stockData.updateDepthL2(side == BID_SIDE, position, price, actualSize, marketMaker, isSmartDepth);
            
    //         // Route the data to the model manager
    //         m_modelManager->addTick(stockData);
    //     }
    // }


    
    
   

    ///////////////////////////////////////////////////////////////////////////
    // MARKET DEPTH CALLBACK (LEVEL 1)
    // Processes market depth updates for bid/ask book and routes to ModelManager
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // void IBKRTrader::updateMktDepth(TickerId id, int position, int operation, int side, 
    //                              double price, Decimal size) {
    //     // Define constants for operation types and sides
    //     const int INSERT = 0;
    //     const int UPDATE = 1;
    //     const int DELETE = 2;
    //     const int BID_SIDE = 0;
    //     const int ASK_SIDE = 1;
        
    //     // Convert operation to readable string for logging
    //     std::string opStr;
    //     switch (operation) {
    //         case INSERT: opStr = "INSERT"; break;
    //         case UPDATE: opStr = "UPDATE"; break;
    //         case DELETE: opStr = "DELETE"; break;
    //         default: opStr = "UNKNOWN_" + std::to_string(operation);
    //     }
        
    //     // Log in compact format
    //     std::cout << "[Depth] ID:" << id 
    //               << " Pos:" << position 
    //               << " Side:" << (side == BID_SIDE ? "BID" : "ASK") 
    //               << " Op:" << opStr
    //               << " Price:" << price 
    //               << " Size:" << static_cast<double>(size) << std::endl;
        
    //     // If we have a ModelManager, process the data
    //     if (m_modelManager && id == m_requestId) {
    //         // Set size to 0 for DELETE operations
    //         double actualSize = (operation == DELETE) ? 0 : static_cast<double>(size);
            
    //         // Check if this is a special size value using the IBKRDecoder
    //         if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(actualSize)) {
    //             actualSize = ibkr_decoder::IBKRDecoder::interpretSizeValue(actualSize, (side == BID_SIDE) ? 
    //                 TickType::BID_SIZE : TickType::ASK_SIZE);
    //         }
            
    //         // Create a StockData object
    //         stock_data_tick::StockData stockData;
    //         stockData.symbol = m_symbol;
    //         stockData.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            
    //         // Update the depth data
    //         stockData.updateDepth(side == BID_SIDE, position, price, actualSize);
            
    //         // Route the data to the model manager
    //         m_modelManager->addTick(stockData);
    //     }
    // }
    
    ///////////////////////////////////////////////////////////////////////////
    // MARKET DEPTH CALLBACK (LEVEL 2)
    // Processes detailed market depth with market maker information
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // void IBKRTrader::updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, 
    //                                int operation, int side, double price, Decimal size, 
    //                                bool isSmartDepth) {
    //     // Define constants for operation types and sides
    //     const int INSERT = 0;
    //     const int UPDATE = 1;
    //     const int DELETE = 2;
    //     const int BID_SIDE = 0;
    //     const int ASK_SIDE = 1;
        
    //     // Convert operation to readable string for logging
    //     std::string opStr;
    //     switch (operation) {
    //         case INSERT: opStr = "INSERT"; break;
    //         case UPDATE: opStr = "UPDATE"; break;
    //         case DELETE: opStr = "DELETE"; break;
    //         default: opStr = "UNKNOWN_" + std::to_string(operation);
    //     }
        
    //     // Log in compact format
    //     std::cout << "[DepthL2] ID:" << id 
    //               << " Pos:" << position 
    //               << " Side:" << (side == BID_SIDE ? "BID" : "ASK") 
    //               << " Op:" << opStr
    //               << " MM:" << marketMaker
    //               << " Price:" << price 
    //               << " Size:" << static_cast<double>(size)
    //               << " Smart:" << (isSmartDepth ? "Y" : "N") << std::endl;
        
    //     // If we have a ModelManager, process the data
    //     if (m_modelManager && id == m_requestId) {
    //         // Set size to 0 for DELETE operations
    //         double actualSize = (operation == DELETE) ? 0 : static_cast<double>(size);
            
    //         // Check if this is a special size value using the IBKRDecoder
    //         if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(actualSize)) {
    //             actualSize = ibkr_decoder::IBKRDecoder::interpretSizeValue(actualSize, (side == BID_SIDE) ? 
    //                 TickType::BID_SIZE : TickType::ASK_SIZE);
    //         }
            
    //         // Create a StockData object
    //         stock_data_tick::StockData stockData;
    //         stockData.symbol = m_symbol;
    //         stockData.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            
    //         // Update the depth data with market maker info
    //         stockData.updateDepthL2(side == BID_SIDE, position, price, actualSize, marketMaker, isSmartDepth);
            
    //         // Route the data to the model manager
    //         m_modelManager->addTick(stockData);
    //     }
    // }

// } // namespace connection