#ifndef MODEL_MANAGER_HPP
#define MODEL_MANAGER_HPP

#include "raw_data_model/raw_data_model.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <mutex>
#include <map>

/**
 * @file model_manager.hpp
 * 
 * @brief This file provides a time-window based wrapper around the raw_data_model.
 * 
 * Data Structure Example:
 * -----------------------
 * 
 * The model_manager manages data from the RawDataModel in a time-controlled way:
 * 
 * ModelManagerFactory (Singleton)
 * └── Maps symbols to ModelManager instances
 *     ├── ModelManager for "AAPL" (5-min window)
 *     │   └── Contains RawDataModel for "AAPL" from RawDataModelManager
 *     │       ├── Contains TradingParams {lots: 1, margin: "50%", ...}
 *     │       │
 *     │       └── Contains STK_Q for "AAPL" (Thread-safe queue for stock ticks)
 *     │           ├── Tick {price: 190.25, volume: 100, timestamp: 1633455600000}
 *     │           ├── Tick {price: 190.30, volume: 200, timestamp: 1633455601200}
 *     │           ├── ... (only ticks within the last 5 minutes)
 *     │           └── Tick {price: 190.45, volume: 150, timestamp: 1633455899800}
 *     │
 *     └── ModelManager for "MSFT" (1-min window)
 *         └── Contains RawDataModel for "MSFT" from RawDataModelManager
 *             ├── Contains TradingParams
 *             │
 *             └── Contains STK_Q for "MSFT" (Thread-safe queue for stock ticks)
 *                ├── Tick {price: 190.25, volume: 100, timestamp: 1633455600000}
 *                ├── Tick {price: 190.30, volume: 200, timestamp: 1633455601200}
 *                ├── ... (only ticks within the last 5 minutes)
 *                └── Tick {price: 190.45, volume: 150, timestamp: 1633455899800}
 * 
 * Usage flow:
 * 1. Client code creates/gets a ModelManager with a specified time window
 * 2. As ticks arrive from IBKR API, they are added via addTick()
 * 3. ModelManager automatically prunes old data outside the time window
 * 4. Client code can always access only the data within the specified time window
 */

namespace model_manager {

/**
 * Time window units for specifying how long data should be retained
 */
enum class TimeWindowUnit {
    SECONDS,  // Keep data for a specified number of seconds
    MINUTES,  // Keep data for a specified number of minutes
    HOURS     // Keep data for a specified number of hours
};

/**
 * @class ModelManager
 * @brief Manages a single model with a sliding time window to limit data retention
 * 
 * This class wraps a RawDataModel and ensures that only data within a specified
 * time window is kept, preventing unbounded memory growth as new ticks arrive.
 */
//
// ModelManager class - Manages market data for a specific trading symbol
//
// This class is responsible for:
// 1. Maintaining a reference to the underlying RawDataModel for a symbol
// 2. Managing time window settings for historical data retention
// 3. Actively pruning the STK_Q in RawDataModel to maintain the time window
//
// DESIGN NOTE: Market data ticks are stored ONLY in the STK_Q in RawDataModel,
// not in the vector of ticks. The ModelManager automatically prunes old data
// from the queue whenever new data is added, keeping only data within the 
// specified time window.
//
// STRUCTURE:
// ModelManager contains:
//   1. A shared_ptr to a RawDataModel
//      - The RawDataModel stores the actual trading parameters (lots, margin, etc.)
//      - The RawDataModel contains a thread-safe STK_Q for stock data (ticks)
//   2. Time window configuration 
//      - Size and unit (e.g., 60 minutes)
//      - Used to actively prune the queue to maintain the time window
//   3. Methods to manipulate and access the data model
//
class ModelManager {
private:
    // The underlying raw data model that stores the actual data
    // This is the primary data model that contains all trading parameters and market data
    std::shared_ptr<raw_data_model::RawDataModel> m_rawDataModel;
    
    // Time window settings for historical data retention
    size_t m_windowSize;                // Size of the time window
    TimeWindowUnit m_windowUnit;        // Unit for the time window (seconds, minutes, hours)
    std::chrono::system_clock::time_point m_lastPruneTime; // When data was last pruned
    
    // Mutex for thread-safe operations
    mutable std::mutex m_mutex;
    
    /**
     * Convert the time window to milliseconds for comparisons
     * @return Window size in milliseconds
     */
    uint64_t windowToMilliseconds() const;
    
