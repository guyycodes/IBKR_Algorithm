#include "model_manager.hpp"
#include "model_manager_factory.hpp"
#include "../util/app_state/app_state.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <random>

namespace model_manager {

//
// ApiCallback implementation
//

ApiCallback::ApiCallback(connection::IBKRTrader& trader, ModelManager& manager, const std::string& symbol)
    : m_manager(manager), m_symbol(symbol), m_requestId(-1), m_lastPrice(0.0), m_trader(trader) {
    std::cout << "[ApiCallback] Created for symbol: " << symbol << std::endl;
}

void ApiCallback::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
    // Check if this is for our symbol's request ID
    if (tickerId != m_requestId) return;
    
    std::string fieldName;
    
    switch (field) {
        case TickType::BID: fieldName = "BID"; break;
        case TickType::ASK: fieldName = "ASK"; break;
        case TickType::LAST: fieldName = "LAST"; break;
        case TickType::HIGH: fieldName = "HIGH"; break;
        case TickType::LOW: fieldName = "LOW"; break;
        case TickType::CLOSE: fieldName = "CLOSE"; break;
        case TickType::OPEN: fieldName = "OPEN"; break;
        default: fieldName = "UNKNOWN_" + std::to_string(field); break;
    }
    
    // Print live update with consistent formatting
    std::time_t now = std::time(nullptr);
    std::cout << "[" << std::put_time(std::localtime(&now), "%H:%M:%S") 
              << "] (Thread " << m_symbol << ") Market Data - Field: " << std::setw(10) << fieldName 
              << ", Price: " << std::fixed << std::setprecision(2) << price;
    
    if (field == TickType::BID || field == TickType::ASK) {
        std::cout << (attrib.preOpen ? " (Pre-open)" : "");
    }
    std::cout << std::endl;
    
    // Store the last price we've seen
    m_lastPrice = price;
    
    // If it's a price we care about (LAST, BID, ASK), route it to the model manager
    if (field == TickType::LAST || field == TickType::BID || field == TickType::ASK) {
        routeTickToModelManager(price, 0);  // Zero volume for price-only updates
    }
}

void ApiCallback::tickSize(TickerId tickerId, TickType field, Decimal size) {
    // Check if this is for our symbol's request ID
    if (tickerId != m_requestId) return;
    
    std::string fieldName;
    
    switch (field) {
        case TickType::BID_SIZE: fieldName = "BID_SIZE"; break;
        case TickType::ASK_SIZE: fieldName = "ASK_SIZE"; break;
        case TickType::LAST_SIZE: fieldName = "LAST_SIZE"; break;
        case TickType::VOLUME: fieldName = "VOLUME"; break;
        default: fieldName = "UNKNOWN_SIZE_" + std::to_string(field); break;
    }
    
    double sizeValue = static_cast<double>(size);
    
    // Print live update with consistent formatting
    std::time_t now = std::time(nullptr);
    std::cout << "[" << std::put_time(std::localtime(&now), "%H:%M:%S") 
              << "] (Thread " << m_symbol << ") Market Data - Field: " << std::setw(10) << fieldName 
              << ", Size: " << std::fixed << std::setw(8) << sizeValue 
              << std::endl;
    
    // If we have last price and this is volume, we can update with volume information
    if (field == TickType::VOLUME && m_lastPrice > 0) {
        routeTickToModelManager(m_lastPrice, sizeValue);
    }
    
    // If it's LAST_SIZE, we might want to update with that size
    if (field == TickType::LAST_SIZE && m_lastPrice > 0) {
        routeTickToModelManager(m_lastPrice, sizeValue);
    }
}

