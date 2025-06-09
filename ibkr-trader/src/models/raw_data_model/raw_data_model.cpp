#include "raw_data_model.hpp"
#include <chrono>
#include <iostream>
#include "../util/stk_q/stk_q.hpp"
#include "../../global_config.hpp"

namespace raw_data_model {

using Clock = std::chrono::system_clock;

/*───────────────────────────────────────────────────────────────────────────────
 * TradingParams::from_json                                                     */
bool TradingParams::from_json(const nlohmann::json& js) noexcept
{
    try {
        lots               = js.value("lots",               0);
        margin             = js.value("margin",             0.0);
        stopLossPct        = js.value("stopLoss",           0.0);
        maxTrades          = js.value("maxTrades",          0);
        lossThresholdPct   = js.value("lossThreshold",      0.0);
        winThresholdPct    = js.value("winThreshold",       0.0);
        minWinRatePct      = js.value("minWinRate",         0.0);
        maxHoldSeconds     = js.value("maxHoldSeconds",     0);
        return true;
    }
    catch (...) { return false; }
}

/*───────────────────────────────────────────────────────────────────────────────
 * ctor                                                                         */
RawDataModel::RawDataModel(std::string sym, int intervalMs)
    : m_symbol(std::move(sym))
{
    try {
        m_queue = std::make_unique<stk_q::STK_Q>();
        
        // STK_Q uses hardcoded 500ms filtering, intervalMs parameter is ignored
        std::clog << "[RawDataModel] " << m_symbol << " created (using STK_Q default 500ms filter interval)" << '\n';
        
        // warm up data collection if global flag is set
        if (isDataCollectionEnabled()) {
            if (write_to_file()) {
                std::clog << "[RawDataModel] Data collection auto‑started for " << m_symbol << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[RawDataModel] Error creating RawDataModel for " << m_symbol 
                  << ": " << e.what() << std::endl;
        throw; // Re-throw to prevent incomplete object construction
    }
}

/*───────────────────────────────────────────────────────────────────────────────
 * dtor                                                                         */
RawDataModel::~RawDataModel()
{
    // Ensure training thread is properly terminated
    stop_write_to_file();
    std::clog << "[RawDataModel] " << m_symbol << " destroyed" << '\n';
}

/*───────────────────────────────────────────────────────────────────────────────
 * addTick – single entry-point for new market ticks                            */
void RawDataModel::addTick(const stock_data_tick::StockData& tick)
{
    // 1. Convert outside any lock
    stk_q::STK_Q_Data q = convertToQueue(tick);

    if (shouldWriteToFile()) {
        std::lock_guard<std::mutex> g{m_trainingMutex};
        m_trainingQueue.push(std::move(q));
        m_trainingCV.notify_one();
    } else {
        m_queue->push(std::move(q));
    }
}

bool RawDataModel::shouldWriteToFile() const
{
    return m_isTrainingActive.load(std::memory_order_acquire);
}

/*───────────────────────────────────────────────────────────────────────────────
 * popNextTick – consumer side (used by ModelManager worker)                    */
bool RawDataModel::popNextTick(stk_q::STK_Q_Data& out)
{
    // STK_Q methods are already thread-safe
    return m_queue->pop(out);
}

/*───────────────────────────────────────────────────────────────────────────────
 * popNextTicks – batch consumer (returns vector of StockData)                  */
std::vector<stock_data_tick::StockData> RawDataModel::popNextTicks(std::size_t maxBatch)
{
    std::vector<stock_data_tick::StockData> result;
    result.reserve(maxBatch);
    
    stk_q::STK_Q_Data queueData;
    for (std::size_t i = 0; i < maxBatch && m_queue->pop(queueData); ++i) {
        result.push_back(convertToStockData(queueData));
    }
    
    return result;
}

/*───────────────────────────────────────────────────────────────────────────────
 * latestTick – lightweight peek of newest element                              */
bool RawDataModel::latestTick(stock_data_tick::StockData& out) const
{
    stk_q::STK_Q_Data qd;
    if (!m_queue->peekLatest(qd)) return false;

    // spin-lock around trivial copy to avoid heavy mutex
    while (m_spinLock.test_and_set(std::memory_order_acquire));
    out.symbol    = m_symbol;
    out.timestamp = static_cast<stock_data_tick::timestamp_t>(qd.time);
    out.exchange  = qd.exchange;
    
    out.bid       = qd.bid;
    out.ask       = qd.ask;
    out.last      = qd.last;
    out.bidSize   = qd.bidSize;
    out.askSize   = qd.askSize;
    out.lastSize  = qd.lastSize;
    out.volume    = qd.volume;

    // OHLC data
    out.open      = qd.open;
    out.high      = qd.high;
    out.low       = qd.low;
    out.close     = qd.close;

    // Derived metrics
    out.mid       = qd.mid;
    out.spread    = qd.spread;
    out.spreadPercent = qd.spreadPercent;
    out.vwap      = qd.vwap;
    out.imbalance = qd.imbalance;
    
    // Technical indicators
    out.rsi       = qd.rsi;
    out.ema9      = qd.ema9;
    out.ema26     = qd.ema26;
    out.alma      = qd.alma;
    out.atr       = qd.atr;

    m_spinLock.clear(std::memory_order_release);
    return true;
}

/*───────────────────────────────────────────────────────────────────────────────
 * queueSize / clear                                                            */
std::size_t RawDataModel::queueSize() const noexcept 
{ 
    return m_queue->size(); 
}

void RawDataModel::clear() noexcept      
{ 
    m_queue->clear(); 
}

void RawDataModel::removeOlderThan(uint64_t cutoffTimeMs) noexcept
{
    m_queue->removeOlderThan(cutoffTimeMs);
}

/*───────────────────────────────────────────────────────────────────────────────
 * initialise – load TradingParams from JSON                                    */
std::error_code RawDataModel::initialise(const nlohmann::json& js)
{
    if (!js.contains(m_symbol))               // key missing
        return std::make_error_code(std::errc::invalid_argument);

    const auto& symNode = js.at(m_symbol);
    if (!symNode.contains("params"))
        return std::make_error_code(std::errc::bad_message);

    if (!m_params.from_json(symNode.at("params")))
        return std::make_error_code(std::errc::bad_message);

    return {};
}

//───────────────────────────────────────────────────────────────────────────────
// convertToQueue – helper (StockData → STK_Q_Data)


stk_q::STK_Q_Data RawDataModel::convertToQueue(const stock_data_tick::StockData& s)
{
    stk_q::STK_Q_Data q;
    q.symbol   = s.symbol;
    q.time     = s.timestamp; // requires ms
    q.bid      = s.bid;
    q.ask      = s.ask;
    q.last     = s.last;
    q.bidSize  = static_cast<int>(s.bidSize);
    q.askSize  = static_cast<int>(s.askSize);
    q.volume   = s.volume;
    q.vwap     = s.vwap;

    // derived metrics that STK_Q consumers might need
    q.mid            = s.midPoint;
    q.spread         = s.spread;
    q.spreadPercent  = s.spreadPercent;
    q.imbalance      = s.imbalance;
    q.rsi            = s.rsi;
    q.ema9           = s.ema9;
    q.ema26          = s.ema26;
    q.alma           = s.alma;
    q.atr            = s.atr;

    // backwards compatibility
    q.price          = q.last;
    q.size           = q.volume;
    return q;
}

// convert stk_q::STK_Q_Data item back into stock_data_tick::StockData
stock_data_tick::StockData RawDataModel::convertToStockData(const stk_q::STK_Q_Data& q)
{
    stock_data_tick::StockData s;
    s.symbol = q.symbol;
    s.timestamp = q.time;
    s.exchange = q.exchange;
    
    // Core market data
    s.bid = q.bid;
    s.ask = q.ask;
    s.last = q.last;
    s.bidSize = static_cast<stock_data_tick::volume_t>(q.bidSize);
    s.askSize = static_cast<stock_data_tick::volume_t>(q.askSize);
    s.lastSize = static_cast<stock_data_tick::volume_t>(q.lastSize);
    s.volume = static_cast<stock_data_tick::volume_t>(q.volume);
    
    // OHLC data
    s.open = q.open;
    s.high = q.high;
    s.low = q.low;
    s.close = q.close;
    
    // Derived metrics
    s.mid = q.mid;
    s.spread = q.spread;
    s.spreadPercent = q.spreadPercent;
    s.vwap = q.vwap;
    s.imbalance = q.imbalance;
    
    // Technical indicators
    s.rsi = q.rsi;
    s.ema9 = q.ema9;
    s.ema26 = q.ema26;
    s.alma = q.alma;
    s.atr = q.atr;
    
    return s;
}

/*───────────────────────────────────────────────────────────────────────────────
 * getTicksInTimeWindow – get ticks newer than cutoff time                      */
std::vector<stock_data_tick::StockData> RawDataModel::getTicksInTimeWindow(std::uint64_t cutoffTimeMs) const
{
    std::vector<stock_data_tick::StockData> result;
    
    // Get all data from STK_Q (non-destructive)
    auto allQueueData = m_queue->getAllData();
    
    // Convert and filter by time if cutoffTimeMs is specified
    for (const auto& qData : allQueueData) {
        if (cutoffTimeMs == 0 || qData.time >= cutoffTimeMs) {
            result.push_back(convertToStockData(qData));
        }
    }
    
    return result;
}

/*───────────────────────────────────────────────────────────────────────────────
 * initialize_training_collector – create and start training data collection    */
bool RawDataModel::initialize_training_collector()
{
    try {
        std::lock_guard<std::mutex> lg{m_trainingMutex};
        
        // Check if already running
        if (m_isTrainingActive.load()) {
            std::clog << "[RawDataModel] Training data collection already active for " << m_symbol << std::endl;
            return false;
        }
        
        // Create training data collector with optimized settings
        m_trainingCollector = std::make_unique<TrainingDataCollector>(m_symbol);
        
        // Reset control flags BEFORE starting thread
        m_shouldStopTraining = false;
        
        // Start the training data collection thread FIRST (may throw)
        m_trainingThread = std::make_unique<std::thread>(&RawDataModel::trainingDataThreadFunc, this);
        
        // Only set active flag AFTER successful thread creation
        m_isTrainingActive = true;
        
        std::clog << "[RawDataModel] Training data collector initialized and thread started for " << m_symbol << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[RawDataModel] Failed to initialize training data collector for " << m_symbol 
                  << ": " << e.what() << std::endl;
        m_trainingCollector.reset();
        m_isTrainingActive = false;
        return false;
    }
}

/*───────────────────────────────────────────────────────────────────────────────
 * write_to_file – start training data collection (if not already running)      */
bool RawDataModel::write_to_file()
{
    // Simply delegate to initialize_training_collector if not already running
    if (m_isTrainingActive.load()) {
        std::clog << "[RawDataModel] Training data collection already active for " << m_symbol << std::endl;
        return false;
    }
    
    return initialize_training_collector();
}

/*───────────────────────────────────────────────────────────────────────────────
 * stop_write_to_file – stop training data collection thread                    */
void RawDataModel::stop_write_to_file()
{
    try {
            {
                std::unique_lock<std::mutex> lock{m_trainingMutex};
                
                if (!m_isTrainingActive.load()) {
                    return; // Not running
                }
                
                // Signal thread to stop
                m_shouldStopTraining = true;
                m_trainingCV.notify_all();
            }  // Lock released here automatically
        
        // Wait for thread to finish (outside lock to avoid deadlock)
        if (m_trainingThread && m_trainingThread->joinable()) {
            m_trainingThread->join();
            m_trainingThread.reset();
        }
        
        // Clean up collector
        if (m_trainingCollector) {
            m_trainingCollector->stop();
            m_trainingCollector.reset();
        }
        
        m_isTrainingActive = false;
        std::clog << "[RawDataModel] Training data collection stopped for " << m_symbol << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[RawDataModel] Error stopping training data collection for " << m_symbol 
                  << ": " << e.what() << std::endl;
        m_isTrainingActive = false;
    }
}

/*───────────────────────────────────────────────────────────────────────────────
 * is_writing_to_file – check if training data collection is active             */
bool RawDataModel::is_writing_to_file() const
{
    return m_isTrainingActive.load();
}

/*───────────────────────────────────────────────────────────────────────────────
 * trainingDataThreadFunc – training data collection thread function            */
void RawDataModel::trainingDataThreadFunc()
{
    try {
        std::clog << "[RawDataModel] Training data thread started for " << m_symbol << std::endl;
        
        if (m_trainingCollector) {
            m_trainingCollector->start();
        }
        
        std::unique_lock<std::mutex> lock{m_trainingMutex};
        
        while (!m_shouldStopTraining.load()) {
            // Wait for data or stop signal (proper producer-consumer pattern)
            m_trainingCV.wait(lock, [this] { 
                return !m_trainingQueue.empty() || m_shouldStopTraining.load(); 
            });
            
            // Process all available data in the queue
            while (!m_trainingQueue.empty() && !m_shouldStopTraining.load()) {
                stk_q::STK_Q_Data qData = m_trainingQueue.front();
                m_trainingQueue.pop();
                
                // Release lock while processing (to avoid blocking addTick)
                lock.unlock();
                
                if (m_trainingCollector && m_trainingCollector->isRunning()) {
                    m_trainingCollector->processTick(qData);
                }
                
                // Reacquire lock for next iteration
                lock.lock();
            }
        }
        
        // Clear active flag on normal thread completion
        m_isTrainingActive = false;
        std::clog << "[RawDataModel] Training data thread finished for " << m_symbol << std::endl;
    } catch (const std::exception& e) {
        // Clear active flag on exception to prevent queue buildup
        m_isTrainingActive = false;
        std::cerr << "[RawDataModel] Error in training data thread for " << m_symbol 
                  << ": " << e.what() << std::endl;
    }
}

} // namespace raw_data_model
