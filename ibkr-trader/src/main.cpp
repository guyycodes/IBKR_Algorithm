// main.cpp

#include "tests/test_input_manager.hpp"
#include "input_manager/input_manager.hpp"
#include <iostream>
#include <string>
#include <memory>

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
    std::cout << "=================\n" << std::endl;
    
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
        auto inputManager = std::make_shared<input_manager::InputManager>();
        
        // Set logging level
        inputManager->setLogLevel(2);
        
        // Initialize the input manager
        if (!inputManager->initialize()) {
            std::cerr << "Failed to initialize InputManager" << std::endl;
            return 1;
        }
        
        // Run appropriate mode
        if (useCliMode) {
            inputManager->runCli();
        } else {
            inputManager->runApiServer(apiPort);
        }
    }
    
    return 0;
}