void ApiCallback::tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
                                Decimal size, const TickAttribLast& tickAttribLast, 
                                const std::string& exchange, const std::string& specialConditions) {
    // Check if this is for our symbol's request ID
    if (reqId != m_requestId) return;
    
    std::string tickTypeStr = tickType == 1 ? "LAST" : "ALLAST";
    int sizeInt = static_cast<int>(size);
    
    // Format time
    char timeStr[20];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&time));
    
    // Print live update with consistent formatting
    std::cout << "[" << timeStr << "] (Thread " << m_symbol << ") Tick-by-Tick - Type: " << std::setw(6) << tickTypeStr
              << ", Exchange: " << std::setw(6) << exchange
              << ", Price: " << std::fixed << std::setprecision(2) << std::setw(8) << price
              << ", Size: " << std::setw(6) << sizeInt;
    
    if (!specialConditions.empty()) {
        std::cout << ", Conditions: " << specialConditions;
    }
    std::cout << std::endl;
    
    // Store the last price we've seen
    m_lastPrice = price;
    
    // Tick-by-tick data is perfect for our ModelManager - most granular and includes both price and size
    // Create timestamp from the provided time_t
    uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
    routeTickToModelManager(price, sizeInt, timestamp);
}

void ApiCallback::error(int id, time_t errorTime, int errorCode, const std::string& errorString, 
                         const std::string& advancedOrderRejectJson) {
    // Even if not for our request ID, log all errors for diagnostic purposes
    std::cerr << "[ERROR] (Thread " << m_symbol << ") Error " << errorCode 
              << " for request " << id << ": " << errorString << std::endl;
              
    // Log advanced order reject if provided
    if (!advancedOrderRejectJson.empty()) {
        std::cerr << "[ERROR] Advanced order reject info: " << advancedOrderRejectJson << std::endl;
    }
}

void ApiCallback::setRequestId(int requestId) {
    m_requestId = requestId;
}

void ApiCallback::routeTickToModelManager(double price, double volume, uint64_t timestamp) {
    // If timestamp is 0, use current time
    if (timestamp == 0) {
        timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    }
    
    // Create a MarketDataTick object
    raw_data_model::MarketDataTick tick;
    tick.price = price;
    tick.volume = volume;
    tick.timestamp = timestamp;
    
    // Add to our model manager
    m_manager.addTick(tick);
}

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
    : m_windowSize(windowSize), m_windowUnit(windowUnit), m_lastPruneTime(std::chrono::system_clock::now()),
      m_requestId(-1), m_connected(false), m_connectionAttempts(0), 
      m_lastConnectionAttempt(std::chrono::system_clock::now()) {
    
    // Get the raw data model from the raw data manager singleton
    // This ensures we're using the same data model instance across the application
    m_rawDataModel = raw_data_model::RawDataModelManager::getInstance().getModel(symbol);
    
    // Initialize connection objects
    m_connManager = std::make_unique<connection_manager::ConnectionManager>();
    
    std::cout << "[ModelManager] Created for symbol: " << symbol << std::endl;
}

/**
 * Destructor - ensure all connections are properly closed
 */
ModelManager::~ModelManager() {
    // Disconnect from IBKR API if still connected
    disconnectFromIBKR();
    
    std::cout << "[ModelManager] Destroyed for symbol: " << getSymbol() << std::endl;
}

/**
 * Connect to IBKR API and subscribe to market data for this symbol
 * @return True if connection successful, false otherwise
 */
