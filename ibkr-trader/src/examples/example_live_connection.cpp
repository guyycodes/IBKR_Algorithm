// #include "example_live_connection.hpp"
// #include "../models/model_manager.hpp"
// #include "../util/app_state/app_state.hpp"
// #include <iomanip>
// #include <thread>
// #include <ctime>

// namespace examples {

// // Custom callback class to handle live data and route it to ModelManagers
// class LiveDataCallback : public ibkr_api_functions::API_Functions {
// public:
//     LiveDataCallback(connection::IBKRTrader& trader)
//         : API_Functions(trader) {}

//     // Override market data callbacks to route data to appropriate ModelManagers
//     void handleTickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override {
//         auto it = m_requestIdToSymbol.find(tickerId);
//         if (it != m_requestIdToSymbol.end()) {
//             std::string symbol = it->second;
//             std::string fieldName;
            
//             switch (field) {
//                 case TickType::BID: fieldName = "BID"; break;
//                 case TickType::ASK: fieldName = "ASK"; break;
//                 case TickType::LAST: fieldName = "LAST"; break;
//                 case TickType::HIGH: fieldName = "HIGH"; break;
//                 case TickType::LOW: fieldName = "LOW"; break;
//                 case TickType::CLOSE: fieldName = "CLOSE"; break;
//                 case TickType::OPEN: fieldName = "OPEN"; break;
//                 default: fieldName = "UNKNOWN_" + std::to_string(field); break;
//             }
            
//             // Print live update
//             std::time_t now = std::time(nullptr);
//             std::cout << "[" << std::put_time(std::localtime(&now), "%H:%M:%S") 
//                       << "] Market Data - Symbol: " << symbol 
//                       << ", Field: " << fieldName 
//                       << ", Price: " << price;
            
//             if (field == TickType::BID || field == TickType::ASK) {
//                 std::cout << (attrib.bidAsk ? " (Pre-open)" : "");
//             }
//             std::cout << std::endl;
            
//             // If it's a price we care about, route it to the model manager
//             if (field == TickType::LAST || field == TickType::BID || field == TickType::ASK) {
//                 routeTickToModelManager(symbol, price, 0);  // Zero volume for price-only updates
//             }
//         }
//     }

//     void handleTickSize(TickerId tickerId, TickType field, Decimal size) override {
//         auto it = m_requestIdToSymbol.find(tickerId);
//         if (it != m_requestIdToSymbol.end()) {
//             std::string symbol = it->second;
//             std::string fieldName;
            
//             switch (field) {
//                 case TickType::BID_SIZE: fieldName = "BID_SIZE"; break;
//                 case TickType::ASK_SIZE: fieldName = "ASK_SIZE"; break;
//                 case TickType::LAST_SIZE: fieldName = "LAST_SIZE"; break;
//                 case TickType::VOLUME: fieldName = "VOLUME"; break;
//                 default: fieldName = "UNKNOWN_SIZE_" + std::to_string(field); break;
//             }
            
//             double sizeValue = static_cast<double>(size);
            
//             // Print live update
//             std::time_t now = std::time(nullptr);
//             std::cout << "[" << std::put_time(std::localtime(&now), "%H:%M:%S") 
//                       << "] Market Data - Symbol: " << symbol 
//                       << ", Field: " << fieldName 
//                       << ", Size: " << sizeValue 
//                       << std::endl;
            
//             // If we have last price, we might want to add this to the model
//             // For simplicity, we're only updating volume if it's volume data
//             if (field == TickType::VOLUME) {
//                 // We don't have a price here, so we just update volume
//                 // In a real implementation, you might want to keep track of the last price
//                 double lastPrice = m_lastPrices[symbol];
//                 if (lastPrice > 0) {
//                     routeTickToModelManager(symbol, lastPrice, sizeValue);
//                 }
//             }
//         }
//     }

//     // Handle tick-by-tick data - the most granular and useful data for trading
//     void handleTickByTickAllLast(int reqId, int tickType, time_t time, double price, Decimal size,
//                             const TickAttribLast& tickAttribLast, const std::string& exchange,
//                             const std::string& specialConditions) override {
//         auto it = m_requestIdToSymbol.find(reqId);
//         if (it != m_requestIdToSymbol.end()) {
//             std::string symbol = it->second;
//             std::string tickTypeStr = tickType == 1 ? "LAST" : "ALLAST";
//             int sizeInt = static_cast<int>(size);
            
//             // Format time
//             char timeStr[20];
//             std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&time));
            
//             // Print live update
//             std::cout << "[TBT] " << timeStr 
//                       << " - Symbol: " << symbol 
//                       << ", Type: " << tickTypeStr
//                       << ", Exchange: " << exchange
//                       << ", Price: " << price
//                       << ", Size: " << sizeInt;
            
