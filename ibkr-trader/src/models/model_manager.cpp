#include "model_manager.hpp"
#include "model_manager_factory.hpp"
#include "../util/app_state/app_state.hpp"
#include "../models/metrics_model/stock_data_tick.hpp"
#include "../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <random>
#include <thread>
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
    : m_windowSize(windowSize),
      m_windowUnit(windowUnit),
      m_lastPruneTime(std::chrono::system_clock::now()),
      // Initialize with the correct window size from the start
      m_timeOrderedBuffer(windowToMilliseconds()),
      m_ringBufferTradeHandler(&m_timeOrderedBuffer),
      m_requestId(-1),
      m_connected(false),
      m_connectionAttempts(0),
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

    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    std::cout << "[ModelManager constructor] [thread_id: " << threadIdStr.str() << "] " << std::endl;
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
        
        ////////////////////////////////////////////////////////////////
        // Generate a unique client ID for this symbol's connection
        // Use a simple hash of the symbol to create a unique client ID
        std::string symbol = getSymbol();
        int clientId = 0;

        // Create a contract for the symbol
        Contract contract;
        contract.symbol = getSymbol();
        contract.secType = "STK";
        contract.currency = "USD";
        contract.exchange = "SMART";

        std::string genericTicks = "233,232,221"; // Request WAP (field code 14)
        bool snapshot = false;           // Continuous updates instead of snapshot
        bool regulatorySnapshot = false;
        TagValueListSPtr mktDataOptions(new TagValueList());
        ////////////////////////////////////////////////////////////////
        // Simple hash: sum ASCII values and add a constant offset to avoid client ID 0
        for (char c : symbol) {
            clientId += static_cast<int>(c);
        }
        clientId = (clientId % 9000) + 1000; // Ensure clientId is between 1000-9999
        
        std::cout << "[ModelManager] Using client ID: " << clientId << " for symbol: " << getSymbol() << std::endl;
        
        if (!m_connManager->connect(clientId, getSymbol(), contract)) { // pass the contract all the way down model_manager->connection_manager->m_trader->connect(clientId, getSymbol(), contract)-> into the connection.cpp
            std::cerr << "[ModelManager] Failed to connect to IBKR API for symbol: " << getSymbol() << std::endl;
            return false;
        }
        
        // Wait for connection to fully establish
        std::cout << "[ModelManager] Waiting for connection to fully establish..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Check if still connected after wait
        if (!m_connManager->getTrader().isConnected()) {
            std::cerr << "[ModelManager] Connection lost during initialization" << std::endl;
            return false;
        }

        // Set this ModelManager directly on the IBKRTrader
        m_connManager->getTrader().setModelManager(this, getSymbol());
        
        // Get IBKR API client from connection manager
        // this goes through the connection manager all the way down to the connection.hpp
        auto client = m_connManager->getTrader().getClient(); // The IBKR client socket
        
        client->setServerLogLevel(5); // Set to most detailed log level
        
        // PAPER TRADING SETUP:
        // Set market data type to live data (1)
        std::cout << "[ModelManager] Setting market data type for " << getSymbol() << std::endl;

        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        std::cout << "[ModelManager] [thread_id: " << threadIdStr.str() << "] " << std::endl;

        client->reqMarketDataType(1); 
        
        // Generate a random request ID (to prevent conflicts with other instances)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> distrib(10000, 99999);
        m_requestId = distrib(gen);
        
        // Set the request ID directly on the IBKRTrader
        m_connManager->getTrader().setRequestId(m_requestId);
        
        // Make the initial market data request
        // Subscribe to tick-by-tick data (most granular), wait 1 second and then start the data stream
        client->reqMktData(m_requestId, contract, genericTicks, snapshot, regulatorySnapshot, mktDataOptions);
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        m_connManager->getTrader().startDataStream();
        
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
                  << "Pruned " << (sizeBefore - sizeAfter) << " old StockData entries, "
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
void ModelManager::addTick(const stock_data_tick::StockData& tick) {
    // ========== SUCCESSFULLY RECEIVED COMPLETE TICK DATA ==========
    std::cout << "\n[ModelManager] ✅ addTick() CALLED SUCCESSFULLY" << std::endl;
    std::cout << "Symbol: " << tick.symbol << " | Timestamp: " << tick.timestamp << std::endl;
    
    // Display core tick-by-tick data
    std::cout << "TICK-BY-TICK DATA:" << std::endl;
    std::cout << "  Last Price: $" << std::fixed << std::setprecision(4) << tick.last << std::endl;
    std::cout << "  Bid: $" << std::fixed << std::setprecision(4) << tick.bid 
              << " x " << std::fixed << std::setprecision(0) << tick.bidSize << std::endl;
    std::cout << "  Ask: $" << std::fixed << std::setprecision(4) << tick.ask 
              << " x " << std::fixed << std::setprecision(0) << tick.askSize << std::endl;
    std::cout << "  Spread: $" << std::fixed << std::setprecision(4) << (tick.ask - tick.bid) 
              << " (" << std::fixed << std::setprecision(2) << ((tick.ask - tick.bid) / tick.last * 100) << "%)" << std::endl;
    
    // Display OHLC data (if available)
    if (tick.open > 0 || tick.high > 0 || tick.low > 0 || tick.close > 0) {
        std::cout << "OHLC DATA:" << std::endl;
        if (tick.open > 0) std::cout << "  Open: $" << std::fixed << std::setprecision(4) << tick.open << std::endl;
        if (tick.high > 0) std::cout << "  High: $" << std::fixed << std::setprecision(4) << tick.high << std::endl;
        if (tick.low > 0) std::cout << "  Low: $" << std::fixed << std::setprecision(4) << tick.low << std::endl;
        if (tick.close > 0) std::cout << "  Close: $" << std::fixed << std::setprecision(4) << tick.close << std::endl;
    } else {
        std::cout << "OHLC DATA: Not yet available (waiting for first 5-second bar)" << std::endl;
    }
    
    // Display additional market data (if available)
    if (tick.volume > 0 || tick.vwap > 0) {
        std::cout << "MARKET DATA:" << std::endl;
        if (tick.volume > 0) std::cout << "  Volume: " << std::fixed << std::setprecision(0) << tick.volume << " shares" << std::endl;
        if (tick.vwap > 0) std::cout << "  VWAP: $" << std::fixed << std::setprecision(4) << tick.vwap << std::endl;
    }
    
    if (!tick.exchange.empty()) {
        std::cout << "  Exchange: " << tick.exchange << std::endl;
    }

    std::cout << "==================Getting volume from the volume_profile_map======================" << std::endl;
    // Get actual volume from volume profile for this price and create enriched tick
    stock_data_tick::StockData enrichedTick = tick;
    
    // Get volume profile summary and parse total volume
    std::string volumeSummary = m_volumeProfile.get_summary();
    int totalVolume = 0;
    
    // Parse the total volume from the summary string
    // Look for "Total Volume: X shares" pattern
    size_t totalVolumePos = volumeSummary.find("Total Volume: ");
    if (totalVolumePos != std::string::npos) {
        // Move past "Total Volume: "
        size_t numberStart = totalVolumePos + 14;
        size_t sharesPos = volumeSummary.find(" shares", numberStart);
        
        if (sharesPos != std::string::npos) {
            std::string volumeStr = volumeSummary.substr(numberStart, sharesPos - numberStart);
            try {
                totalVolume = std::stoi(volumeStr);
            } catch (const std::exception& e) {
                std::cout << "[ModelManager] Error parsing volume from summary: " << e.what() << std::endl;
                totalVolume = 0;
            }
        }
    }
    
    if (totalVolume > 0) {
        // Use real volume from volume profile
        enrichedTick.volume = totalVolume;
        std::cout << "[ModelManager] Volume updated from profile: " << totalVolume 
                  << " shares (total traded volume)" << std::endl;
    } else {
        // If no volume in profile yet, use the tick volume as-is (this happens early in trading session)
        std::cout << "[ModelManager] No volume in profile yet, using tick volume: " << enrichedTick.volume << std::endl;
    }
    std::cout << "========================================" << std::endl;
    // Calculate derived metrics
    enrichedTick.calculateDerivedMetrics();

    // Add to time-ordered buffer
    m_timeOrderedBuffer.addTick(enrichedTick);
    
    // Get latest technical indicators
    time_ordered_tick_buffer::TechnicalIndicators indicators = m_timeOrderedBuffer.calculateIndicators();
    
    // Enrich the tick with calculated indicators
    // enrichedTick.vwap = indicators.vwap;
    // add the later
    enrichedTick.rsi = indicators.rsi;
    enrichedTick.ema9 = indicators.ema9;
    enrichedTick.ema26 = indicators.ema26;
    enrichedTick.alma = indicators.alma;
    enrichedTick.atr = indicators.atr;
    

    // Get thread ID for logging
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    
    // Get queue size before adding the tick
    size_t queueSizeBefore = m_rawDataModel->getStockQueue()->size();
    
    // Add the tick to the underlying model's queue
    // Validation print for incoming StockData
    std::cout << "\n[ThreadID: " << threadIdStr.str() << "][Symbol: " << getSymbol() << "] StockData Received:" 
              << "\n[ModelManager-Queue] Current queue size before adding tick: " << queueSizeBefore 
              << "\n=========================================" << std::endl;

    // No need to create a new StockData object since we already have one
    // Just pass it directly to the raw data model with the enriched indicators
    
    // Quick trade opportunity check using ring buffer
    if (m_ringBufferTradeHandler.checkForTradeOpportunity(enrichedTick)) {
        std::cout << "[TradeAlert] Opportunity detected for " << getSymbol() 
                  << " at price: " << enrichedTick.last << std::endl;
    }
    
    m_rawDataModel->addTick(enrichedTick);
    
    // Get queue size after adding the tick
    size_t queueSizeAfter = m_rawDataModel->getStockQueue()->size();

    // Print detailed tick information with thread ID
    std::cout << "[ModelManager][VALIDATION][ThreadID: " << threadIdStr.str() << "][Symbol: " << getSymbol() << "] "
              << "\n  Symbol: " << enrichedTick.symbol
              << "\n  Timestamp: " << enrichedTick.timestamp
              << "\n  Exchange: " << (!enrichedTick.exchange.empty() ? enrichedTick.exchange : "-")
              << "\n  Price Data: Last=" << enrichedTick.last << " Bid=" << enrichedTick.bid << " Ask=" << enrichedTick.ask
              << "\n  Size Data: BidSize=" << enrichedTick.bidSize << " AskSize=" << enrichedTick.askSize << " Volume=" << enrichedTick.volume
              << "\n  OHLC: Open=" << enrichedTick.open << " High=" << enrichedTick.high << " Low=" << enrichedTick.low << " Previous_close=" << enrichedTick.close
              << "\n  Mid: " << enrichedTick.mid << " Spread: " << enrichedTick.spread 
              << "\n  Derived Metrics: VWAP=" << enrichedTick.vwap 
              << "\n  Technical Indicators: RSI=" << enrichedTick.rsi
              << " EMA9=" << enrichedTick.ema9 << " EMA26=" << enrichedTick.ema26
              << " ALMA=" << enrichedTick.alma << " ATR=" << enrichedTick.atr
              << "\n[ModelManager-Queue] New queue size after adding tick: " << queueSizeAfter 
              << (queueSizeAfter > queueSizeBefore ? " ✓" : " ✗") << std::endl;

    // Prune old data immediately after adding a new tick
    pruneOldData();
}

