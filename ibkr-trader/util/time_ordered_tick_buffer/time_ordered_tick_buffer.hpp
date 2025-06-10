// TimeOrderedTickBuffer.hpp

#ifndef TIME_ORDERED_TICK_BUFFER_HPP
#define TIME_ORDERED_TICK_BUFFER_HPP

#include <map>
#include <vector>
#include <chrono>
#include <memory>
#include <limits>
#include <algorithm>
#include <mutex>
#include <atomic>
#include "models/stock_data_tick/stock_data_tick.hpp"
#include <cstdint>

// Forward declaration
namespace ring_buffer_trade_handler {
    class RingBufferTradeHandler;
}

namespace time_ordered_tick_buffer {

// Configuration constants, the model_manager_factory creates a model_manager that sets this value, if none is set, we default to 600'000ms (10 minutes)
// DEFAULT VALUE: Used only when TimeOrderedTickBuffer() constructor called without parameters
// Most instances get their window size from ModelManager::windowToDuration() instead
static constexpr int64_t DEFAULT_WINDOW_MS = 600'000;  // 10 minute default window , change this to 300'000; for 5 minutes

// Helper struct for indicators
struct TechnicalIndicators {
    double vwap = 0.0;
    double rsi = 0.0;
    double ema9 = 0.0;
    double ema26 = 0.0;
    double alma = 0.0;
    double atr = 0.0;
    // double chaikin = 0.0;  // Commented out temporarily

    // NEW: readiness flags for warm-up status
    bool ema9Ready = false;
    bool ema26Ready = false;
    bool almaReady = false;

    bool isValid() const {
        // Check if we have valid indicator values
        return vwap > 0.0 && ema9 > 0.0 && ema26 > 0.0;
    }
};

// Candle structure for aggregating tick data
struct Candle {
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    int64_t timestamp = 0;

    Candle() = default;
    Candle(double o, double h, double l, double c, double v, int64_t ts = 0)
        : open(o), high(h), low(l), close(c), volume(v), timestamp(ts) {}
};

// Temporary candle for building buckets
struct TemporaryCandle {
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    bool empty = true;

    bool isEmpty() const { return empty; }

    void update(double price, double size) {
        if (empty) {
            open = high = low = close = price;
            empty = false;
        } else {
            high = std::max(high, price);
            low = std::min(low, price);
            close = price;
        }
        volume = size;
    }
};

// ----------------------------------------------------------------------
// Lightweight, read‑only copy of the three rings for the monitor thread
// ----------------------------------------------------------------------
struct MonitorSnapshot {
    std::vector<TemporaryCandle> minuteRing;
    std::vector<int64_t>         minuteIdx;

    std::vector<Candle>          candleRing;
    size_t                       candleHead = 0;
    size_t                       candleCount = 0;

    std::vector<double>          priceRing;
    size_t                       priceHead = 0;
    size_t                       priceCount = 0;
};

class TimeOrderedTickBuffer {
public:
    // Constructor with configurable window size
    TimeOrderedTickBuffer(int64_t windowSizeMs = DEFAULT_WINDOW_MS);
    
    // Destructor
    ~TimeOrderedTickBuffer();
    
    // Add a tick maintaining chronological order
    void addTick(const stock_data_tick::StockData& tick);
    
    // Get most recent technical indicators (delegates to calculator)
    TechnicalIndicators calculateIndicators();
    
    // Set the calculator for technical indicators
    void setCalculator(ring_buffer_trade_handler::RingBufferTradeHandler* calculator);
    
    // Ultra-low latency ring buffer access for trade handlers
    const std::vector<TemporaryCandle>& getMinuteRing() const { return m_minuteRing; }
    const std::vector<int64_t>& getMinuteIndices() const { return m_minuteIndices; }
    const std::vector<Candle>& getCandleRing() const { return m_candleRing; }
    const std::vector<double>& getPriceRing() const { return m_priceRing; }
    /// All raw ticks in chronological order – **duplicates allowed**
    const std::multimap<int64_t, stock_data_tick::StockData>& getOrderedTicks() const { return m_orderedTicks; }
    size_t getWindowMinutes() const { return m_windowMinutes; }
    int64_t getWindowSizeMs() const { return m_windowSizeMs; }
    