//             if (!specialConditions.empty()) {
//                 std::cout << ", Conditions: " << specialConditions;
//             }
//             std::cout << std::endl;
            
//             // Store the last price for this symbol
//             m_lastPrices[symbol] = price;
            
//             // This is highly granular tick data - ideal for the model
//             routeTickToModelManager(symbol, price, sizeInt);
//         }
//     }

//     // Error handling
//     void handleError(int id, int errorCode, const std::string& errorString) override {
//         std::cerr << "Error " << errorCode << " for request " << id << ": " << errorString << std::endl;
//     }
    
//     // Add a request ID to symbol mapping
//     void addRequestMapping(int reqId, const std::string& symbol) {
//         m_requestIdToSymbol[reqId] = symbol;
//     }
    
//     // Clear a request ID mapping
//     void clearRequestMapping(int reqId) {
//         m_requestIdToSymbol.erase(reqId);
//     }

// private:
//     // Map to track which request ID corresponds to which symbol
//     std::map<int, std::string> m_requestIdToSymbol;
    
//     // Map to track the last price for each symbol (needed for volume-only updates)
//     std::map<std::string, double> m_lastPrices;
    
//     // Route a market data tick to the appropriate ModelManager
//     void routeTickToModelManager(const std::string& symbol, double price, double volume) {
//         // Get the ModelManager for this symbol
//         auto& factory = model_manager::ModelManagerFactory::getInstance();
//         auto modelManager = factory.getModelManager(symbol);
        
//         if (modelManager) {
//             // Create a MarketDataTick object
//             raw_data_model::MarketDataTick tick;
//             tick.price = price;
//             tick.volume = volume;
//             tick.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            
//             // Add the tick to the model manager
//             // This will automatically handle pruning old data
//             modelManager->addTick(tick);
            
//             std::cout << "Routed tick for " << symbol << " to ModelManager: price=" 
//                       << price << ", volume=" << volume << std::endl;
//         } else {
//             std::cerr << "No ModelManager found for symbol: " << symbol << std::endl;
//         }
//     }
// };

// // ExampleLiveConnection implementation
// ExampleLiveConnection::ExampleLiveConnection() 
//     : m_connManager(new connection_manager::ConnectionManager())
//     , m_running(false)
//     , m_nextRequestId(1)
// {
// }

// ExampleLiveConnection::~ExampleLiveConnection() {
//     stop();
// }

// bool ExampleLiveConnection::initialize() {
//     if (!m_connManager->connect()) {
//         std::cerr << "Failed to connect to IBKR API" << std::endl;
//         return false;
//     }
    
//     std::cout << "Successfully connected to IBKR API" << std::endl;
//     m_running = true;
    
//     // Set market data type to delayed (3)
//     auto api = m_connManager->getAPI();
//     api->getClient()->reqMarketDataType(3);
//     std::cout << "Market data type set to DELAYED" << std::endl;
    
//     // Create our custom callback and set it in the connection manager
//     m_dataCallback = std::make_shared<LiveDataCallback>(m_connManager->getTrader());
    
//     // Wait a moment for connection to stabilize
//     std::this_thread::sleep_for(std::chrono::seconds(2));
    
//     return true;
// }

// void ExampleLiveConnection::stop() {
//     if (m_running) {
//         // Cancel all market data subscriptions
//         for (const auto& subscription : m_activeSubscriptions) {
//             auto api = m_connManager->getAPI();
//             int reqId = subscription.first;
//             SubscriptionType type = subscription.second;
            
//             switch (type) {
//                 case SubscriptionType::MARKET_DATA:
//                     api->cancelMarketData(reqId);
//                     break;
//                 case SubscriptionType::MARKET_DEPTH:
//                     api->cancelMarketDepth(reqId);
//                     break;
//                 case SubscriptionType::TICK_BY_TICK:
//                     api->cancelTickByTickData(reqId);
//                     break;
//                 case SubscriptionType::REAL_TIME_BARS:
//                     api->cancelRealTimeBars(reqId);
//                     break;
//             }
//         }
        
//         m_activeSubscriptions.clear();
//         m_running = false;
//         m_connManager->disconnect();
//         std::cout << "Disconnected from IBKR API" << std::endl;
//     }
// }

// Contract ExampleLiveConnection::createStockContract(const std::string& symbol) {
//     Contract contract;
//     contract.symbol = symbol;
//     contract.secType = "STK";
//     contract.currency = "USD";
//     contract.exchange = "SMART"; // SMART routing initially
//     return contract;
// }

// // Subscribe to market data for a symbol
// int ExampleLiveConnection::subscribeMarketData(const std::string& symbol) {
//     // Create contract for the given symbol
//     Contract contract = createStockContract(symbol);
    
//     // Get a new request ID
//     int reqId = m_nextRequestId++;
    
