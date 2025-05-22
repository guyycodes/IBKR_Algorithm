#ifndef MODEL_MANAGER_HPP
#define MODEL_MANAGER_HPP

#include "raw_data_model/raw_data_model.hpp"
#include "technical_calculator/technical_calculator.hpp"
#include "../connection_manager/connection_manager.hpp"
#include "../connection_manager/api_functions/api_functions.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <mutex>
#include <map>
#include <atomic>

// Forward declaration to avoid circular dependency
namespace connection_manager {
    class ConnectionManager;
}

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
 *     │   ├── Contains RawDataModel for "AAPL" from RawDataModelManager
 *     │   │   ├── Contains TradingParams {lots: 1, margin: "50%", ...}
 *     │   │   │
 *     │   │   └── Contains STK_Q for "AAPL" (Thread-safe queue for stock ticks)
 *     │   │       ├── Tick {price: 190.25, volume: 100, timestamp: 1633455600000}
 *     │   │       ├── Tick {price: 190.30, volume: 200, timestamp: 1633455601200}
 *     │   │       ├── ... (only ticks within the last 5 minutes)
 *     │   │       └── Tick {price: 190.45, volume: 150, timestamp: 1633455899800}
 *     │   │
 *     │   ├── Contains TechnicalCalculator
 *     │   │   └── Processes queue data to calculate metrics, indicators and signals
 *     │   │
 *     │   └── Contains own ConnectionManager with API connection to IBKR
 *     │       └── Feeds data directly to STK_Q queue for "AAPL"
 *     │
 *     └── ModelManager for "MSFT" (1-min window)
 *         ├── Contains RawDataModel for "MSFT" from RawDataModelManager
 *         │   ├── Contains TradingParams
 *         │   │
 *         │   └── Contains STK_Q for "MSFT" (Thread-safe queue for stock ticks)
 *         │      ├── Tick {price: 190.25, volume: 100, timestamp: 1633455600000}
 *         │      ├── Tick {price: 190.30, volume: 200, timestamp: 1633455601200}
 *         │      ├── ... (only ticks within the last 5 minutes)
 *         │      └── Tick {price: 190.45, volume: 150, timestamp: 1633455899800}
 *         │ 
 *         ├── Contains TechnicalCalculator
 *         │   └── Processes queue data to calculate metrics, indicators and signals
 *         │
 *         └── Contains own ConnectionManager with API connection to IBKR
 *             └── Feeds data directly to STK_Q queue for "MSFT"
 * 
 * Usage flow:
 * 1. Client code creates/gets a ModelManager with a specified time window
 * 2. ModelManager establishes its own IBKR connection
 * 3. As ticks arrive from IBKR API, they are added via addTick()
 * 4. ModelManager automatically prunes old data outside the time window
 * 5. TechnicalCalculator processes the data in the queue to generate metrics
 * 6. Client code can always access only the data within the specified time window
 */
namespace model_manager {

// Enum to specify the unit for the time window
enum class TimeWindowUnit {
    SECONDS,
    MINUTES,
    HOURS
};

// Forward declarations
class ModelManager;

/**
 * ModelManager class
 * 
 * This class manages a model for a specific trading symbol (e.g., "AAPL").
 * It wraps a RawDataModel with time window functionality to maintain a limited
 * history of market data. The history length is controlled by the windowSize and
 * windowUnit parameters.
 * 
 * Key features:
 *   1. Time-window based data management (e.g., keep last 5 minutes of data)
 *   2. Automatic pruning of old data
 *   3. Direct connection to IBKR API for real-time data
 *   4. Embedded TechnicalCalculator for analyzing market data and generating metrics
 *   5. Integration with AppState for thread management
 *      - Fetches data specifically for its assigned symbol
 *      - Calculator runs in the same thread as data processing
 *   6. Methods to manipulate and access the data model
 */
class ModelManager {
private:
    // The underlying raw data model that stores the actual data
    // This is the primary data model that contains all trading parameters and market data
    std::shared_ptr<raw_data_model::RawDataModel> m_rawDataModel;
    
    // Technical calculator for this model
    technical_calculator::TechnicalCalculator m_calculator;
    
