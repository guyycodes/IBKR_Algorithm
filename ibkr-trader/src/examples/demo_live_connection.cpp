// #include "example_live_connection.hpp"
// #include "../models/model_manager.hpp"
// #include "../util/app_state/app_state.hpp"
// #include <iostream>
// #include <thread>
// #include <chrono>
// #include <csignal>

// // Global flag for signal handling
// volatile std::sig_atomic_t g_running = true;

// // Signal handler for clean shutdown
// void signalHandler(int signum) {
//     std::cout << "Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
//     g_running = false;
// }

// int main() {
//     // Register signal handler
//     std::signal(SIGINT, signalHandler);
    
//     std::cout << "IBKR Trading API Integration Demo" << std::endl;
//     std::cout << "=================================" << std::endl;
//     std::cout << "This program demonstrates how to integrate IBKR's API with" << std::endl;
//     std::cout << "our ModelManager architecture for concurrent symbol processing." << std::endl;
//     std::cout << std::endl;
    
//     // Create the example connection
//     examples::ExampleLiveConnection connection;
    
//     // Initialize the connection
//     if (!connection.initialize()) {
//         std::cerr << "Failed to initialize connection. Make sure TWS or IB Gateway is running." << std::endl;
//         return 1;
//     }
    
//     std::cout << "\nConnection established. Creating model managers..." << std::endl;
    
//     // Get the ModelManagerFactory singleton
//     auto& factory = model_manager::ModelManagerFactory::getInstance();
    
//     // Create model managers for a few symbols (normally done by InputManager)
//     // Use 5-minute windows for this demo
//     auto appleManager = factory.createModelManager("AAPL", 5, model_manager::TimeWindowUnit::MINUTES);
//     auto teslaManager = factory.createModelManager("TSLA", 5, model_manager::TimeWindowUnit::MINUTES);
//     auto msftManager = factory.createModelManager("MSFT", 5, model_manager::TimeWindowUnit::MINUTES);
    
//     std::cout << "Created model managers for AAPL, TSLA, and MSFT" << std::endl;
    
//     // Manually initialize the models with some trading parameters
//     // In a real app, this would come from InputManager
//     for (const auto& symbol : {"AAPL", "TSLA", "MSFT"}) {
//         auto model = factory.getModelManager(symbol);
//         if (model) {
//             // Create a JSON structure with parameters
//             nlohmann::json params;
//             params[symbol] = {
//                 {"symbol", symbol},
//                 {"params", {
//                     {"lots", 1},
//                     {"margin", ".10"},
//                     {"stopLoss", ".05"},
//                     {"maxTrades", 10},
//                     {"lossThreshold", 3},
//                     {"winThreshold", 5}, 
//                     {"minWinRate", ".50"},
//                     {"maxHoldSeconds", 300}
//                 }}
//             };
            
//             // Initialize the model with parameters
//             model->initFromJson(params);
//             std::cout << "Initialized " << symbol << " with trading parameters" << std::endl;
//         }
//     }
    
//     // Subscribe to tick-by-tick data for all symbols
//     int appleReqId = connection.subscribeTickByTick("AAPL");
//     int teslaReqId = connection.subscribeTickByTick("TSLA");
//     int msftReqId = connection.subscribeTickByTick("MSFT");
    
//     std::cout << "\nSubscribed to tick-by-tick data for all symbols" << std::endl;
//     std::cout << "Collecting data for each symbol's ModelManager..." << std::endl;
//     std::cout << "Press Ctrl+C to exit" << std::endl;
    
//     // Main loop - run until user presses Ctrl+C
//     int counter = 0;
//     while (g_running) {
//         std::this_thread::sleep_for(std::chrono::seconds(1));
//         counter++;
        
//         // Every 10 seconds, print status
//         if (counter % 10 == 0) {
//             std::cout << "\n--- Status Update ---" << std::endl;
            
//             // Get information about all running symbols
//             auto& appState = app_state::AppState::getInstance();
//             auto runningSymbols = appState.getRunningSymbols();
            
//             std::cout << "Running threads: " << runningSymbols.size() << std::endl;
//             for (const auto& sym : runningSymbols) {
//                 auto model = factory.getModelManager(sym);
//                 if (model) {
//                     std::cout << "Symbol: " << sym << " - Tick count: " << model->getTickCount() << std::endl;
                    
//                     // Get the latest tick if available
//                     auto latestTick = model->getLatestTick();
//                     if (latestTick) {
//                         std::cout << "  Latest data: Price=" << latestTick->price 
//                                   << ", Volume=" << latestTick->volume << std::endl;
//                     }
//                 }
//             }
//         }
//     }
    
//     // Cleanup - cancel all subscriptions
//     std::cout << "\nShutting down..." << std::endl;
//     connection.cancelSubscription(appleReqId);
//     connection.cancelSubscription(teslaReqId);
//     connection.cancelSubscription(msftReqId);
    
//     // Stop the connection
//     connection.stop();
    
//     // Stop all model threads
//     app_state::AppState::getInstance().stopAllThreads();
    
//     std::cout << "Demo completed successfully!" << std::endl;
//     return 0;
// } 