bool ModelManager::connectToIBKR() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // If already connected, return success
    if (m_connected) {
        return true;
    }
    
    // Check if we've exceeded max connection attempts
    if (m_connectionAttempts >= MAX_CONNECTION_ATTEMPTS) {
        std::cerr << "[ModelManager] Maximum connection attempts (" << MAX_CONNECTION_ATTEMPTS << ") reached for symbol: " 
                  << getSymbol() << ". Giving up." << std::endl;
        return false;
    }
    
    // Check if we should wait before retrying
    auto now = std::chrono::system_clock::now();
    if (m_connectionAttempts > 0) {
        auto timeSinceLastAttempt = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_lastConnectionAttempt).count();
            
        if (timeSinceLastAttempt < CONNECTION_RETRY_DELAY_SECONDS) {
            // Too soon to retry
            return false;
        }
    }
    
    // Increment connection attempt counter and record timestamp
    m_connectionAttempts++;
    m_lastConnectionAttempt = now;
    
    try {
        // Establish connection to IBKR
        std::cout << "[ModelManager] Connection attempt " << m_connectionAttempts << "/" 
                  << MAX_CONNECTION_ATTEMPTS << " for symbol: " << getSymbol() << std::endl;
        
        if (!m_connManager->connect()) {
            std::cerr << "[ModelManager] Failed to connect to IBKR API for symbol: " << getSymbol() << std::endl;
            return false;
        }
        
        // Create API callback that will route data to this ModelManager
        m_apiCallback = std::make_shared<ApiCallback>(m_connManager->getTrader(), *this, getSymbol());
        
        // Get IBKR API client from connection manager and set the callback
        auto client = m_connManager->getTrader().getClient();
        client->setServerLogLevel(5); // Set to most detailed log level
        
        // PAPER TRADING SETUP:
        // Note: Connection to paper trading is handled in connection.cpp which uses port 4002
        // Set market data type to DELAYED_FROZEN_DATA (3) for data outside market hours
        // For live data during market hours, use REALTIME (1)
        // Options: 1 = Live, 2 = Frozen, 3 = Delayed, 4 = Delayed+Frozen
        std::cout << "[ModelManager] Setting market data type for " << getSymbol() << std::endl;
        client->reqMarketDataType(3); // Using delayed data for testing
        
        // Generate a random request ID (to prevent conflicts with other instances)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> distrib(10000, 99999);
        m_requestId = distrib(gen);
        
        // Set the request ID in our callback so it knows which data to process
        m_apiCallback->setRequestId(m_requestId);
        
        // Create a contract for the symbol
        Contract contract;
        contract.symbol = getSymbol();
        contract.secType = "STK";
        contract.currency = "USD";
        contract.exchange = "SMART";
        
        // Subscribe to tick-by-tick data (most granular) 
        client->reqMktData(m_requestId, contract, "", false, false, TagValueListSPtr());
        
        std::cout << "[ModelManager] Connected to IBKR API for symbol: " << getSymbol() 
                  << " with request ID: " << m_requestId << std::endl;
        
        // Reset connection attempts on success
        m_connectionAttempts = 0;
        
        // Mark as connected
        m_connected = true;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[ModelManager] Exception connecting to IBKR for symbol " 
                  << getSymbol() << ": " << e.what() << std::endl;
        return false;
    }
}

/**
 * Disconnect from IBKR API and cancel market data subscription
 */
void ModelManager::disconnectFromIBKR() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_connected) {
        return;
    }
    
    try {
        // Cancel the market data subscription
        if (m_requestId >= 0) {
            auto client = m_connManager->getTrader().getClient();
            client->cancelMktData(m_requestId);
        }
        
        // Disconnect from IBKR
        m_connManager->disconnect();
        
        std::cout << "[ModelManager] Disconnected from IBKR API for symbol: " << getSymbol() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[ModelManager] Exception disconnecting from IBKR for symbol " 
                  << getSymbol() << ": " << e.what() << std::endl;
    }
    
    // Reset connection state
    m_connected = false;
    m_requestId = -1;
    m_apiCallback.reset();
}

/**
 * Check if currently connected to IBKR API
 * @return True if connected, false otherwise
 */
bool ModelManager::isConnected() const {
    return m_connected;
}

/**
 * Get the connection status string
 * @return String describing connection status
 */
std::string ModelManager::getConnectionStatus() const {
    std::stringstream ss;
    
    if (m_connected) {
        ss << "Connected to IBKR API (Request ID: " << m_requestId << ")";
    } else {
        ss << "Not connected to IBKR API";
    }
    
    return ss.str();
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
    // Check if we need to attempt connection
    if (!isConnected()) {
        // Don't attempt to connect if max retries have been reached
        if (m_connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
            connectToIBKR();
        } else if (m_connectionAttempts == MAX_CONNECTION_ATTEMPTS) {
            // Only print this message once when we hit the limit
            std::cerr << "[ModelManager] Max connection attempts reached for " << getSymbol() 
                      << ". Will continue processing without market data." << std::endl;
            m_connectionAttempts++; // Increment to avoid printing this message again
        }
    }
    
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

} // namespace model_manager 