    // Time window settings for historical data retention
    size_t m_windowSize;                // Size of the time window
    TimeWindowUnit m_windowUnit;        // Unit for the time window (seconds, minutes, hours)
    std::chrono::system_clock::time_point m_lastPruneTime; // When data was last pruned
    
    // IBKR API connection objects
    std::unique_ptr<connection_manager::ConnectionManager> m_connManager;
    int m_requestId; // Request ID for this symbol's market data
    std::atomic<bool> m_connected; // Flag indicating if we're connected to IBKR API
    
    // Connection retry tracking
    std::atomic<int> m_connectionAttempts{0};
    static const int MAX_CONNECTION_ATTEMPTS = 5;
    std::chrono::system_clock::time_point m_lastConnectionAttempt;
    static const int CONNECTION_RETRY_DELAY_SECONDS = 7;
    
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
    ModelManager(const std::string& symbol, size_t windowSize = 60, TimeWindowUnit windowUnit = TimeWindowUnit::MINUTES);
    
    /**
     * Destructor
     */
    ~ModelManager();
    
    /**
     * Connect to IBKR API and subscribe to market data for this symbol
     * @return True if connection successful, false otherwise
     */
    bool connectToIBKR();
    
    /**
     * Disconnect from IBKR API and cancel market data subscription
     */
    void disconnectFromIBKR();
    
    /**
     * Check if currently connected to IBKR API
     * @return True if connected, false otherwise
     */
    bool isConnected() const;
    
    /**
     * Get the connection status string
     * @return String describing connection status
     */
    std::string getConnectionStatus() const;
    
    /**
     * Initialize the model from JSON configuration data
     * @param jsonData JSON object containing configuration data
     * @return True if successfully initialized, false otherwise
     */
    bool initFromJson(const nlohmann::json& jsonData);
    
    /**
     * Add a new market data tick and immediately prune the queue
     * @param tick The new market data tick to add
     */
    void addTick(const stock_data_tick::StockData& tick);
    
    /**
     * Get the underlying raw data model for advanced operations
     * @return Shared pointer to the raw data model
     */
    std::shared_ptr<raw_data_model::RawDataModel> getRawDataModel() const;
    
    /**
     * Get the technical calculator for this model
     * @return Reference to the technical calculator
     */
    technical_calculator::TechnicalCalculator& getCalculator() {
        return m_calculator;
    }
    
    /**
     * Get the symbol this model is for
     * @return Symbol string (e.g., "AAPL")
     */
    std::string getSymbol() const;
    
    /**
     * Get the trading parameters for this model
     * @return Reference to the trading parameters
     */
    const raw_data_model::TradingParams& getParams() const;
    
    /**
     * Get all ticks that fall within the current time window
     * @return Vector of market data ticks
     */
    std::vector<stock_data_tick::StockData> getTicksInWindow() const;
    
    /**
     * Get the most recent market data tick
     * @return Pointer to the latest tick, or nullptr if none
     */
    const stock_data_tick::StockData* getLatestTick() const;
    
    /**
     * Get the total number of ticks currently stored in the queue
     * @return Count of ticks in the queue
     */
    size_t getTickCount() const;
    
    /**
     * Clear all stored ticks (trading parameters are preserved)
     */
    void clearTicks();
    
    /**
     * Get the current time window settings
     * @return Pair of window size and unit
     */
    std::pair<size_t, TimeWindowUnit> getTimeWindow() const;
    
    /**
     * Update the time window settings and prune data accordingly
     * @param windowSize New window size
     * @param windowUnit New window unit
     */
    void setTimeWindow(size_t windowSize, TimeWindowUnit windowUnit);
    
    /**
     * Process a batch of data items from the queue
     * @param maxItems Maximum number of items to process in one batch
     * @return Number of items actually processed
     */
    size_t processQueueData(size_t maxItems);
    
    /**
     * Get the number of connection attempts made so far
     * @return Connection attempt count
     */
    int getConnectionAttempts() const { return m_connectionAttempts; }
    
    /**
     * Get the maximum number of connection attempts
     * @return Maximum connection attempts
     */
    static int getMaxConnectionAttempts() { return MAX_CONNECTION_ATTEMPTS; }
};

// Include ModelManagerFactory
// #include "model_manager_factory.hpp"

} // namespace model_manager

#endif // MODEL_MANAGER_HPP 