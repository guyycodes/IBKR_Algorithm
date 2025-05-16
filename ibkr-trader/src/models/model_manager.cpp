#include "model_manager.hpp"
#include "../util/app_state/app_state.hpp"

namespace model_manager {

//
// ModelManager implementation
//

/**
 * Constructor that creates a ModelManager for a specific symbol with a time window
 * 
 * This constructor gets the raw data model from the RawDataModelManager singleton
 * and establishes the time window settings for data retention.
 */
ModelManager::ModelManager(const std::string& symbol, size_t windowSize, TimeWindowUnit windowUnit)
    : m_windowSize(windowSize), m_windowUnit(windowUnit), m_lastPruneTime(std::chrono::system_clock::now()) {
    
    // Get the raw data model from the raw data manager singleton
    // This ensures we're using the same data model instance across the application
    m_rawDataModel = raw_data_model::RawDataModelManager::getInstance().getModel(symbol);
}

/**
 * Convert the time window specification to milliseconds for timestamp comparisons
 * 
 * This is used to determine the cutoff time for data pruning. Any data point
 * with a timestamp older than (current_time - window_in_ms) will be removed.
 */
uint64_t ModelManager::windowToMilliseconds() const {
    uint64_t milliseconds = 0;
    
    // Convert the window size to milliseconds based on the unit
    switch (m_windowUnit) {
        case TimeWindowUnit::SECONDS:
            milliseconds = m_windowSize * 1000;
            break;
        case TimeWindowUnit::MINUTES:
            milliseconds = m_windowSize * 60 * 1000;
            break;
        case TimeWindowUnit::HOURS:
            milliseconds = m_windowSize * 60 * 60 * 1000;
            break;
    }
    
    return milliseconds;
}

/**
 * Remove data points that fall outside the specified time window
 * 
 * This is the key method that prevents uncontrolled data accumulation.
 * It calculates a cutoff timestamp and removes any ticks older than that
 * from the queue.
 * 
 * This implementation uses the STK_Q::removeOlderThan method to efficiently
 * filter out old data points from the queue based on the configured time window.
 */
void ModelManager::pruneOldData() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
    
    // Calculate cutoff time by subtracting the window size from current time
    auto cutoffMs = nowMs - windowToMilliseconds();

    // Get access to the queue
    auto* queue = m_rawDataModel->getStockQueue();
    if (!queue) return;
    
    // Prune the queue by removing items older than the cutoff time
    queue->removeOlderThan(cutoffMs);
    
    // Update last prune time
    m_lastPruneTime = now;
}

/**
 * Initialize the model from JSON configuration data
 * 
 * This passes the JSON data to the underlying raw data model.
 */
bool ModelManager::initFromJson(const nlohmann::json& jsonData) {
    return m_rawDataModel->initFromJson(jsonData);
}

/**
 * Add a new market data tick and immediately prune the queue
 * 
 * This method adds the tick to the RawDataModel's queue and then
 * calls pruneOldData() to remove any data points that fall outside
 * the specified time window. This ensures the queue size is constantly
 * managed and prevents unbounded memory growth.
 * 
 * Note: This delegates the actual storage to RawDataModel::addTick,
 * but adds time window management on top of it.
 * 
 * @param tick The new market data tick to add
 */
void ModelManager::addTick(const raw_data_model::MarketDataTick& tick) {
    // Add the tick to the underlying model's queue
    m_rawDataModel->addTick(tick);
    
    // Prune old data immediately after adding a new tick
    // This ensures we maintain the time window constraint
    pruneOldData();
}

/**
 * Get the underlying raw data model for advanced operations
 */
std::shared_ptr<raw_data_model::RawDataModel> ModelManager::getRawDataModel() const {
    return m_rawDataModel;
}

/**
 * Get the symbol this model is for
 */
std::string ModelManager::getSymbol() const {
    return m_rawDataModel->getSymbol();
}

/**
 * Get the trading parameters for this model
 */
const raw_data_model::TradingParams& ModelManager::getParams() const {
    return m_rawDataModel->getParams();
}

/**
 * Get all ticks that fall within the current time window
 * 
 * This provides a filtered view of the ticks based on the current time window.
 * Since we're now storing ticks only in the queue rather than in vectors,
 * this method creates a vector representation of the queue contents for
 * compatibility with existing code.
 * 
 * Note: This creates a copy of data and should be used sparingly.
 */
std::vector<raw_data_model::MarketDataTick> ModelManager::getTicksInWindow() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
    
    // Calculate cutoff time by subtracting the window size from current time
    auto cutoffMs = nowMs - windowToMilliseconds();
    
    // Create a vector to hold the filtered ticks
    std::vector<raw_data_model::MarketDataTick> windowTicks;
    
    // Get access to the queue
    auto* queue = m_rawDataModel->getStockQueue();
    if (!queue) return windowTicks;
    
    // Create a temporary queue to prevent modifying the original
    std::queue<stk_q::STK_Q_Data> tempQueue;
    stk_q::STK_Q_Data data;
    
    // Get a copy of the queue
    auto queueSize = queue->size();
    windowTicks.reserve(queueSize); // Reserve space for efficiency
    
    // We'll create a temporary copy of the queue by popping and re-adding
    for (size_t i = 0; i < queueSize; i++) {
        if (queue->pop(data)) {
            // Only include data within the time window
            if (static_cast<uint64_t>(data.time) >= cutoffMs) {
                // Convert queue data back to MarketDataTick
                raw_data_model::MarketDataTick tick;
                tick.price = data.price;
                tick.volume = data.size;
                tick.timestamp = data.time;
                
                // Add to vector
                windowTicks.push_back(tick);
            }
            
            // Re-add to the original queue
            queue->push(data);
        }
    }
    
    return windowTicks;
}

