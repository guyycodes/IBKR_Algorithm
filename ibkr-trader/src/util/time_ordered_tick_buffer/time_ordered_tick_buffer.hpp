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
#include "../../models/metrics_model/stock_data_tick.hpp"
#include "../../models/technical_calculator/technical_calculator.hpp"
#include <cstdint>

// Forward declarations
namespace technical_calculator {
    class TechnicalCalculator;
    
    namespace config {
        extern const int ALMA_WINDOW_SIZE;
        extern const double ALMA_SIGMA;
        extern const double ALMA_OFFSET;
    }
}

namespace time_ordered_tick_buffer {

// Configuration constants
static constexpr int64_t DEFAULT_WINDOW_MS = 60'000;  // 1 minute default window , change this to 300'000; for 5 minutes

// Helper struct for indicators
struct TechnicalIndicators {
    double vwap = 0.0;
    double rsi = 0.0;
    double ema9 = 0.0;
    double ema26 = 0.0;
    double alma = 0.0;
    double chaikin = 0.0;

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
        volume += size;
    }
};

class TimeOrderedTickBuffer {
public:
    // Constructor with configurable window size
    TimeOrderedTickBuffer(int64_t windowSizeMs = DEFAULT_WINDOW_MS);
    
    // Destructor
    ~TimeOrderedTickBuffer();
    
    // Add a tick maintaining chronological order
    void addTick(const stock_data_tick::StockData& tick);
    
    // Get most recent technical indicators
    TechnicalIndicators calculateIndicators();
    
private:
    // Map with timestamp as key ensures automatic chronological ordering
    std::map<int64_t, stock_data_tick::StockData> m_orderedTicks;
    
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
    
    // ALMA incremental calculation
    std::vector<double> m_almaWeights;         // Pre-computed ALMA weight vector
    std::vector<double> m_priceRing;           // Ring buffer for last M closes
    size_t m_priceRingHead;                    // Next price slot to overwrite
    size_t m_priceRingCount;                   // Number of valid price slots
    double m_almaDot;                          // Running ALMA dot product
    
    // Technical calculator (using a pointer to avoid circular dependency)
    std::unique_ptr<technical_calculator::TechnicalCalculator> m_calculator;
    
    // Last time we updated candles
    int64_t m_lastCandleUpdateTime;
    
    // How often to update candles (in ms)
    const int64_t m_candleUpdateFrequencyMs;   // Update frequency
    
    // Chaikin Oscillator state for incremental calculation
    double m_runningADL = 0.0;        // Accumulation/Distribution Line
    double m_emaADL_fast = std::numeric_limits<double>::quiet_NaN();  // Fast EMA of ADL
    double m_emaADL_slow = std::numeric_limits<double>::quiet_NaN();  // Slow EMA of ADL
    double m_lastChaikin = 0.0;       // Most recent Chaikin value for quick access
    std::map<int64_t, double> m_candleMFV;  // Track MFV by minute timestamp for rolling window
    
    // RSI state for incremental calculation
    double m_prevClose = std::numeric_limits<double>::quiet_NaN();
    double m_avgGain;
    double m_avgLoss;
    double m_lastRSI;
    static constexpr int RSI_PERIOD = 14;
    int m_rsiWarmupCount;
    
    // Price EMA state for incremental calculation
    double m_emaPriceFast = std::numeric_limits<double>::quiet_NaN();
    double m_emaPriceSlow = std::numeric_limits<double>::quiet_NaN();
    
    // Track the last minute processed to avoid double-counting candles
    int64_t m_lastProcessedMinute = std::numeric_limits<int64_t>::min();
    
    // Private methods
    int64_t getCurrentTimestamp();
    bool shouldUpdateCandles();
    void pruneOldTicks();
    void updateCandles();
    TechnicalIndicators computeIndicatorsFromCandles();
    void updateChaikinForCandle(const Candle& candle, int64_t minuteIndex, bool isFirstTime);
    void updateRSIForCandle(double close);
    
    // Ring buffer and ALMA optimization methods
    void initializeAlmaWeights();
    void addCandleToRing(const Candle& candle);
    void updateAlmaIncremental(double newClose);
    
    // Thread safety
    mutable std::mutex m_mutex;
};

} // namespace time_ordered_tick_buffer

#endif // TIME_ORDERED_TICK_BUFFER_HPP