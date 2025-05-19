#ifndef MODEL_MANAGER_FACTORY_HPP
#define MODEL_MANAGER_FACTORY_HPP

#include <memory>
#include <map>
#include <string>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>
/*
┌─────────────────────────────────────────────────────────────────────┐
│ ModelManagerFactory (Singleton)                                      │
│                                                                     │
│ ┌─────────────────────────────────────────────────────────────────┐ │
│ │ std::map<std::string, std::shared_ptr<ModelManager>> m_managers │ │
│ │                                                                 │ │
│ │ "AAPL" ────────┐    "MSFT" ────────┐    "GOOG" ────────┐       │ │
│ │                ▼                    ▼                    ▼       │ │
│ │             ┌─────┐              ┌─────┐              ┌─────┐   │ │
│ │             │ MM1 │              │ MM2 │              │ MM3 │   │ │
│ │             └─────┘              └─────┘              └─────┘   │ │
│ │                                                                 │ │
│ │ • createModelManager() - Creates new entry in the map           │ │
│ │ • getModelManager() - Returns existing entry from the map       │ │
│ │ • removeModel() - Removes entry and disconnects from IBKR       │ │
│ │ • clearAll() - Clears all entries and disconnects all           │ │
│ └─────────────────────────────────────────────────────────────────┘ │
│                                                                     │
│ • Synchronized access with m_mutex                                  │
│ • Thread-safe singleton pattern                                     │
│ • Accessed via ModelManagerFactory::getInstance()                   │
└─────────────────────────────────────────────────────────────────────┘

Flow of Model Creation and Thread Management:
1. InputManager calls factory.getModelManager(symbol, true, params)
2. Factory creates or updates ModelManager in m_managers map
3. Factory calls appState.registerModelThread(symbol, modelManager)
4. AppState creates thread in m_modelThreads map running runModelManagerThread()
5. Thread connects to IBKR and processes data until signaled to stop

Flow of Model/Thread Removal:
1. InputManager calls factory.removeModel(symbol)
2. Factory removes ModelManager from m_managers map after disconnecting
3. InputManager calls appState.requestThreadStop(symbol)
4. AppState signals thread to stop via m_threadRunFlags[symbol] = false
5. Thread completes cleanup and exits, AppState joins thread

This design separates concerns:
- ModelManagerFactory: Manages model objects and their lifecycle
- AppState: Manages threads and their execution state
- InputManager: Coordinates between the two
*/
namespace app_state {
    enum class ThreadState;
}

namespace model_manager {

// Forward declarations
class ModelManager;
enum class TimeWindowUnit;

class ModelManagerFactory {
private:
    // Singleton pattern
    static std::unique_ptr<ModelManagerFactory> s_instance;
    static std::mutex s_instanceMutex;
    
    // Map of symbol to model manager
    std::map<std::string, std::shared_ptr<ModelManager>> m_managers;
    mutable std::mutex m_mutex;
    
    // Private constructor (singleton pattern)
    ModelManagerFactory() = default;

public:
    // Delete copy constructor and assignment (singleton pattern)
    ModelManagerFactory(const ModelManagerFactory&) = delete;
    ModelManagerFactory& operator=(const ModelManagerFactory&) = delete;
    
    // Get the singleton instance of the factory
    static ModelManagerFactory& getInstance();
    
    // Create a new model manager for a symbol
    std::shared_ptr<ModelManager> createModelManager(
        const std::string& symbol, 
        size_t windowSize, 
        TimeWindowUnit windowUnit
    );
    
    // Get an existing model manager for a symbol
    std::shared_ptr<ModelManager> getModelManager(const std::string& symbol);
    
    // Initialize a model from JSON data, creating it if it doesn't exist
    bool initModelFromJson(
        const std::string& symbol, 
        const nlohmann::json& jsonData, 
        size_t windowSize, 
        TimeWindowUnit windowUnit
    );
    
    // Check if a model exists for a given symbol
    bool hasModel(const std::string& symbol);
    
    // Remove a model for a given symbol
    bool removeModel(const std::string& symbol);
    
    // Get all symbols that have model managers
    std::vector<std::string> getAllSymbols() const;
    
    // Clear all model managers
    void clearAll();
    
    // Backward compatibility methods
    // Create or update a model manager for a symbol
    std::shared_ptr<ModelManager> getModelManager(
        const std::string& symbol,
        bool shouldStartThread,
        const nlohmann::json& params = nlohmann::json());
    
    // Get a model manager without starting a thread (alias for getModelManager)
    std::shared_ptr<ModelManager> getExistingModelManager(const std::string& symbol);
    
    // Get all available model managers
    std::map<std::string, std::shared_ptr<ModelManager>> getAllModelManagers() const;
    
    // Clear and remove a model manager (alias for removeModel)
    bool removeModelManager(const std::string& symbol);
    
    // Clear all model managers (alias for clearAll)
    void clearAllModelManagers();
};

} // namespace model_manager

#endif // MODEL_MANAGER_FACTORY_HPP 