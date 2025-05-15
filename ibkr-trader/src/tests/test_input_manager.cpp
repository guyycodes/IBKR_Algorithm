#include "test_input_manager.hpp"
#include <iostream>

namespace tests {

// Constructor
TestInputManager::TestInputManager() 
    : m_inputManager(nullptr) {
    setupCallbacks();
}

// Set up callbacks
void TestInputManager::setupCallbacks() {
    // Trade callback
    m_tradeCallback = [](const nlohmann::json& tradeData) {
        std::cout << "Trade callback triggered with data:" << std::endl;
        std::cout << tradeData.dump(2) << std::endl;
    };
    
    // Error callback
    m_errorCallback = [](const std::string& errorCode, const std::string& errorMessage) {
        std::cout << "Error callback triggered: [" << errorCode << "] " << errorMessage << std::endl;
    };
}

// Setup the test
bool TestInputManager::setup(int logLevel) {
    std::cout << "Setting up InputManager test..." << std::endl;
    
    // Create and initialize the InputManager
    m_inputManager = std::make_shared<input_manager::InputManager>();
    
    // Set logging level (0-2, with 2 being most verbose)
    m_inputManager->setLogLevel(logLevel);
    
    // Register callbacks
    m_inputManager->registerTradeCallback(m_tradeCallback);
    m_inputManager->registerErrorCallback(m_errorCallback);
    
    // Initialize the input manager
    if (!m_inputManager->initialize()) {
        std::cerr << "Failed to initialize InputManager" << std::endl;
        return false;
    }
    
    std::cout << "InputManager test setup complete" << std::endl;
    return true;
}

// Run CLI test
void TestInputManager::runCliTest() {
    std::cout << "Running CLI mode test..." << std::endl;
    
    // Start in CLI mode
    if (!m_inputManager->start(input_manager::InputSource::CLI)) {
        std::cerr << "Failed to start InputManager in CLI mode" << std::endl;
        return;
    }
    
    // Run the CLI (this is a blocking call)
    m_inputManager->runCli();
    
    // When CLI exits, clean up
    tearDown();
}

// Run API test
void TestInputManager::runApiTest(int port) {
    std::cout << "Running API mode test on port " << port << "..." << std::endl;
    
    // Start in API mode - bind to all interfaces (0.0.0.0) when in a container
    if (!m_inputManager->start(input_manager::InputSource::API, port)) {
        std::cerr << "Failed to start InputManager in API mode" << std::endl;
        return;
    }
    
    std::cout << "\n=== API SERVER RUNNING ===\n" << std::endl;
    std::cout << "The server is now running and listening for HTTP requests on port " << port << std::endl;
    std::cout << "Available endpoints:" << std::endl;
    std::cout << "  GET  /status      - Get server status" << std::endl;
    std::cout << "  GET  /trades      - Get current trades" << std::endl;
    std::cout << "  POST /trade       - Submit a trade request" << std::endl;
    std::cout << "  POST /clear       - Clear a symbol" << std::endl;
    std::cout << "  POST /clear-all   - Clear all symbols" << std::endl;
    std::cout << "  POST /emergency-stop - Trigger emergency stop" << std::endl;
    std::cout << "\nExample curl commands:" << std::endl;
    std::cout << "  curl -X GET http://localhost:" << port << "/status" << std::endl;
    std::cout << "  curl -X GET http://localhost:" << port << "/trades" << std::endl;
    std::cout << "  curl -X POST -H \"Content-Type: application/json\" -d '{\"symbol\":\"AAPL\",\"params\":{\"lots\":5,\"margin\":\".10\",\"stopLoss\":\".05\",\"maxTrades\":10,\"lossThreshold\":3,\"winThreshold\":5,\"minWinRate\":\".50\",\"maxHoldSeconds\":3600}}' http://localhost:" << port << "/trade" << std::endl;
    std::cout << "  curl -X POST -H \"Content-Type: application/json\" -d '{\"symbol\":\"AAPL\"}' http://localhost:" << port << "/clear" << std::endl;
    std::cout << "  curl -X POST http://localhost:" << port << "/clear-all" << std::endl;
    std::cout << "  curl -X POST http://localhost:" << port << "/emergency-stop" << std::endl;
    
    std::cout << "\nPress Enter to stop the server..." << std::endl;
    std::cin.get();
    
    // Clean up
    tearDown();
}

// Simulate API requests
void TestInputManager::simulateApiRequests() {
    std::cout << "Simulating API requests..." << std::endl;
    
    // Create a sample API request
    nlohmann::json sampleRequest = {
        {"symbol", "AAPL"},
        {"params", {
            {"lots", 5},
            {"margin", ".10"},
            {"stopLoss", ".05"},
            {"maxTrades", 10},
            {"lossThreshold", 3},
            {"winThreshold", 5},
            {"minWinRate", ".50"},
            {"maxHoldSeconds", 3600}
        }}
    };
    
    // Process the sample request
    if (m_inputManager->processApiRequest(sampleRequest)) {
        std::cout << "Sample request processed successfully" << std::endl;
    } else {
        std::cerr << "Failed to process sample request" << std::endl;
    }
    
    // Create and process another sample request
    nlohmann::json anotherRequest = {
        {"symbol", "MSFT"},
        {"params", {
            {"lots", 3},
            {"margin", ".08"},
            {"stopLoss", ".04"},
            {"maxTrades", 8},
            {"lossThreshold", 2},
            {"winThreshold", 6},
            {"minWinRate", ".60"},
            {"maxHoldSeconds", 1800}
        }}
    };
    
    if (m_inputManager->processApiRequest(anotherRequest)) {
        std::cout << "Second request processed successfully" << std::endl;
    } else {
        std::cerr << "Failed to process second request" << std::endl;
    }
    
    // Print the current output JSON
    printStatus("Current output JSON");
}

// Wait for emergency trigger
void TestInputManager::waitForEmergencyTrigger() {
    std::cout << "\nPress Enter to simulate an emergency stop..." << std::endl;
    std::cin.get();
    
    // Trigger emergency stop
    m_inputManager->emergencyStop();
    
    // Check if output was cleared
    printStatus("Output JSON after emergency stop");
}

// Print status
void TestInputManager::printStatus(const std::string& label) {
    std::cout << "\n" << label << ":" << std::endl;
    std::cout << m_inputManager->getOutput().dump(2) << std::endl;
}

// Tear down
void TestInputManager::tearDown() {
    if (m_inputManager) {
        std::cout << "Cleaning up InputManager test..." << std::endl;
        m_inputManager->stop();
        m_inputManager = nullptr;
    }
    std::cout << "Test complete" << std::endl;
}

} // namespace tests 