/**
 * Get the most recent market data tick
 * 
 * This method gets the latest tick directly from the queue, which is
 * where ticks are stored in the current design.
 * 
 * Note: This returns nullptr if the queue is empty or can't be accessed.
 */
const raw_data_model::MarketDataTick* ModelManager::getLatestTick() const {
    static raw_data_model::MarketDataTick latestTick;
    
    if (m_rawDataModel->getLatestTickFromQueue(latestTick)) {
        return &latestTick;
    }
    
    return nullptr;
}

/**
 * Get the total number of ticks currently stored in the queue
 * 
 * This returns the size of the queue rather than the vector
 * since ticks are now stored only in the queue.
 */
size_t ModelManager::getTickCount() const {
    auto* queue = m_rawDataModel->getStockQueue();
    if (!queue) {
        return 0;
    }
    return queue->size();
}

/**
 * Clear all stored ticks (trading parameters are preserved)
 * 
 * This clears the STK_Q queue of all stored ticks.
 */
void ModelManager::clearTicks() {
    // This will clear the queue in RawDataModel
    m_rawDataModel->clearTicks();
}

/**
 * Get the current time window settings
 */
std::pair<size_t, TimeWindowUnit> ModelManager::getTimeWindow() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_windowSize, m_windowUnit};
}

/**
 * Update the time window settings and prune data accordingly
 * 
 * This allows dynamic adjustment of the time window at runtime.
 * After changing the window, data is immediately pruned to match the new window.
 */
void ModelManager::setTimeWindow(size_t windowSize, TimeWindowUnit windowUnit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_windowSize = windowSize;
    m_windowUnit = windowUnit;
    
    // Prune data with new window settings immediately
    pruneOldData();
}

/**
 * Process a batch of data items from the queue
 * 
 * This method is designed to be called from the ModelManager's thread.
 * It fetches a batch of items from the queue and processes them according
 * to the trading rules for this symbol.
 * 
 * @param maxItems Maximum number of items to process in one batch
 * @return Number of items actually processed
 */
size_t ModelManager::processQueueData(size_t maxItems) {
    // Get the queue
    auto* queue = m_rawDataModel->getStockQueue();
    if (!queue) return 0;
    
    size_t processedCount = 0;
    stk_q::STK_Q_Data data;
    
    // Process up to maxItems from the queue
    for (size_t i = 0; i < maxItems; i++) {
        // Try to get the next item
        if (!queue->pop(data)) {
            break; // No more items in queue
        }
        
        // Skip items outside our time window
        auto now = std::chrono::system_clock::now();
        auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
        auto cutoffMs = nowMs - windowToMilliseconds();
        
        if (static_cast<uint64_t>(data.time) < cutoffMs) {
            continue; // Skip old data
        }
        
        // Process this tick data according to trading rules
        // In a real implementation, this would apply trading strategies,
        // generate signals, etc.
        
        // For now, just count it as processed
        processedCount++;
    }
    
    return processedCount;
}

//
// ModelManagerFactory implementation
//

// Initialize static members for the singleton pattern
std::unique_ptr<ModelManagerFactory> ModelManagerFactory::s_instance = nullptr;
std::mutex ModelManagerFactory::s_instanceMutex;

/**
 * Get the singleton instance of the factory
 * 
 * This follows the thread-safe singleton pattern to ensure only one instance exists.
 */
ModelManagerFactory& ModelManagerFactory::getInstance() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance) {
        s_instance = std::unique_ptr<ModelManagerFactory>(new ModelManagerFactory());
    }
    return *s_instance;
}

/**
 * Create a new model manager for a symbol with specified time window
 * 
 * This creates a new ModelManager instance and adds it to the managers map.
 * If a manager already exists for the symbol, it will be replaced.
 */
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

/**
 * Get an existing model manager for a symbol
 * 
 * Returns nullptr if no manager exists for the specified symbol.
 */
std::shared_ptr<ModelManager> ModelManagerFactory::getModelManager(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_managers.find(symbol);
    if (it != m_managers.end()) {
        return it->second;
    }
    
    return nullptr;
}

/**
 * Initialize a model from JSON data, creating it if it doesn't exist
 * 
 * This method gets or creates a model manager and initializes it with the JSON data.
 * It also ensures that the model has a thread registered with AppState,
 * which is crucial when reusing an existing model after its thread was previously stopped.
 */
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

/**
 * Check if a model exists for a given symbol
 */
bool ModelManagerFactory::hasModel(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_managers.find(symbol) != m_managers.end();
}

/**
 * Remove a model for a given symbol
 * 
 * Returns true if a model was removed, false if no model existed.
 */
bool ModelManagerFactory::removeModel(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Stop and remove the thread for this symbol
    app_state::AppState::getInstance().removeModelThread(symbol);
    
    // Remove the model from our map
    return m_managers.erase(symbol) > 0;
}

/**
 * Get all symbols that have model managers
 */
std::vector<std::string> ModelManagerFactory::getAllSymbols() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> symbols;
    for (const auto& pair : m_managers) {
        symbols.push_back(pair.first);
    }
    return symbols;
}

/**
 * Clear all model managers
 * 
 * This removes all model managers from the factory.
 */
void ModelManagerFactory::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Stop all running threads
    app_state::AppState::getInstance().stopAllThreads();
    
    // Clear all models
    m_managers.clear();
}

} // namespace model_manager 