//     // Add mapping between request ID and symbol
//     m_dataCallback->addRequestMapping(reqId, symbol);
    
//     // Request market data
//     auto api = m_connManager->getAPI();
//     api->requestMarketDataNASDAQ(reqId, contract, false);
    
//     // Record the subscription
//     m_activeSubscriptions[reqId] = SubscriptionType::MARKET_DATA;
    
//     std::cout << "Subscribed to market data for " << symbol << " with request ID " << reqId << std::endl;
//     return reqId;
// }

// // Subscribe to tick-by-tick data for a symbol
// int ExampleLiveConnection::subscribeTickByTick(const std::string& symbol) {
//     // Create contract for the given symbol
//     Contract contract = createStockContract(symbol);
    
//     // Get a new request ID
//     int reqId = m_nextRequestId++;
    
//     // Add mapping between request ID and symbol
//     m_dataCallback->addRequestMapping(reqId, symbol);
    
//     // Request tick-by-tick data
//     auto api = m_connManager->getAPI();
//     api->requestTickByTickData(reqId, contract, "AllLast", 0, true);
    
//     // Record the subscription
//     m_activeSubscriptions[reqId] = SubscriptionType::TICK_BY_TICK;
    
//     std::cout << "Subscribed to tick-by-tick data for " << symbol << " with request ID " << reqId << std::endl;
//     return reqId;
// }

// // Cancel a subscription
// void ExampleLiveConnection::cancelSubscription(int reqId) {
//     auto it = m_activeSubscriptions.find(reqId);
//     if (it != m_activeSubscriptions.end()) {
//         auto api = m_connManager->getAPI();
//         SubscriptionType type = it->second;
        
//         switch (type) {
//             case SubscriptionType::MARKET_DATA:
//                 api->cancelMarketData(reqId);
//                 break;
//             case SubscriptionType::MARKET_DEPTH:
//                 api->cancelMarketDepth(reqId);
//                 break;
//             case SubscriptionType::TICK_BY_TICK:
//                 api->cancelTickByTickData(reqId);
//                 break;
//             case SubscriptionType::REAL_TIME_BARS:
//                 api->cancelRealTimeBars(reqId);
//                 break;
//         }
        
//         // Remove from our tracking maps
//         m_activeSubscriptions.erase(it);
//         m_dataCallback->clearRequestMapping(reqId);
        
//         std::cout << "Canceled subscription for request ID " << reqId << std::endl;
//     }
// }

// // Example of how this would integrate with the existing architecture
// void ExampleLiveConnection::demonstrateModelManagerIntegration() {
//     std::cout << "\n===== DEMONSTRATING INTEGRATION WITH MODEL MANAGER ARCHITECTURE =====\n" << std::endl;
    
//     // 1. Get the ModelManagerFactory
//     auto& factory = model_manager::ModelManagerFactory::getInstance();
    
//     // 2. Create some model managers (this would normally be done by InputManager)
//     auto appleManager = factory.createModelManager("AAPL", 60, model_manager::TimeWindowUnit::MINUTES);
//     auto teslaManager = factory.createModelManager("TSLA", 60, model_manager::TimeWindowUnit::MINUTES);
    
//     std::cout << "Created ModelManagers for AAPL and TSLA" << std::endl;
    
//     // 3. Subscribe to market data for these symbols
//     int appleReqId = subscribeTickByTick("AAPL");
//     int teslaReqId = subscribeTickByTick("TSLA");
    
//     // 4. Wait to collect data (in a real app this would run indefinitely)
//     std::cout << "Collecting data for 30 seconds..." << std::endl;
//     for (int i = 0; i < 30 && m_running; ++i) {
//         std::this_thread::sleep_for(std::chrono::seconds(1));
        
//         // Every 5 seconds, show how many ticks we've collected
//         if (i % 5 == 0) {
//             std::cout << "Current tick counts:" << std::endl;
//             std::cout << "AAPL: " << appleManager->getTickCount() << " ticks" << std::endl;
//             std::cout << "TSLA: " << teslaManager->getTickCount() << " ticks" << std::endl;
//         }
//     }
    
//     // 5. Cancel the subscriptions
//     cancelSubscription(appleReqId);
//     cancelSubscription(teslaReqId);
    
//     std::cout << "\n===== INTEGRATION DEMONSTRATION COMPLETED =====\n" << std::endl;
// }

// // Run a full example
// void ExampleLiveConnection::runExample() {
//     if (!initialize()) {
//         std::cerr << "Failed to initialize connection" << std::endl;
//         return;
//     }
    
//     // Demonstrate integration with the ModelManager architecture
//     demonstrateModelManagerIntegration();
    
//     // Cleanup
//     stop();
// }

// } // namespace examples 
// } // namespace examples 