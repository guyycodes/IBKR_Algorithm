#include "model_manager_factory.hpp"
#include "model_manager.hpp"
#include "../util/app_state/app_state.hpp"
#include <iostream>

namespace model_manager {

// Initialize static members for the singleton pattern
std::unique_ptr<ModelManagerFactory> ModelManagerFactory::s_instance = nullptr;
std::mutex ModelManagerFactory::s_instanceMutex;

// Get the singleton instance of the factory
ModelManagerFactory& ModelManagerFactory::getInstance() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance) {
        s_instance = std::unique_ptr<ModelManagerFactory>(new ModelManagerFactory());
    }
    return *s_instance;
}

// Create a new model manager for a symbol with specified time window
std::shared_ptr<ModelManager> ModelManagerFactory::createModelManager(
    const std::string& symbol, 
    size_t windowSize, 
    TimeWindowUnit windowUnit
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Create new model manager with the specified symbol and time window
    auto manager = std::make_shared<ModelManager>(symbol, windowSize, windowUnit);
    m_managers[symbol] = manager;
    
    // Register this manager with AppState to run on its own thread
    app_state::AppState::getInstance().registerModelThread(symbol, manager);
    
    return manager;
}

// Get an existing model manager for a symbol
std::shared_ptr<ModelManager> ModelManagerFactory::getModelManager(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_managers.find(symbol);
    if (it != m_managers.end()) {
        return it->second;
    }
    
    return nullptr;
}

// Initialize a model from JSON data
bool ModelManagerFactory::initModelFromJson(
    const std::string& symbol, 
    const nlohmann::json& jsonData, 
    size_t windowSize, 
    TimeWindowUnit windowUnit
) {
    // Get or create the model manager
    std::shared_ptr<ModelManager> manager;
    bool isNewManager = false;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_managers.find(symbol);
        if (it != m_managers.end()) {
            manager = it->second;
        } else {
            // Create a new manager if one doesn't exist
            manager = std::make_shared<ModelManager>(symbol, windowSize, windowUnit);
            m_managers[symbol] = manager;
            isNewManager = true;
        }
    }
    
    // Initialize the manager with JSON data
    bool success = manager->initFromJson(jsonData);
    
    // IMPORTANT: Regardless of whether the model is new or reused,
    // we need to ensure it has a thread registered with AppState.
    // If the model was previously cleared, its thread was removed
    // but the ModelManager itself remained in our map.
    auto& appState = app_state::AppState::getInstance();
    if (success && !appState.hasRunningThread(symbol)) {
        // Only register a thread if one isn't already running for this symbol
        appState.registerModelThread(symbol, manager);
    }
    
    return success;
}

// Check if a model exists for a given symbol
bool ModelManagerFactory::hasModel(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_managers.find(symbol) != m_managers.end();
}

// Remove a model for a given symbol
bool ModelManagerFactory::removeModel(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Request to stop the thread for this symbol using request-based API
    app_state::AppState::getInstance().requestThreadStop(symbol, "ModelManagerFactory::removeModel");
    
    // Get the manager before removing it to disconnect from IBKR
    auto it = m_managers.find(symbol);
    if (it != m_managers.end()) {
        // Disconnect from IBKR API
        it->second->disconnectFromIBKR();
    }
    
    // Remove the model from our map
    return m_managers.erase(symbol) > 0;
}

// Get all symbols that have model managers
std::vector<std::string> ModelManagerFactory::getAllSymbols() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> symbols;
    for (const auto& pair : m_managers) {
        symbols.push_back(pair.first);
    }
    return symbols;
}

// Clear all model managers
void ModelManagerFactory::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Disconnect all connections
    for (auto& pair : m_managers) {
        pair.second->disconnectFromIBKR();
    }
    
    // Request all threads to stop using request-based API
    app_state::AppState::getInstance().requestAllThreadsStop("ModelManagerFactory::clearAll");
    
    // Clear all models
    m_managers.clear();
}

// Backward compatibility methods

// Create or update a model manager for a symbol
std::shared_ptr<ModelManager> ModelManagerFactory::getModelManager(
    const std::string& symbol,
    bool shouldStartThread,
    const nlohmann::json& params) {
    
    // Check if we already have a manager for this symbol
    auto managerIt = m_managers.find(symbol);
    std::shared_ptr<ModelManager> manager;
    
    if (managerIt != m_managers.end()) {
        // Use existing model manager
        manager = managerIt->second;
        std::cout << "[ModelManagerFactory] Using existing ModelManager for " << symbol << std::endl;
        
        // Update configuration if params provided
        if (!params.is_null() && params.is_object()) {
            // Call appropriate method to update configuration
            if (params.contains(symbol)) {
                // Format: JSON with symbol as top-level key
                manager->initFromJson(params);
            } else {
                // Raw params object
                nlohmann::json formattedParams;
                formattedParams[symbol] = params;
                manager->initFromJson(formattedParams);
            }
            std::cout << "[ModelManagerFactory] Updated configuration for " << symbol << std::endl;
        }
    } else {
        // Create a new model manager with default settings
        size_t windowSize = 60; // Default: 60 minutes
        TimeWindowUnit windowUnit = TimeWindowUnit::MINUTES;
        
        manager = std::make_shared<ModelManager>(symbol, windowSize, windowUnit);
        
        // Set initial configuration if params provided
        if (!params.is_null() && params.is_object()) {
            if (params.contains(symbol)) {
                // Format: JSON with symbol as top-level key
                manager->initFromJson(params);
            } else {
                // Raw params object
                nlohmann::json formattedParams;
                formattedParams[symbol] = params;
                manager->initFromJson(formattedParams);
            }
        }
        
        // Store in the map
        m_managers[symbol] = manager;
        std::cout << "[ModelManagerFactory] Created new ModelManager for " << symbol << std::endl;
    }
    
    // Start a thread for this model if requested
    if (shouldStartThread) {
        auto& appState = app_state::AppState::getInstance();
        
        // Check if a thread is already running for this symbol
        if (appState.hasRunningThread(symbol)) {
            // If thread exists but state is different, stop it first
            if (appState.getThreadState(symbol) != app_state::ThreadState::RUNNING) {
                std::cout << "[ModelManagerFactory] Requesting stop for non-running thread for " 
                          << symbol << std::endl;
                appState.requestThreadStop(symbol, "ModelManagerFactory::getModelManager");
            } else {
                std::cout << "[ModelManagerFactory] Thread for symbol " << symbol 
                          << " is already running" << std::endl;
            }
        }
        
        // Start a new thread if none is running or if previous was stopped
        if (!appState.hasRunningThread(symbol) || 
            appState.getThreadState(symbol) != app_state::ThreadState::RUNNING) {
            std::cout << "[ModelManagerFactory] Registering new thread for " << symbol << std::endl;
            appState.registerModelThread(symbol, manager);
        }
    }
    
    return manager;
}

// Get a model manager without starting a thread (alias for getModelManager)
std::shared_ptr<ModelManager> ModelManagerFactory::getExistingModelManager(const std::string& symbol) {
    return getModelManager(symbol); // Alias for the original method
}

// Get all available model managers
std::map<std::string, std::shared_ptr<ModelManager>> ModelManagerFactory::getAllModelManagers() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_managers;
}

// Clear and remove a model manager (alias for removeModel)
bool ModelManagerFactory::removeModelManager(const std::string& symbol) {
    return removeModel(symbol); // Alias for the original method
}

// Clear all model managers (alias for clearAll)
void ModelManagerFactory::clearAllModelManagers() {
    clearAll(); // Alias for the original method
}

} // namespace model_manager 