/**
 * Add an individual trade to the volume profile
 * This method is called from connection.cpp when tick-by-tick trade data arrives
 * 
 * @param price The trade price
 * @param volume The trade volume
 */
void ModelManager::addTradeTick(double price, int volume) {
    if (volume <= 0) {
        return; // Skip invalid volume
    }
    
    // Add the trade to the volume profile
    // use the class instantiated in the constructor
    m_volumeProfile.add_transaction(price, volume);

    
    // Log the trade addition for debugging
    std::cout << "[VolumeProfile][Symbol: " << getSymbol() << "] "
              << "Added trade: " << volume << " shares at $" 
              << std::fixed << std::setprecision(4) << price << std::endl;
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
std::vector<stock_data_tick::StockData> ModelManager::getTicksInWindow() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();
    
    // Calculate cutoff time by subtracting the window size from current time
    auto cutoffMs = nowMs - windowToMilliseconds();
    
    // Create a vector to hold the filtered ticks
    std::vector<stock_data_tick::StockData> windowTicks;
    
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
    
    // STK_Q already handles chronological ordering and time filtering
    // Just use removeOlderThan to get the data we need, then rebuild the vector
    // First, let STK_Q prune old data
    queue->removeOlderThan(cutoffMs);
    
    // Now get all remaining data (which is already in our time window)
    stk_q::STK_Q_Data data;
    windowTicks.reserve(queue->size()); // Reserve space for efficiency
    
    // Pop all data to convert to StockData format, then push back
    size_t originalSize = queue->size();
    std::vector<stk_q::STK_Q_Data> tempData;
    tempData.reserve(originalSize);
    
    // Extract all data
    while (queue->pop(data)) {
        tempData.push_back(data);
        
        // Convert queue data to StockData with all the rich data we now store
        stock_data_tick::StockData tick;
        tick.symbol = getSymbol();
        tick.timestamp = data.time;
        tick.exchange = data.exchange;
        
        // Use the rich data from our expanded STK_Q_Data
        tick.bid = data.bid;
        tick.ask = data.ask;
        tick.last = data.last;
        tick.bidSize = data.bidSize;
        tick.askSize = data.askSize;
        tick.lastSize = data.lastSize;
        tick.volume = data.volume;
        
        // OHLC data
        tick.open = data.open;
        tick.high = data.high;
        tick.low = data.low;
        tick.close = data.close;
        
        // Derived metrics
        tick.mid = data.mid;
        tick.spread = data.spread;
        tick.spreadPercent = data.spreadPercent;
        tick.vwap = data.vwap;
        tick.imbalance = data.imbalance;
        
        // Technical indicators
        tick.rsi = data.rsi;
        tick.ema9 = data.ema9;
        tick.ema26 = data.ema26;
        tick.alma = data.alma;
        tick.atr = data.atr;

        // Add to vector
        windowTicks.push_back(tick);
    }
    
    // Push all data back to queue
    for (auto& item : tempData) {
        queue->push(std::move(item));
    }
    
    std::cout << "[getTicksInWindow][Symbol: " << getSymbol() << "] Returning " 
              << windowTicks.size() << " ticks in time window" << std::endl;
    
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
const stock_data_tick::StockData* ModelManager::getLatestTick() const {
    static stock_data_tick::StockData latestTick;
    
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


} // namespace model_manager 