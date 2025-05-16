#include "model_manager.hpp"

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
 * It calculates a cutoff timestamp and removes any ticks older than that.
 */
void ModelManager::pruneOldData() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
    
    // Calculate cutoff time by subtracting the window size from current time
    auto cutoffMs = nowMs - windowToMilliseconds();
    
    // Get all ticks from the raw data model
    const auto& ticks = m_rawDataModel->getTicks();
    
    // If no ticks or all ticks are within window (front/oldest tick is newer than cutoff), return
    if (ticks.empty() || ticks.front().timestamp >= cutoffMs) {
        return;
    }
    
    // Create a new vector with only the ticks within the window
    // This is more efficient than removing items one by one
    std::vector<raw_data_model::MarketDataTick> newTicks;
    newTicks.reserve(ticks.size()); // Pre-allocate memory to avoid reallocations
    
    // Copy only the ticks that are within the time window
    for (const auto& tick : ticks) {
        if (tick.timestamp >= cutoffMs) {
            newTicks.push_back(tick);
        }
    }
    
    // Clear and replace the ticks in the raw data model
    m_rawDataModel->clearTicks();
    for (const auto& tick : newTicks) {
        m_rawDataModel->addTick(tick);
    }
    
    // Update last prune time to avoid pruning too frequently
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
 * Add a new market data tick and prune old data if necessary
 * 
 * This method adds the tick to the underlying model and then checks
 * if pruning is needed based on the elapsed time since last prune.
 */
void ModelManager::addTick(const raw_data_model::MarketDataTick& tick) {
    // Add the tick to the underlying model
    m_rawDataModel->addTick(tick);
    
    // Check if it's time to prune old data
    auto now = std::chrono::system_clock::now();
    auto elapsed = now - m_lastPruneTime;
    
    // Prune every second (can be adjusted for performance if needed)
    // This ensures memory usage doesn't grow too large between pruning operations
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 1) {
        pruneOldData();
    }
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
 * This provides a filtered view of the ticks based on the current time window,
 * without modifying the underlying raw data model.
 */
std::vector<raw_data_model::MarketDataTick> ModelManager::getTicksInWindow() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
    
    // Calculate cutoff time by subtracting the window size from current time
    auto cutoffMs = nowMs - windowToMilliseconds();
    
    // Get all ticks from the raw data model
    const auto& ticks = m_rawDataModel->getTicks();
    
    // Create a new vector with only the ticks within the window
    std::vector<raw_data_model::MarketDataTick> windowTicks;
    windowTicks.reserve(ticks.size()); // Pre-allocate memory for efficiency
    
    // Copy only the ticks that are within the time window
    for (const auto& tick : ticks) {
        if (tick.timestamp >= cutoffMs) {
            windowTicks.push_back(tick);
        }
    }
    
    return windowTicks;
}

/**
 * Get the most recent market data tick
 */
const raw_data_model::MarketDataTick* ModelManager::getLatestTick() const {
    return m_rawDataModel->getLatestTick();
}

/**
 * Get the total number of ticks currently stored
 */
size_t ModelManager::getTickCount() const {
    return m_rawDataModel->getTickCount();
}

/**
 * Clear all stored ticks (trading parameters are preserved)
 */
void ModelManager::clearTicks() {
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
 */
bool ModelManagerFactory::initModelFromJson(
    const std::string& symbol, 
    const nlohmann::json& jsonData, 
    size_t windowSize, 
    TimeWindowUnit windowUnit
) {
    // Get or create the model manager
    std::shared_ptr<ModelManager> manager;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_managers.find(symbol);
        if (it != m_managers.end()) {
            manager = it->second;
        } else {
            // Create a new manager if one doesn't exist
            manager = std::make_shared<ModelManager>(symbol, windowSize, windowUnit);
            m_managers[symbol] = manager;
        }
    }
    
    // Initialize the manager with JSON data
    return manager->initFromJson(jsonData);
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
    m_managers.clear();
}

} // namespace model_manager 