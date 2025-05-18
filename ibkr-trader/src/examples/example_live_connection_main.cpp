// #include "example_live_connection.hpp"
// #include <iostream>
// #include <string>
// #include <thread>
// #include <chrono>

// int main(int argc, char* argv[]) {
//     std::cout << "IBKR Live Data Connection Example" << std::endl;
//     std::cout << "=================================" << std::endl;
    
//     // Create the example connection
//     examples::ExampleLiveConnection example;
    
//     // Initialize connection to IBKR
//     if (!example.initialize()) {
//         std::cerr << "Failed to initialize connection to IBKR. Exiting." << std::endl;
//         return 1;
//     }
    
//     // Wait for connection to stabilize
//     std::cout << "Connection established. Waiting 2 seconds for connection to stabilize..." << std::endl;
//     std::this_thread::sleep_for(std::chrono::seconds(2));
    
//     // Menu of examples
//     std::cout << "\nChoose a live data example to run:" << std::endl;
//     std::cout << "1. Market Data (Level 1) - Top of book prices and sizes" << std::endl;
//     std::cout << "2. Market Depth (Level 2) - Order book" << std::endl;
//     std::cout << "3. Tick-by-Tick - Individual trades" << std::endl;
//     std::cout << "4. Real-Time Bars - 5-second OHLCV bars" << std::endl;
//     std::cout << "5. Run all examples sequentially" << std::endl;
//     std::cout << "0. Exit" << std::endl;
    
//     int choice;
//     std::cout << "\nEnter your choice: ";
//     std::cin >> choice;
    
//     try {
//         switch (choice) {
//             case 1:
//                 example.runMarketDataExample();
//                 break;
                
//             case 2:
//                 example.runMarketDepthExample();
//                 break;
                
//             case 3:
//                 example.runTickByTickExample();
//                 break;
                
//             case 4:
//                 example.runRealTimeBarExample();
//                 break;
                
//             case 5:
//                 std::cout << "\nRunning all examples sequentially:\n" << std::endl;
//                 example.runMarketDataExample();
//                 example.runMarketDepthExample();
//                 example.runTickByTickExample();
//                 example.runRealTimeBarExample();
//                 break;
                
//             case 0:
//             default:
//                 break;
//         }
//     }
//     catch (const std::exception& e) {
//         std::cerr << "Error occurred: " << e.what() << std::endl;
//     }
    
//     // Disconnect and clean up
//     example.stop();
    
//     std::cout << "\nExample complete. Exiting." << std::endl;
//     return 0;
// } 