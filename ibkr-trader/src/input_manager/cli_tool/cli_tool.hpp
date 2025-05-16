#ifndef CLI_TOOL_HPP
#define CLI_TOOL_HPP

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <memory>
#include <functional>

// Forward declaration
namespace input_manager {
    class InputManager;
}

namespace cli_tool {

class CliTool {
public:
    // Constructor
    CliTool();
    
    // Set the input manager reference
    void setInputManager(std::shared_ptr<input_manager::InputManager> inputManager);
    
    // Run the CLI tool main loop
    void run();
    
    // Take input and add to JSON structure
    void takeInput();
    
    // Clear a specific input from the JSON structure
    void clearSpecificInput();
    
    // Submit all inputs
    void submitInputs();
    
    // Display running threads
    void displayRunningThreads();
    
    // Emergency exit - immediate exit
    void emergencyExit() const;
    
    // Get the current JSON inputs
    const nlohmann::json& getJsonInputs() const;
    
    // Set callback for when inputs are submitted
    void setSubmitCallback(std::function<void(const nlohmann::json&)> callback);
    
private:
    // JSON structure to hold inputs
    nlohmann::json m_jsonInputs;
    
    // Reference to the input manager
    std::weak_ptr<input_manager::InputManager> m_inputManager;
    
    // Callback for when inputs are submitted
    std::function<void(const nlohmann::json&)> m_submitCallback;
    
    // Helper function to wait for keypress
    void waitForKeypress() const;
};

} // namespace cli_tool

#endif // CLI_TOOL_HPP