#include "model_manager.hpp"
#include "model_manager_factory.hpp"
#include "../util/app_state/app_state.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <random>

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
    : m_windowSize(windowSize), m_windowUnit(windowUnit), m_lastPruneTime(std::chrono::system_clock::now()),
      m_requestId(-1), m_connected(false), m_connectionAttempts(0), 
      m_lastConnectionAttempt(std::chrono::system_clock::now()) {
    
    // Get the raw data model from the raw data manager singleton
    // This ensures we're using the same data model instance across the application
    m_rawDataModel = raw_data_model::RawDataModelManager::getInstance().getModel(symbol);
    
    // Initialize connection objects
    m_connManager = std::make_unique<connection_manager::ConnectionManager>();
    
    std::cout << "[ModelManager] Created for symbol: " << symbol 
              << " with time window: " << m_windowSize << " " 
              << (m_windowUnit == TimeWindowUnit::SECONDS ? "seconds" : 
                 (m_windowUnit == TimeWindowUnit::MINUTES ? "minutes" : "hours"))
              << std::endl;
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
        
        // Generate a unique client ID for this symbol's connection
        // Use a simple hash of the symbol to create a unique client ID
        std::string symbol = getSymbol();
        int clientId = 0;
        
        // Simple hash: sum ASCII values and add a constant offset to avoid client ID 0
        for (char c : symbol) {
            clientId += static_cast<int>(c);
        }
        clientId = (clientId % 9000) + 1000; // Ensure clientId is between 1000-9999
        
        std::cout << "[ModelManager] Using client ID: " << clientId << " for symbol: " << getSymbol() << std::endl;
        
        if (!m_connManager->connect(clientId)) {
            std::cerr << "[ModelManager] Failed to connect to IBKR API for symbol: " << getSymbol() << std::endl;
            return false;
        }
        
        // Wait for connection to fully establish
        std::cout << "[ModelManager] Waiting for connection to fully establish..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Check if still connected after wait
        if (!m_connManager->getTrader().isConnected()) {
            std::cerr << "[ModelManager] Connection lost during initialization" << std::endl;
            return false;
        }

        // Set this ModelManager directly on the IBKRTrader
        m_connManager->getTrader().setModelManager(this, getSymbol());
        
        // Get IBKR API client from connection manager
        auto client = m_connManager->getTrader().getClient();
        client->setServerLogLevel(5); // Set to most detailed log level
        
        // PAPER TRADING SETUP:
        // Set market data type to DELAYED_FROZEN_DATA (3) for data outside market hours
        std::cout << "[ModelManager] Setting market data type for " << getSymbol() << std::endl;
        client->reqMarketDataType(3); // Using delayed data for testing
        
        // Generate a random request ID (to prevent conflicts with other instances)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> distrib(10000, 99999);
        m_requestId = distrib(gen);
        
        // Set the request ID directly on the IBKRTrader
        m_connManager->getTrader().setRequestId(m_requestId);
        
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
    
    // Get size before pruning
    size_t sizeBefore = queue->size();
    
    // Prune the queue by removing items older than the cutoff time
    queue->removeOlderThan(cutoffMs);
    
    // Get size after pruning
    size_t sizeAfter = queue->size();
    
    // Log pruning results if items were removed
    if (sizeBefore != sizeAfter) {
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        
        std::cout << "[Prune][ThreadID: " << threadIdStr.str() << "][Symbol: " << getSymbol() << "] "
                  << "Pruned " << (sizeBefore - sizeAfter) << " old ticks, "
                  << "new queue size: " << sizeAfter << std::endl;
    }
    
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
    // Get thread ID for logging
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    
    // Get queue size before adding the tick
    size_t queueSizeBefore = m_rawDataModel->getStockQueue()->size();
    
    // Format volume appropriately (scientific notation for very large values)
    std::stringstream volumeStr;
    if (tick.volume > 1.0e10) {
        volumeStr << std::scientific << tick.volume << std::fixed;
    } else {
        volumeStr << tick.volume;
    }
    
    // Add the tick to the underlying model's queue
    std::cout << "[Queue][ThreadID: " << threadIdStr.str() << "][Symbol: " << getSymbol() << "] "
              << "Adding tick - Price: " << tick.price 
              << ", Volume: " << volumeStr.str() 
              << ", Current queue size: " << queueSizeBefore << std::endl;
              
    m_rawDataModel->addTick(tick);
    
    // Get queue size after adding the tick
    size_t queueSizeAfter = m_rawDataModel->getStockQueue()->size();
    
    // Print detailed tick information with thread ID
    std::cout << "[Queue][ThreadID: " << threadIdStr.str() << "][Symbol: " << getSymbol() << "] "
              << "Added tick - Price: " << tick.price 
              << ", Volume: " << volumeStr.str() 
              << ", New queue size: " << queueSizeAfter 
              << (queueSizeAfter > queueSizeBefore ? " ✓" : " ✗") << std::endl;
    
    // Prune old data immediately after adding a new tick
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
    if (!queue) {
        std::cout << "[getTicksInWindow][Symbol: " << getSymbol() << "] Error: Queue is null" << std::endl;
        return windowTicks;
    }
    
    // Log queue status
    size_t queueSize = queue->size();
    std::cout << "[getTicksInWindow][Symbol: " << getSymbol() << "] Queue size: " << queueSize 
              << ", Time window: " << m_windowSize << " " 
              << (m_windowUnit == TimeWindowUnit::SECONDS ? "seconds" : 
                 (m_windowUnit == TimeWindowUnit::MINUTES ? "minutes" : "hours"))
              << ", Cutoff time: " << cutoffMs << std::endl;
    
    if (queueSize == 0) {
        std::cout << "[getTicksInWindow][Symbol: " << getSymbol() << "] Queue is empty" << std::endl;
        return windowTicks;
    }
    
    // Create a temporary queue to prevent modifying the original
    std::queue<stk_q::STK_Q_Data> tempQueue;
    stk_q::STK_Q_Data data;
    
    // Get a copy of the queue
    windowTicks.reserve(queueSize); // Reserve space for efficiency
    
    // Track included and excluded items
    int includedCount = 0;
    int excludedCount = 0;
    
    // We'll create a temporary copy of the queue by popping and re-adding
    for (size_t i = 0; i < queueSize; i++) {
        if (queue->pop(data)) {
            // Check if data is within the time window
            bool isWithinWindow = static_cast<uint64_t>(data.time) >= cutoffMs;
            
            if (isWithinWindow) {
                // Convert queue data back to MarketDataTick
                raw_data_model::MarketDataTick tick;
                tick.price = data.price;
                tick.volume = data.size;
                tick.timestamp = data.time;
                
                // Add to vector
                windowTicks.push_back(tick);
                includedCount++;
                
                // Log the data item we're including (limit to first 5 to avoid spam)
                if (includedCount <= 5) {
                    std::cout << "[getTicksInWindow][Symbol: " << getSymbol() << "] Including tick: Price=" 
                            << data.price << ", Time=" << data.time 
                            << " (within cutoff " << cutoffMs << ")" << std::endl;
                }
            } else {
                excludedCount++;
                
                // Log the first few items we're filtering out
                if (excludedCount <= 5) {
                    std::cout << "[getTicksInWindow][Symbol: " << getSymbol() << "] Excluding tick: Price=" 
                            << data.price << ", Time=" << data.time 
                            << " (before cutoff " << cutoffMs << ")" << std::endl;
                }
            }
            
            // Re-add to the original queue
            queue->push(data);
        }
    }
    
    std::cout << "[getTicksInWindow][Symbol: " << getSymbol() << "] Finished processing. Included: " 
              << includedCount << ", Excluded: " << excludedCount 
              << ", Returning vector size: " << windowTicks.size() << std::endl;
    
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
        // Process this tick data according to trading rules
        // For now, just count it as processed
        processedCount++;
    }
    
    // Run calculations using TechnicalCalculator if we have enough data
    if (queue->size() > 10) {  // A minimal threshold, adjust as needed
        try {
            // Get ticks in the current time window
            auto ticks = getTicksInWindow();
            
            // Skip if we don't have enough data
            if (ticks.size() < 10) {
                return processedCount;
            }
            
            // Extract prices and volumes for analysis
            std::vector<double> prices;
            std::vector<double> volumes;
            std::vector<double> highs;
            std::vector<double> lows;
            std::vector<double> closes;
            
            for (const auto& tick : ticks) {
                // In our simplified model, we use the same price for high/low/close
                double price = tick.price;
                prices.push_back(price);
                highs.push_back(price);
                lows.push_back(price);
                closes.push_back(price);
                volumes.push_back(tick.volume);
            }
            
            // 1. Calculate VWAP
            double vwap = m_calculator.calculateVWAP(prices, volumes);
            
            // 2. Check if this is a valid trading candidate (initial filter)
            bool isValid = m_calculator.isValidTradingCandidate(prices, volumes, vwap);
            
            // 3. Calculate advanced indicators
            // These will be useful for ongoing monitoring and exit decisions
            double chaikinValue = 0.0;
            double almaValue = 0.0;
            
            if (isValid && prices.size() >= 10) {
                // Calculate Chaikin Oscillator (helps detect money flow)
                chaikinValue = m_calculator.calculateChaikinOscillator(
                    highs, lows, closes, volumes, 3, 10);
                
                // Calculate ALMA (smooth moving average for trend)
                almaValue = m_calculator.calculateALMA(closes, 9, 0.85, 6.0);
                
                // In debug mode, we'll print advanced insights
                #ifdef DEBUG
                double atr = m_calculator.calculateATR(highs, lows, closes, 3);
                
                std::cout << "[TechnicalAnalysis][Symbol: " << getSymbol() << "] "
                          << "Latest price: " << prices.back() 
                          << ", VWAP: " << vwap 
                          << ", Valid candidate: " << (isValid ? "YES" : "NO") << std::endl;
                
                std::cout << "[AdvancedMetrics][Symbol: " << getSymbol() << "] "
                          << "ATR: " << atr
                          << ", Chaikin: " << chaikinValue 
                          << ", ALMA: " << almaValue
                          << ", Price vs ALMA: " << (prices.back() > almaValue ? "ABOVE" : "BELOW")
                          << ", Money Flow: " << (chaikinValue > 0 ? "POSITIVE" : "NEGATIVE") << std::endl;
                #endif
                
                // Entry indicator example (not triggering actual trades here, just logging)
                bool strongMoneyFlow = (chaikinValue > 0);
                bool aboveAlma = (prices.back() > almaValue);
                
                if (isValid && strongMoneyFlow && aboveAlma) {
                    std::cout << "[SIGNAL][Symbol: " << getSymbol() << "] "
                              << "*** BULL SIGNAL ACTIVE *** "
                              << "Price: " << prices.back() 
                              << ", Chaikin: " << chaikinValue << std::endl;
                }
            }
        } catch (const std::exception& e) {
            // Just log the error and continue - don't let calculation errors
            // disrupt the main data processing pipeline
            std::cerr << "[Calculator][Symbol: " << getSymbol() << "] "
                      << "Error calculating metrics: " << e.what() << std::endl;
        }
    }
    
    // Get thread ID for logging
    if (processedCount > 0) {
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        
        std::cout << "[Thread][ThreadID: " << threadIdStr.str() << "][Symbol: " << getSymbol() << "] "
                  << "Processed " << processedCount << " queue items" << std::endl;
    }
    
    return processedCount;
}

} // namespace model_manager 