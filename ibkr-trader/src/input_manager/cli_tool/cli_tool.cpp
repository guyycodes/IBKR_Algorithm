#include "cli_tool.hpp"
#include "../input_manager.hpp"
#include "../../util/app_state/app_state.hpp"

namespace cli_tool {

// Constructor
CliTool::CliTool() 
    : m_submitCallback([](const nlohmann::json&){}) {
    // Empty constructor since our JSON object is default-initialized
}

// Set the input manager reference
void CliTool::setInputManager(std::shared_ptr<input_manager::InputManager> inputManager) {
    m_inputManager = inputManager;
}

// Run the CLI tool main loop
void CliTool::run() {
    bool running = true;
    
    while (running) {
        // Use input manager for UI functions if available
        auto inputManager = m_inputManager.lock();
        if (inputManager) {
            inputManager->clearScreen();
            inputManager->printBanner();
            inputManager->printMenu();
            inputManager->printCurrentInputs();
        } else {
            // Fallback if input manager isn't available
            std::cout << "CLI Tool running (no input manager connected)" << std::endl;
        }
        
        int choice;
        std::cout << "\nEnter your choice: ";
        std::cin >> choice;
        
        // Clear any bad input
        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            choice = -1;
        }
        
        // Process choice
        switch (choice) {
            case 1: // Take input
                takeInput();
                break;
                
            case 2: // Clear specific input
                clearSpecificInput();
                break;
                
            case 3: // Submit inputs
                submitInputs();
                break;
                
            case 4: // Clear screen
                if (inputManager) {
                    inputManager->clearScreen();
                }
                break;
                
            case 5: // Display running threads
                displayRunningThreads();
                break;
                
            case 9: // Emergency exit
                emergencyExit();
                break;
                
            case 0: // Exit
                running = false;
                std::cout << "Exiting CLI Tool." << std::endl;
                break;
                
            default:
                std::cout << "Invalid choice. Please try again." << std::endl;
                waitForKeypress();
                break;
        }
    }
}

// Take input and add to JSON structure
void CliTool::takeInput() {
    // Check if we already have 10 inputs
    if (m_jsonInputs.size() >= 10) {
        std::cout << "Maximum number of inputs (10) reached. Please clear some inputs before adding more." << std::endl;
        waitForKeypress();
        return;
    }
    
    std::string symbol;
    std::cout << "Enter stock symbol (e.g., IONQ): ";
    std::cin >> symbol;
    
    // Convert to uppercase
    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
    
    // Create JSON object for parameters
    nlohmann::json params;
    
    // Get lots
    int lots;
    std::cout << "Enter lots: ";
    std::cin >> lots;
    params["lots"] = lots;
    
    // Get margin
    std::string margin;
    std::cout << "Enter margin (e.g., .10): ";
    std::cin >> margin;
    params["margin"] = margin;
    
    // Get stopLoss
    std::string stopLoss;
    std::cout << "Enter stopLoss (e.g., .05): ";
    std::cin >> stopLoss;
    params["stopLoss"] = stopLoss;
    
    // Get maxTrades
    int maxTrades;
    std::cout << "Enter maxTrades: ";
    std::cin >> maxTrades;
    params["maxTrades"] = maxTrades;
    
    // Get lossThreshold
    int lossThreshold;
    std::cout << "Enter lossThreshold: ";
    std::cin >> lossThreshold;
    params["lossThreshold"] = lossThreshold;
    
    // Get winThreshold
    int winThreshold;
    std::cout << "Enter winThreshold: ";
    std::cin >> winThreshold;
    params["winThreshold"] = winThreshold;
    
    // Get minWinRate
    std::string minWinRate;
    std::cout << "Enter minWinRate (e.g., .50): ";
    std::cin >> minWinRate;
    params["minWinRate"] = minWinRate;
    
    // Get maxHoldSeconds
    int maxHoldSeconds;
    std::cout << "Enter maxHoldSeconds: ";
    std::cin >> maxHoldSeconds;
    params["maxHoldSeconds"] = maxHoldSeconds;
    
    // Add to JSON structure
    m_jsonInputs[symbol] = params;
    std::cout << "Input added successfully!" << std::endl;
    
    waitForKeypress();
}

