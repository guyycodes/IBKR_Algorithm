// ───────────────────────────────────────────────────────────────────────────────
//  model_manager.cpp
// ───────────────────────────────────────────────────────────────────────────────

#include "models/model_manager.hpp"
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <sstream>
#include <numeric>
#include <algorithm>
#include <chrono>
#include <ctime>

using namespace std::chrono_literals;
namespace model_manager {

// ────────────────────────────────────────────────────────────────────────────────
// Thread logging utility - same as InputManager for consistency
// ────────────────────────────────────────────────────────────────────────────────
void logThreadRole(const std::string& role, const std::string& action) {
    auto tid = std::this_thread::get_id();
    std::ostringstream ss;
    ss << tid;
    std::cout << "[" << role << ":" << ss.str() << "] " << action << std::endl;
}

// ────────────────────────────────────────────────────────────────────────────────
//  ctor / dtor
// ────────────────────────────────────────────────────────────────────────────────
ModelManager::ModelManager(std::string sym,
                           std::size_t windowSize,
                           TimeWindowUnit unit)
    : m_symbol(std::move(sym)),
      m_windowSize(windowSize),
      m_windowUnit(unit),
      m_lastPrune(std::chrono::steady_clock::now())
{
    m_debug_logging = true;
    m_rawModel   = std::make_unique<raw_data_model::RawDataModel>(m_symbol);
    m_volProfile = std::make_unique<volume_profile_map::VolumeProfileMap>(0.05);
    
    // CRITICAL: Convert user-friendly time units (5 MINUTES, 2 HOURS, etc.) to milliseconds
    // This enables per-instance time window configuration instead of using a single default
    // Example: ModelManager("AAPL", 5, MINUTES) → 300,000ms window
    //          ModelManager("TSLA", 2, HOURS)   → 7,200,000ms window
    m_toBuffer   = std::make_unique<time_ordered_tick_buffer::TimeOrderedTickBuffer>(windowToDuration().count());
    
    m_rbHandler  = std::make_unique<ring_buffer_trade_handler::RingBufferTradeHandler>(
                    *m_toBuffer, *m_volProfile, *m_rawModel);
    m_connMgr    = std::make_unique<connection_manager::ConnectionManager>();
    logThreadRole("ModelManager", "Creating ModelManager for " + m_symbol);
}

ModelManager::~ModelManager() { 
    disconnectFromIBKR(); 
    logThreadRole("ModelManager", "ModelManager destroyed for " + m_symbol);
}

// ────────────────────────────────────────────────────────────────────────────────
//  IBKR connectivity (no blocking loops; retry handled by caller)
// ────────────────────────────────────────────────────────────────────────────────
bool ModelManager::connectToIBKR(){
    std::scoped_lock lg{m_mx};
    if (m_connected) return true;
    if (m_connectionAttempts >= MAX_CONN_ATTEMPTS) return false;

    if (m_connectionAttempts>0 &&
       (std::chrono::steady_clock::now()-m_lastAttempt)<std::chrono::seconds(RETRY_DELAY_SECONDS))
        return false;                                   // back-off

    ++m_connectionAttempts;
    m_lastAttempt = std::chrono::steady_clock::now();

    // build IBKR contract
    Contract c; 
    c.symbol = getSymbol(); 
    c.secType = "STK"; 
    c.exchange = "SMART"; 
    c.currency = "USD";
    
    std::random_device rd; 
    std::mt19937 gen(rd());
    int clientId = (std::accumulate(c.symbol.begin(),c.symbol.end(),0)+1000)%9000+1000;

    logThreadRole("ModelManager", "Connecting to IBKR for " + getSymbol());
    
    if (!m_connMgr->connect(clientId, getSymbol(), c)) {
        logThreadRole("ModelManager", "Connection failed for " + getSymbol());
        return false;
    }
    
    m_reqId = std::uniform_int_distribution<int>(10'000,99'999)(gen);
    auto& trader = m_connMgr->trader();
    auto cli = trader.getClient();
    if (cli) {
        cli->setServerLogLevel(5); 
        cli->reqMarketDataType(1);
    }
    trader.setModelManager(this, getSymbol());
    
    // Make the main market data request (like legacy code) using m_reqId
    if (cli) {
        std::cout << "[ModelManager] Making main market data request for " << getSymbol() 
                  << " with requestId=" << m_reqId << "\n";
        cli->reqMktData(m_reqId, c, "233,232,221", false, false, {});
    }
    
    // Wait 1 second after reqMktData (like legacy code)
    std::cout << "[ModelManager] Waiting after market data request...\n";
    std::this_thread::sleep_for(1s);
    
    // Now start supplementary data streams
    trader.startDataStream(getSymbol());
    
    std::this_thread::sleep_for(2s);
    if (!trader.isConnected()) { 
        m_connMgr->disconnect(); 
        return false; 
    }

    m_connectionAttempts = 0;
    m_connected = true;
    logThreadRole("ModelManager", "IBKR connected for " + getSymbol());
    return true;
}

void ModelManager::disconnectFromIBKR(){
    std::scoped_lock lg{m_mx};
    if (!m_connected) return;
    
    logThreadRole("ModelManager", "Disconnecting from IBKR for " + getSymbol());
    
    if (m_reqId >= 0 && m_connMgr->isConnected()) {
        auto& trader = m_connMgr->trader();
        auto cli = trader.getClient();
        if (cli) {
            cli->cancelMktData(m_reqId);
        }
    }
    m_connMgr->disconnect();
    m_connected = false; 
    m_reqId = -1;
}

std::string ModelManager::getConnectionStatus() const {
    return m_connected ? "CONNECTED" : "NOT CONNECTED";
}

// ────────────────────────────────────────────────────────────────────────────────
//  time-window helpers
// ────────────────────────────────────────────────────────────────────────────────
// PURPOSE: Convert user-friendly time specifications to milliseconds for TimeOrderedTickBuffer
// This allows flexible time window configuration per ModelManager instance:
//   - 30 SECONDS for scalping strategies  
//   - 5 MINUTES for short-term analysis
//   - 2 HOURS for swing trading
//   - etc.
std::chrono::milliseconds ModelManager::windowToDuration() const {
    using namespace std::chrono;
    switch (m_windowUnit){
        case TimeWindowUnit::SECONDS: return milliseconds(m_windowSize*1'000);
        case TimeWindowUnit::MINUTES: return milliseconds(m_windowSize*60'000);
        case TimeWindowUnit::HOURS  : return milliseconds(m_windowSize*3'600'000);
    }
    return milliseconds(0);  // Fallback (should never happen with valid enum)
}

void ModelManager::pruneOldData(){
    
    auto now = std::chrono::steady_clock::now();  // ① use steady_clock to match m_lastPrune
    if (now - m_lastPrune < 5s) return;        // prune at most every 5 s
    m_lastPrune = now;
    
    // Calculate cutoff time based on window duration
    // cutoff = (system_clock_now − window) in **milliseconds since epoch**
    auto system_now = std::chrono::system_clock::now();
    const auto cutoffMs = std::chrono::duration_cast<std::chrono::milliseconds>(system_now.time_since_epoch())
                        - windowToDuration();       // <- already returns milliseconds
    
    // Get current queue size for logging
    size_t sizeBefore = m_rawModel->queueSize();
    
    // Use STK_Q's efficient removeOlderThan method via RawDataModel interface
    m_rawModel->removeOlderThan(static_cast<uint64_t>(cutoffMs.count()));
    
    if (m_debug_logging && sizeBefore > 0) {
        size_t sizeAfter = m_rawModel->queueSize();
        if (sizeBefore > sizeAfter) {
            std::cout << "[ModelManager] Pruned " << (sizeBefore - sizeAfter) 
                      << " old ticks for " << getSymbol() 
                      << " (cutoff: " << cutoffMs.count() << "ms)" << std::endl;
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────────
//  queue processing for ML model - batch processing
// ────────────────────────────────────────────────────────────────────────────────
std::vector<stock_data_tick::StockData> ModelManager::batchQueueData(std::size_t maxBatch){
    // Log only on first call to avoid spam
    static thread_local bool first_process = true;
    if (first_process) {
        logThreadRole("ModelManager", "Starting queue processing for " + getSymbol());
        first_process = false;
    }
    
    // Use RawDataModel's batch processing method with conversion
    std::vector<stock_data_tick::StockData> stockTicks = m_rawModel->popNextTicks(maxBatch);
    
    if (!stockTicks.empty()) {
        if (m_debug_logging) {
            std::cout << "[ModelManager] Processed " << stockTicks.size() 
                      << " queued ticks for " << getSymbol() << std::endl;
        }
        pruneOldData();
    }
    
    return stockTicks;
}

// ────────────────────────────────────────────────────────────────────────────────
//  queue processing (called in AppState worker loop)
// ────────────────────────────────────────────────────────────────────────────────
// std::size_t ModelManager::processQueueData(){
//     // Log only on first call to avoid spam
//     static thread_local bool first_process = true;
//     if (first_process) {
//         logThreadRole("ModelManager", "Starting queue management for " + getSymbol());
//         first_process = false;
//     }
    
//     // Check queue size - prune if it's getting large to prevent unbounded memory
//     size_t currentSize = m_rawModel->queueSize();
//     static constexpr size_t PRUNE_THRESHOLD = 100000;  // Prune when queue gets large
    
//     size_t processed = 0;
//     if (currentSize > PRUNE_THRESHOLD) {
//         size_t sizeBefore = currentSize;
//         pruneOldData();  // Uses time window logic and updates m_lastPrune
//     }
    
//     return processed;
// }

// ────────────────────────────────────────────────────────────────────────────────
//  data ingestion (called by connection pipeline)
// ────────────────────────────────────────────────────────────────────────────────
void ModelManager::addTick(const stock_data_tick::StockData& t){

    // Work with a copy for enrichment
    stock_data_tick::StockData enrichedTick = t;

    // Calculate derived metrics in-place
    enrichedTick.calculateDerivedMetrics();
    // time is in miliseconds

    // add a print out that will confirm the data that we are geting.
    // std::cout << "==================== COMPLETE STOCKDATA VALIDATION ====================" << std::endl;
    // // Core identification
    // std::cout << "CORE DATA:" << std::endl;
    // std::cout << "  Symbol: " << enrichedTick.symbol << std::endl;
    // std::cout << "  Timestamp: " << enrichedTick.timestamp << std::endl;
    // std::cout << "  Exchange: " << (enrichedTick.exchange.empty() ? "EMPTY" : enrichedTick.exchange) << std::endl;
    
    // // Core market data
    // std::cout << "MARKET DATA:" << std::endl;
    // std::cout << "  Last: $" << std::fixed << std::setprecision(4) << enrichedTick.last << std::endl;
    // std::cout << "  Bid: $" << std::fixed << std::setprecision(4) << enrichedTick.bid 
    //           << " x " << enrichedTick.bidSize << std::endl;
    // std::cout << "  Ask: $" << std::fixed << std::setprecision(4) << enrichedTick.ask 
    //           << " x " << enrichedTick.askSize << std::endl;
    // // std::cout << "  LastSize: " << enrichedTick.lastSize << std::endl;
    // // std::cout << "  Volume (Total Market): " << std::fixed << std::setprecision(2) << t.volume << "M" << std::endl;
    // std::cout << "  MidPoint: " << enrichedTick.midPoint << std::endl;
    // std::cout << "  Spread: " << enrichedTick.spread << std::endl;
    // std::cout << "  SpreadPercent: " << enrichedTick.spreadPercent << "%" << std::endl;
    // std::cout << "  PriceChange: " << enrichedTick.priceChange << std::endl;
    // std::cout << "  VWAP: $" << std::fixed << std::setprecision(4) << enrichedTick.vwap << std::endl;
    
    // // OHLC data
    // std::cout << "OHLC DATA:" << std::endl;
    // std::cout << "  Open: " << enrichedTick.open << std::endl;
    // std::cout << "  High: " << enrichedTick.high << std::endl;
    // std::cout << "  Low: " << enrichedTick.low << std::endl;
    // std::cout << "  Close: " << enrichedTick.close << std::endl;
    // std::cout << "  BarRange: " << enrichedTick.barRange << std::endl;
    
    // std::cout << "=======================================================================" << std::endl;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Efficient volume calculation - direct access instead of string parsing
    int totalVolume = m_volProfile->getTotalVolume();
    
    if (totalVolume > 0) {
        // std::cout << "Volume from profile: " << totalVolume << " shares" << std::endl;
        enrichedTick.volume = totalVolume;
        if (m_debug_logging) {
            std::cout << "[ModelManager] Volume from profile: " << totalVolume << " shares" << std::endl;
        }
    }

    // Technical analysis pipeline (time_ordered_tick_buffer)
    m_toBuffer->addTick(enrichedTick);
    
    time_ordered_tick_buffer::TechnicalIndicators indicators = m_toBuffer->calculateIndicators();

    // Apply technical indicators in-place
    enrichedTick.rsi = indicators.rsi;
    enrichedTick.ema9 = indicators.ema9;
    enrichedTick.ema26 = indicators.ema26;
    enrichedTick.alma = indicators.alma;
    enrichedTick.atr = indicators.atr;
    // will do the macd later
    // enrichedTick.macd = indicators.macd;

    size_t queueSizeBefore = m_rawModel->queueSize();

    // Trade opportunity detection dont call this here, would start thousands of threads (commented out - monitoring now starts in constructor)
    // m_rbHandler->evaluate(enrichedTick)

    //////////////////////////////////////////////////////////////////////////////////////////////
    // Store in STK_Q and prune old data
    m_rawModel->addTick(enrichedTick);
    
    size_t queueSizeAfter = m_rawModel->queueSize();
    
    // Print detailed tick information with thread ID (throttled to every 5 seconds)
    static auto lastPrintTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPrintTime).count() >= 5) {
        lastPrintTime = now;
        
        std::ostringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
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
                  << (queueSizeAfter > queueSizeBefore ? " ✓" : " ✗") << "\n" << std::endl;
    }   
                   
    pruneOldData();
}

/// adds public tape tradetick to the volume profile map
void ModelManager::addTradeTick(double p,int v){ 
    // this is used inside connection.cpp
    // std::cout << "[ModelManager] STUB: Adding trade tick for " << getSymbol() << " price=" << p << " vol=" << v << '\n';
    if (v>0) m_volProfile->add_transaction(p,v); 
}

// ────────────────────────────────────────────────────────────────────────────────
//  misc accessors
// ────────────────────────────────────────────────────────────────────────────────
// used to get the symbol for the api
std::string ModelManager::getSymbol() const { return m_rawModel->symbol(); }

// used to get the params for the api
const raw_data_model::TradingParams& ModelManager::getParams() const { return m_rawModel->params(); }

// used to get the tick count from the stk_q
std::size_t ModelManager::getTickCount() const { 
    return m_rawModel->queueSize(); 
}

// used to get the latest tick for the api  
const stock_data_tick::StockData* ModelManager::getLatestTick() const {
    static stock_data_tick::StockData tmp;
    return m_rawModel->latestTick(tmp) ? &tmp : nullptr;
}

// used to get the ticks in the time window for the api
std::vector<stock_data_tick::StockData> ModelManager::getTicksInWindow() const {
    // Calculate cutoff time based on window duration
    std::uint64_t cutoff = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()
                           ).count() - windowToDuration().count();
    
    return m_rawModel->getTicksInTimeWindow(cutoff);
}

// used to get the time window for the api
std::pair<std::size_t,TimeWindowUnit> ModelManager::getTimeWindow() const {
    return {m_windowSize,m_windowUnit};
}

// used to set the time window for the api
void ModelManager::setTimeWindow(std::size_t sz,TimeWindowUnit u){
    m_windowSize=sz; m_windowUnit=u;
    // m_toBuffer.setWindow(windowToDuration());
    // pruneOldData();
}

// used to clear the ticks for the api
void ModelManager::clearTicks(){ 
    m_rawModel->clear(); 
}

// used to initialize the model from the json for the api
bool ModelManager::initFromJson(const nlohmann::json& js) {
    auto result = m_rawModel->initialise(js);
    return !result; // error_code converts to bool (true if error), so we negate it
}

} // namespace model_manager
