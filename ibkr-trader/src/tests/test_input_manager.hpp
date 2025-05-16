#pragma once

#include "input_manager/input_manager.hpp"
#include <memory>
#include <string>
#include <functional>

namespace tests {

class TestInputManager {
public:
    // Constructor
    TestInputManager();
    
    // Run tests based on mode
    void runCliTest();
    void runApiTest(int port = 9000);
    
    // Setup and tear down
    bool setup(int logLevel = 2);
    void tearDown();
    
private:
    // The input manager instance
    std::shared_ptr<input_manager::InputManager> m_inputManager;
    
    // Callback functions
    std::function<void(const nlohmann::json&)> m_tradeCallback;
    std::function<void(const std::string&, const std::string&)> m_errorCallback;
    
    // Internal test methods
    void setupCallbacks();
    void simulateApiRequests();
    void waitForEmergencyTrigger();
    void printStatus(const std::string& label);
};

} // namespace tests 