// TimeOrderedTickBuffer.hpp

#ifndef TIME_ORDERED_TICK_BUFFER_HPP
#define TIME_ORDERED_TICK_BUFFER_HPP

#include <map>
#include <vector>
#include <chrono>
#include <memory>
#include "../../models/metrics_model/stock_data_tick.hpp"
#include "../../models/technical_calculator/technical_calculator.hpp"

// Forward declarations
namespace technical_calculator {
    class TechnicalCalculator;
    
    namespace config {
        extern const int ALMA_WINDOW_SIZE;
        extern const double ALMA_SIGMA;
        extern const double ALMA_OFFSET;
        extern const int CHAIKIN_FAST_PERIOD;
        extern const int CHAIKIN_SLOW_PERIOD;
    }
}

namespace time_ordered_tick_buffer {

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
    TimeOrderedTickBuffer(int64_t windowSizeMs = 60000);
    
    // Destructor
    ~TimeOrderedTickBuffer();
    
    // Add a tick maintaining chronological order
    void addTick(const stock_data_tick::StockData& tick);
    
    // Get most recent technical indicators
    TechnicalIndicators calculateIndicators();
    
private:
    // Map with timestamp as key ensures automatic chronological ordering
    std::map<int64_t, stock_data_tick::StockData> m_orderedTicks;
    
    // Window size in milliseconds (e.g., 1 minute = 60000ms)
    const int64_t m_windowSizeMs;
    
    // Aggregated candles at different timeframes
    std::vector<Candle> m_oneMinCandles;
    
    // Technical calculator (using a pointer to avoid circular dependency)
    std::unique_ptr<technical_calculator::TechnicalCalculator> m_calculator;
    
    // Last time we updated candles
    int64_t m_lastCandleUpdateTime = 0;
    
    // How often to update candles (in ms)
    const int64_t m_candleUpdateFrequencyMs = 1000; // Update every second
    
    // Private methods
    void pruneOldTicks();
    void updateCandles();
    bool shouldUpdateCandles();
    int64_t getCurrentTimestamp();
    TechnicalIndicators computeIndicatorsFromCandles();
};

} // namespace time_ordered_tick_buffer

#endif // TIME_ORDERED_TICK_BUFFER_HPP