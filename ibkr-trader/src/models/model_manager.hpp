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
 *     │       └── Contains filtered MarketDataTicks (only the last 5 minutes)
 *     │           ├── Tick {price: 190.25, volume: 100, timestamp: 1633455600000}
 *     │           ├── Tick {price: 190.30, volume: 200, timestamp: 1633455601200}
 *     │           ├── ... (only ticks within the last 5 minutes)
 *     │           └── Tick {price: 190.45, volume: 150, timestamp: 1633455899800}
 *     │       └── Contains TradingParams {lots: 1, margin: "50%", ...}
 *     │
 *     └── ModelManager for "MSFT" (1-min window)
 *         └── Contains RawDataModel for "MSFT" from RawDataModelManager
 *             └── Contains filtered MarketDataTicks (only the last 1 minute)
 *             └── Contains TradingParams
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
class ModelManager {
private:
    // Underlying raw data model from the RawDataModelManager singleton
    std::shared_ptr<raw_data_model::RawDataModel> m_rawDataModel;
    
    // Time window settings that control how much historical data to keep
    size_t m_windowSize;        // Size of the window (in units specified by m_windowUnit)
    TimeWindowUnit m_windowUnit; // Unit for the window size (seconds, minutes, hours)
    
    // Timestamp of when data was last pruned (to avoid pruning too frequently)
    std::chrono::time_point<std::chrono::system_clock> m_lastPruneTime;
    
    // Mutex for thread safety when accessing/modifying data
    mutable std::mutex m_mutex;
    
    /**
     * Convert the time window to milliseconds for timestamp comparisons
     * @return Window size in milliseconds
     */
    uint64_t windowToMilliseconds() const;
    
    /**
     * Remove data points outside the time window to maintain the sliding window
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
     * Add a new market data tick and automatically prune old data if needed
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

/**
 * @class ModelManagerFactory
 * @brief Singleton factory that creates and manages ModelManager instances
 * 
 * This class provides centralized access to all model managers in the system,
 * allowing retrieval by symbol and ensuring only one manager exists per symbol.
 */
class ModelManagerFactory {
private:
    // Private constructor for singleton pattern
    ModelManagerFactory() = default;
    
    // Map that associates trading symbols with their ModelManager instances
    std::map<std::string, std::shared_ptr<ModelManager>> m_managers;
    
    // Mutex for thread safety when accessing the managers map
    mutable std::mutex m_mutex;
    
    // Static singleton instance and its mutex
    static std::unique_ptr<ModelManagerFactory> s_instance;
    static std::mutex s_instanceMutex;

public:
    // Delete copy/move constructors and assignment operators to ensure singleton
    ModelManagerFactory(const ModelManagerFactory&) = delete;
    ModelManagerFactory(ModelManagerFactory&&) = delete;
    ModelManagerFactory& operator=(const ModelManagerFactory&) = delete;
    ModelManagerFactory& operator=(ModelManagerFactory&&) = delete;
    
    // Destructor
    ~ModelManagerFactory() = default;
    
    /**
     * Get the singleton instance of the factory
     * @return Reference to the singleton instance
     */
    static ModelManagerFactory& getInstance();
    
    /**
     * Create a new model manager for a symbol with specified time window
     * @param symbol Trading symbol (e.g., "AAPL")
     * @param windowSize Size of the time window
     * @param windowUnit Unit for the time window
     * @return Shared pointer to the created model manager
     */
    std::shared_ptr<ModelManager> createModelManager(
        const std::string& symbol, 
        size_t windowSize, 
        TimeWindowUnit windowUnit
    );
    
    /**
     * Get an existing model manager for a symbol
     * @param symbol Trading symbol to look up
     * @return Shared pointer to the model manager, or nullptr if not found
     */
    std::shared_ptr<ModelManager> getModelManager(const std::string& symbol);
    
    /**
     * Initialize a model from JSON data, creating it if it doesn't exist
     * @param symbol Trading symbol
     * @param jsonData JSON configuration data
     * @param windowSize Size of the time window
     * @param windowUnit Unit for the time window
     * @return True if initialization succeeded, false otherwise
     */
    bool initModelFromJson(
        const std::string& symbol, 
        const nlohmann::json& jsonData, 
        size_t windowSize, 
        TimeWindowUnit windowUnit
    );
    
    /**
     * Check if a model exists for a given symbol
     * @param symbol Trading symbol to check
     * @return True if model exists, false otherwise
     */
    bool hasModel(const std::string& symbol);
    
    /**
     * Remove a model for a given symbol
     * @param symbol Trading symbol to remove
     * @return True if model was removed, false if it didn't exist
     */
    bool removeModel(const std::string& symbol);
    
    /**
     * Get all symbols that have model managers
     * @return Vector of symbol strings
     */
    std::vector<std::string> getAllSymbols() const;
    
    /**
     * Clear all model managers
     */
    void clearAll();
};

} // namespace model_manager

#endif 