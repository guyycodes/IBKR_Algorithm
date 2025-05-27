#include "time_ordered_tick_buffer.hpp"
#include <algorithm>
#include <iostream>

namespace time_ordered_tick_buffer {

// Constructor
TimeOrderedTickBuffer::TimeOrderedTickBuffer(int64_t windowSizeMs)
    : m_windowSizeMs(windowSizeMs),
      m_lastCandleUpdateTime(0),
      m_calculator(std::make_unique<technical_calculator::TechnicalCalculator>())
{
    // Nothing else to initialize
}

// Destructor - no need to explicitly delete m_calculator as unique_ptr handles it
TimeOrderedTickBuffer::~TimeOrderedTickBuffer() = default;

// Add a tick maintaining chronological order
void TimeOrderedTickBuffer::addTick(const stock_data_tick::StockData& tick) {
    // Insert or replace at timestamp position
    m_orderedTicks[tick.timestamp] = tick;
    
    // Remove old ticks outside the window
    pruneOldTicks();
    
    // Aggregate into candles when appropriate
    if (shouldUpdateCandles()) {
        updateCandles();
    }
}

// Get most recent technical indicators
TechnicalIndicators TimeOrderedTickBuffer::calculateIndicators() {
    // Use candles to calculate indicators
    return computeIndicatorsFromCandles();
}

// Private methods

// Get current timestamp in milliseconds
int64_t TimeOrderedTickBuffer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto value = now_ms.time_since_epoch();
    return value.count();
}

// Determine if it's time to update candles
bool TimeOrderedTickBuffer::shouldUpdateCandles() {
    int64_t currentTime = getCurrentTimestamp();
    
    // Update candles if it's been long enough since the last update
    if (currentTime - m_lastCandleUpdateTime > m_candleUpdateFrequencyMs) {
        m_lastCandleUpdateTime = currentTime;
        return true;
    }
    return false;
}

// Remove ticks older than the window
void TimeOrderedTickBuffer::pruneOldTicks() {
    // Get current time
    int64_t currentTime = getCurrentTimestamp();
    
    // Remove ticks older than the window
    auto it = m_orderedTicks.begin();
    while (it != m_orderedTicks.end()) {
        if (currentTime - it->first > m_windowSizeMs) {
            it = m_orderedTicks.erase(it);
        } else {
            // Map is ordered, so once we hit a recent tick, all following ticks are recent
            break; 
        }
    }
}

// Update candles from tick data
void TimeOrderedTickBuffer::updateCandles() {
    // Clear existing candles
    m_oneMinCandles.clear();
    
    // Group ticks into 1-minute buckets
    std::map<int64_t, TemporaryCandle> minuteBuckets;
    
    for (const auto& [timestamp, tick] : m_orderedTicks) {
        int64_t minuteIndex = timestamp / 60000; // Convert ms to minute index
        
        // Update the candle for this minute
        minuteBuckets[minuteIndex].update(tick.last, tick.lastSize);
    }
    
    // Convert buckets to candles
    for (const auto& [minuteIndex, tempCandle] : minuteBuckets) {
        if (!tempCandle.isEmpty()) {
            m_oneMinCandles.push_back(Candle(
                tempCandle.open, tempCandle.high, tempCandle.low, 
                tempCandle.close, tempCandle.volume, minuteIndex * 60000
            ));
        }
    }
    
    // Sort candles by time
    std::sort(m_oneMinCandles.begin(), m_oneMinCandles.end(),
             [](const Candle& a, const Candle& b) {
                 return a.timestamp < b.timestamp;
             });
}

// Compute technical indicators from candles
TechnicalIndicators TimeOrderedTickBuffer::computeIndicatorsFromCandles() {
    TechnicalIndicators indicators;
    
    // Check if we have enough candles
    if (m_oneMinCandles.size() < 26) {  // Need at least 26 for slowest EMA
        return indicators;
    }
    
    // Extract price arrays for calculations
    std::vector<double> closes, highs, lows, volumes;
    for (const auto& candle : m_oneMinCandles) {
        closes.push_back(candle.close);
        highs.push_back(candle.high);
        lows.push_back(candle.low);
        volumes.push_back(candle.volume);
    }
    
    // Calculate VWAP
    indicators.vwap = m_calculator->calculateVWAP(closes, volumes);
    
    // Calculate EMAs
    indicators.ema9 = m_calculator->calculateEMA(closes, 9);
    indicators.ema26 = m_calculator->calculateEMA(closes, 26);
    
    // Calculate ALMA
    indicators.alma = m_calculator->calculateALMA(
        closes, 
        technical_calculator::config::ALMA_WINDOW_SIZE, 
        technical_calculator::config::ALMA_SIGMA, 
        technical_calculator::config::ALMA_OFFSET);
    
    // Calculate RSI
    indicators.rsi = m_calculator->calculateRSI(closes, 14);
    
    // Calculate Chaikin Oscillator
    indicators.chaikin = m_calculator->calculateChaikinOscillator(
        highs, lows, closes, volumes, 
        technical_calculator::config::CHAIKIN_FAST_PERIOD, 
        technical_calculator::config::CHAIKIN_SLOW_PERIOD);
    
    return indicators;
}

} // namespace time_ordered_tick_buffer 