    // Ring buffer state for ultra-low latency access // FOR FEED THREAD ONLY
    size_t getCandleRingHead() const { return m_candleRingHead; }
    size_t getCandleRingCount() const { return m_candleRingCount; }
    size_t getPriceRingHead() const { return m_priceRingHead; }
    size_t getPriceRingCount() const { return m_priceRingCount; }
    
    // Track the last minute processed to avoid double-counting candles
    int64_t getLastProcessedMinute() const { return m_lastProcessedMinute; }
    void setLastProcessedMinute(int64_t minute) { m_lastProcessedMinute = minute; }
    
    // ------------------------------------------
    // Snapshot access for RingBufferMonitor
    // ------------------------------------------
    std::shared_ptr<const MonitorSnapshot> getSnapshot() const
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_monitorSnapshot;
    }
    
private:
    /// Multimap keeps full market‑tape granularity (duplicates per ms)
    std::multimap<int64_t, stock_data_tick::StockData> m_orderedTicks;
    
    // Window size in milliseconds (e.g., DEFAULT_WINDOW_MS = 1 minute)
    const int64_t m_windowSizeMs;
    
    // Fixed-size ring buffers for O(1) operations
    size_t m_windowMinutes;                    // Number of minutes in window
    std::vector<Candle> m_candleRing;          // Ring buffer for candles
    size_t m_candleRingHead;                   // Next slot to overwrite
    size_t m_candleRingCount;                  // Number of valid slots
    
    // Ring buffer for minute buckets (instead of std::map)
    std::vector<TemporaryCandle> m_minuteRing; // Ring buffer for minute aggregation
    std::vector<int64_t> m_minuteIndices;      // Track which minute each slot represents
    
    // ALMA incremental calculation (only ring buffer, weights moved to calculator)
    std::vector<double> m_priceRing;           // Ring buffer for last M closes
    size_t m_priceRingHead;                    // Next price slot to overwrite
    size_t m_priceRingCount;                   // Number of valid price slots
    
    // Last time we updated candles
    int64_t m_lastCandleUpdateTime;
    
    // How often to update candles (in ms)
    const int64_t m_candleUpdateFrequencyMs;   // Update frequency
    
    // Track the last minute processed to avoid double-counting candles
    int64_t m_lastProcessedMinute = std::numeric_limits<int64_t>::min();
    
    // ===== INCREMENTAL CANDLE ENGINE =====
    int64_t m_workingMinute = -1;          // minuteIndex currently being built
    TemporaryCandle m_workingCandle;       // mutable candle for that minute
    
    // Reference to calculator (will be set externally)
    ring_buffer_trade_handler::RingBufferTradeHandler* m_calculator = nullptr;
    
    // ---- one‑writer (feed) / many‑reader (monitor) snapshot ----
    std::shared_ptr<MonitorSnapshot> m_monitorSnapshot;
    mutable std::mutex m_snapshotMutex;
    
    // Private methods
    int64_t getCurrentTimestamp();
    bool shouldUpdateCandles();
    void pruneOldTicks();
    void updateCandles();
    void addCandleToRing(const Candle& candle);
    void updatePriceRing(double newClose);
    
    // ===== INCREMENTAL CANDLE ENGINE METHODS =====
    void finaliseWorkingCandle();
    void handleOutOfOrderTick(int64_t minuteIdx, const stock_data_tick::StockData& tick);
    
    // ===== MONITOR SNAPSHOT METHODS =====
    void publishMonitorSnapshotUnlocked();
    
    // Thread safety
    mutable std::mutex m_mutex;
};

} // namespace time_ordered_tick_buffer

#endif // TIME_ORDERED_TICK_BUFFER_HPP