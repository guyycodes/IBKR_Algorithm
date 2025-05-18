// #ifndef EXAMPLE_LIVE_CONNECTION_HPP
// #define EXAMPLE_LIVE_CONNECTION_HPP

// #include <iostream>
// #include <memory>
// #include <string>
// #include <map>
// #include <thread>
// #include <chrono>
// #include <atomic>
// #include <mutex>
// #include <iomanip>
// #include "../connection_manager/connection_manager.hpp"
// #include "../connection_manager/api_functions/api_functions.hpp"
// #include "../common/common_types.hpp"

// namespace examples {

// // Forward declaration of our custom callback class
// class LiveDataCallback;

// // Enum to track what type of subscription we have
// enum class SubscriptionType {
//     MARKET_DATA,
//     MARKET_DEPTH,
//     TICK_BY_TICK,
//     REAL_TIME_BARS
// };

// class ExampleLiveConnection {
// public:
//     ExampleLiveConnection();
//     ~ExampleLiveConnection();

//     // Initialize and connect to IBKR
//     bool initialize();
    
//     // Subscribe to different data streams
//     int subscribeMarketData(const std::string& symbol);
//     int subscribeTickByTick(const std::string& symbol);
//     void cancelSubscription(int reqId);
    
//     // Helper methods
//     Contract createStockContract(const std::string& symbol);
    
//     // Demonstrate integration with ModelManager
//     void demonstrateModelManagerIntegration();
    
//     // Run a full example
//     void runExample();
    
//     // Stop data collection
//     void stop();

// private:
//     // Connection objects
//     std::unique_ptr<connection_manager::ConnectionManager> m_connManager;
//     std::shared_ptr<LiveDataCallback> m_dataCallback;
    
//     // Control
//     std::atomic<bool> m_running;
//     int m_nextRequestId;
    
//     // Track active subscriptions
//     std::map<int, SubscriptionType> m_activeSubscriptions;
// };

// } // namespace examples

// #endif // EXAMPLE_LIVE_CONNECTION_HPP 