    /**
     * Remove data points that fall outside the current time window
     * This is the key method that prevents unbounded data accumulation
     */
    void pruneOldData();

public:
    /**
     * Constructor
     * @param symbol The trading symbol this model is for (e.g., "AAPL")
     * @param windowSize Size of the time window
     * @param windowUnit Unit for the time window (seconds, minutes, hours)
     */
    ModelManager(const std::string& symbol, size_t windowSize, TimeWindowUnit windowUnit);
    
    /**
     * Initialize model parameters from JSON configuration
     * @param jsonData JSON data containing model parameters
     * @return True if initialization succeeded, false otherwise
     */
    bool initFromJson(const nlohmann::json& jsonData);
    
    /**
     * Add a new market data tick and immediately prune the queue
     * 
     * This method adds the tick to the RawDataModel's queue and then
     * calls pruneOldData() to remove any data points that fall outside
     * the specified time window. This ensures the queue size is constantly
     * managed and prevents unbounded memory growth.
     * 
     * @param tick The new market data tick to add
     */
    void addTick(const raw_data_model::MarketDataTick& tick);
    
    /**
     * Get the underlying raw data model (for advanced operations)
     * @return Shared pointer to the raw data model
     */
    std::shared_ptr<raw_data_model::RawDataModel> getRawDataModel() const;
    
    /**
     * Get the symbol this model is for
     * @return The symbol string (e.g., "AAPL")
     */
    std::string getSymbol() const;
    
    /**
     * Get the trading parameters for this model
     * @return Reference to the trading parameters
     */
    const raw_data_model::TradingParams& getParams() const;
    
    /**
     * Get all ticks that fall within the current time window
     * @return Vector of market data ticks within the time window
     */
    std::vector<raw_data_model::MarketDataTick> getTicksInWindow() const;
    
    /**
     * Get the most recent market data tick
     * @return Pointer to the latest tick, or nullptr if no ticks available
     */
    const raw_data_model::MarketDataTick* getLatestTick() const;
    
    /**
     * Get the total number of ticks currently stored
     * @return Count of ticks
     */
    size_t getTickCount() const;
    
    /**
     * Clear all stored ticks (trading parameters are preserved)
     */
    void clearTicks();
    
    /**
     * Get the current time window settings
     * @return Pair containing window size and unit
     */
    std::pair<size_t, TimeWindowUnit> getTimeWindow() const;
    
    /**
     * Update the time window settings and prune data accordingly
     * @param windowSize New window size
     * @param windowUnit New window unit
     */
    void setTimeWindow(size_t windowSize, TimeWindowUnit windowUnit);
};

//
// ModelManagerFactory - Singleton manager of ModelManager instances
//
// This class is responsible for creating, storing, and managing ModelManager instances.
// It ensures that only one ModelManager exists per symbol (singleton per symbol).
//
// ARCHITECTURE:
// - ModelManagerFactory (Singleton)
//   |
//   ├── Map<symbol, ModelManager>
//   |    |
//   |    ├── ModelManager for symbol "AAPL"
//   |    |    └── Contains RawDataModel for "AAPL"
//   |    |
//   |    ├── ModelManager for symbol "MSFT" 
//   |    |    └── Contains RawDataModel for "MSFT"
//   |    |
//   |    └── ... (more symbols)
//   |
//   └── Methods to create/get/manage ModelManagers
//
class ModelManagerFactory {
private:
    // Static singleton instance
    static std::unique_ptr<ModelManagerFactory> s_instance;
    static std::mutex s_instanceMutex; // For thread-safe initialization
    
    // Map of symbol to ModelManager instances
    // This is the core data structure that maintains all model managers
    std::map<std::string, std::shared_ptr<ModelManager>> m_managers;
    mutable std::mutex m_mutex; // For thread-safe operations on the map
    
    // Private constructor (singleton pattern)
    ModelManagerFactory() {}
    
public:
    // Delete copy constructor and assignment (singleton pattern)
    ModelManagerFactory(const ModelManagerFactory&) = delete;
    ModelManagerFactory& operator=(const ModelManagerFactory&) = delete;
    
    // Get the singleton instance of the factory
    // This is the only way to access the factory
    static ModelManagerFactory& getInstance();
    
    // Create a new model manager for a symbol
    // If a manager already exists for the symbol, it will be replaced.
    std::shared_ptr<ModelManager> createModelManager(
        const std::string& symbol, 
        size_t windowSize, 
        TimeWindowUnit windowUnit
    );
    
    // Get an existing model manager for a symbol
    // Returns nullptr if no manager exists for the symbol
    std::shared_ptr<ModelManager> getModelManager(const std::string& symbol);
    
    // Initialize a model from JSON data
    // Will create the model if it doesn't exist
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
};

} // namespace model_manager

#endif 