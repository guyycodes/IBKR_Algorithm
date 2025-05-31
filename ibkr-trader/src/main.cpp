// main.cpp

#include "tests/test_input_manager.hpp"
#include "input_manager/input_manager.hpp"
#include "util/app_state/app_state.hpp"
#include <iostream>
#include <string>
#include <memory>
#include <signal.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <unistd.h> // For _exit()

// Global reference to input manager for signal handling
std::shared_ptr<input_manager::InputManager> g_inputManager;
std::atomic<bool> g_shutdownRequested(false);

// Signal handler for CTRL+C
void signalHandler(int signum) {
    std::cout << "\n[main] Received signal " << signum << " (CTRL+C)" << std::endl;
    std::cout << "[main] Initiating graceful shutdown sequence..." << std::endl;
    
    // Use AppState requestEmergencyStop to handle thread shutdown
    auto& appState = app_state::AppState::getInstance();
    appState.requestEmergencyStop(5000, "signalHandler");
    
    // Set up a watchdog timer to force exit if shutdown takes too long
    std::thread watchdogThread([]() {
        std::cout << "[main] Started watchdog timer (5 seconds) for shutdown" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Check if shutdown is still in progress
        auto& appState = app_state::AppState::getInstance();
        if (appState.isShutdownInProgress()) {
            // Shutdown is still in progress, force exit
            std::cout << "[main] Watchdog timer expired, forcing exit" << std::endl;
            // Use _exit instead of exit to ensure immediate termination without cleanup
            _exit(1);
        } else {
            std::cout << "[main] Shutdown completed gracefully before watchdog timeout" << std::endl;
            // Force exit even if shutdown completed gracefully
            _exit(0);
        }
    });
    
    // Detach the watchdog thread so it runs independently
    watchdogThread.detach();
    
    // Log that graceful shutdown is in progress
    std::cout << "[main] Graceful shutdown in progress (watchdog will force exit in 5s if needed)" << std::endl;
    
    // Set global shutdown flag to signal other components
    g_shutdownRequested.store(true);
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --test-cli       Run in test CLI mode (using TestInputManager)" << std::endl;
    std::cout << "  --test-api       Run in test API mode (using TestInputManager)" << std::endl;
    std::cout << "  --api            Run in direct API mode (using InputManager)" << std::endl;
    std::cout << "  --cli            Run in direct CLI mode (using InputManager)" << std::endl;
    std::cout << "  --port PORT      Set API port (default: 9000)" << std::endl;
    std::cout << "  --help           Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "IBKR Trading System" << std::endl;
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    std::cout << "[Main] [Main_Thread: " << threadIdStr.str() << "] " << std::endl;
    std::cout << "=================\n" << std::endl;
    
    // Register signal handler for CTRL+C
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Parse command line arguments
    bool useCliMode = true;         // Default to CLI mode
    bool useTestMode = true;        // Default to test mode
    int apiPort = 9000;             // Default API port - container friendly
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test-api") {
            useCliMode = false;
            useTestMode = true;
        } else if (arg == "--test-cli") {
            useCliMode = true;
            useTestMode = true;
        } else if (arg == "--api") {
            useCliMode = false;
            useTestMode = false;
        } else if (arg == "--cli") {
            useCliMode = true;
            useTestMode = false;
        } else if (arg == "--port" && i + 1 < argc) {
            apiPort = std::stoi(argv[i + 1]);
            ++i;
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    if (useTestMode) {
        // Use TestInputManager for testing
        tests::TestInputManager tester;
        
        // Set up the test
        if (!tester.setup()) {
            std::cerr << "Failed to set up test" << std::endl;
            return 1;
        }
        
        // Run appropriate test based on mode
        if (useCliMode) {
            tester.runCliTest();
        } else {
            tester.runApiTest(apiPort);
        }
    } else {
        // Use InputManager directly
        g_inputManager = std::make_shared<input_manager::InputManager>();
        
        // Set logging level
        g_inputManager->setLogLevel(2);
        
        // Initialize the input manager
        if (!g_inputManager->initialize()) {
            std::cerr << "Failed to initialize InputManager" << std::endl;
            return 1;
        }
        
        // Run appropriate mode
        if (useCliMode) {
            g_inputManager->runCli();
        } else {
            g_inputManager->runApiServer(apiPort);
        }
    }
    
    // Ensure any remaining cleanup happens
    if (g_shutdownRequested.load()) {
        std::cout << "Cleanup complete. Exiting..." << std::endl;
    }
    
    return 0;
}