// Clear a specific input from the JSON structure
void CliTool::clearSpecificInput() {
    std::string symbol;
    std::cout << "Enter symbol to clear: ";
    std::cin >> symbol;
    
    // Convert to uppercase
    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
    
    bool removed = false;
    
    // Check if symbol exists in local JSON structure
    if (m_jsonInputs.contains(symbol)) {
        m_jsonInputs.erase(symbol);
        removed = true;
    }
    
    // Also try to clear from InputManager to ensure thread cleanup
    auto inputManager = m_inputManager.lock();
    if (inputManager) {
        // Call InputManager's clearSymbol to properly stop and remove thread
        if (inputManager->clearSymbol(symbol)) {
            removed = true;
        }
    }
    
    if (removed) {
        std::cout << "Symbol " << symbol << " cleared successfully!" << std::endl;
    } else {
        std::cout << "Symbol " << symbol << " not found in inputs or active threads." << std::endl;
    }
    
    waitForKeypress();
}

// Submit all inputs
void CliTool::submitInputs() {
    if (m_jsonInputs.empty()) {
        std::cout << "No inputs to submit." << std::endl;
        waitForKeypress();
        return;
    }
    
    std::cout << "Submitting inputs..." << std::endl;
    
    // Use input manager for display if available
    auto inputManager = m_inputManager.lock();
    if (inputManager) {
        inputManager->clearScreen();
        inputManager->printBanner();
        // Let input manager handle the display
    } else {
        // Fallback if input manager isn't available
        
        // Print all inputs in the requested format
        std::cout << "\nInputs:" << std::endl;
        for (auto it = m_jsonInputs.begin(); it != m_jsonInputs.end(); ++it) {
            std::cout << it.key() << ": [";
            
            // Print parameters in the map-like format
            const auto& params = it.value();
            std::cout << "lots:" << params["lots"] << ", ";
            std::cout << "margin: \"" << params["margin"] << "\", ";
            std::cout << "stopLoss: \"" << params["stopLoss"] << "\", ";
            std::cout << "maxTrades: " << params["maxTrades"] << ", ";
            std::cout << "lossThreshold: " << params["lossThreshold"] << ", ";
            std::cout << "winThreshold: " << params["winThreshold"] << ", ";
            std::cout << "minWinRate: \"" << params["minWinRate"] << "\", ";
            std::cout << "maxHoldSeconds: " << params["maxHoldSeconds"] << "]" << std::endl;
        }
        
        // Also print the raw JSON
        std::cout << "\nJSON Structure:" << std::endl;
        std::cout << m_jsonInputs.dump(2) << std::endl;
    }
    
    // Call the submit callback with the current inputs
    m_submitCallback(m_jsonInputs);
    
    // Clear inputs after submission
    m_jsonInputs.clear();
    
    std::cout << "\nInputs submitted and cleared." << std::endl;
    waitForKeypress();
}

// Emergency exit - immediate exit
void CliTool::emergencyExit() const {
    std::cout << "Emergency exit triggered. Exiting immediately." << std::endl;
    
    // Try to call emergency exit on input manager if available
    auto inputManager = m_inputManager.lock();
    if (inputManager) {
        inputManager->emergencyStop();
    } else {
        // Fallback to direct exit
        exit(0);
    }
}

// Get the current JSON inputs
const nlohmann::json& CliTool::getJsonInputs() const {
    return m_jsonInputs;
}

// Set callback for when inputs are submitted
void CliTool::setSubmitCallback(std::function<void(const nlohmann::json&)> callback) {
    if (callback) {
        m_submitCallback = callback;
    }
}

// Helper function to wait for keypress
void CliTool::waitForKeypress() const {
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
}

// Display information about currently running threads
void CliTool::displayRunningThreads() {
    std::cout << "\n===== RUNNING MODEL THREADS =====" << std::endl;
    
    // Get AppState to access thread information
    auto& appState = app_state::AppState::getInstance();
    auto runningSymbols = appState.getRunningSymbols();
    
    if (runningSymbols.empty()) {
        std::cout << "No active threads running." << std::endl;
    } else {
        std::cout << "Currently running " << runningSymbols.size() << " symbol threads:" << std::endl;
        
        // Display each running symbol
        for (const auto& symbol : runningSymbols) {
            std::cout << "  - " << symbol << std::endl;
            
            // Get the associated model parameters if available
            auto inputManager = m_inputManager.lock();
            if (inputManager) {
                auto outputJson = inputManager->getOutput();
                if (outputJson.contains(symbol) && outputJson[symbol].contains("params")) {
                    // Display the parameters
                    auto& params = outputJson[symbol]["params"];
                    std::cout << "    Parameters: [";
                    std::cout << "lots:" << params["lots"] << ", ";
                    std::cout << "margin:" << params["margin"] << ", ";
                    std::cout << "stopLoss:" << params["stopLoss"] << ", ";
                    std::cout << "maxTrades:" << params["maxTrades"];
                    std::cout << "]" << std::endl;
                }
            }
        }
    }
    
    std::cout << "=================================" << std::endl;
    waitForKeypress();
}

} // namespace cli_tool