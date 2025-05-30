#include "time_ordered_tick_buffer.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <limits>    // for quiet_NaN
#include <cmath> 
#include <cstdint>
namespace time_ordered_tick_buffer {

// Chaikin Oscillator Configuration
static constexpr int    FAST_PERIOD        = 3;                        // Fast EMA period (3 candles)
static constexpr int    SLOW_PERIOD        = 10;                       // Slow EMA period (10 candles)
static constexpr int    FAST_PERIOD_MS     = FAST_PERIOD * 60 * 1000;  // 3 minutes in milliseconds
static constexpr int    SLOW_PERIOD_MS     = SLOW_PERIOD * 60 * 1000;  // 10 minutes in milliseconds
static constexpr double ALPHA_FAST         = 2.0 / (FAST_PERIOD + 1);  // Fast EMA smoothing factor
static constexpr double ALPHA_SLOW         = 2.0 / (SLOW_PERIOD + 1);  // Slow EMA smoothing factor

// Time Conversion Constants
static constexpr int64_t MS_PER_MINUTE     = 60 * 1000;                // Milliseconds per minute

/**
 * ========================================================================
 * TimeOrderedTickBuffer - Advanced Technical Analysis Engine
 * ========================================================================
 * 
 * PURPOSE:
 * This class is the heart of our technical analysis system. It transforms
 * noisy, real-time tick data into clean, analyzable market data structures
 * and calculates sophisticated technical indicators for trading decisions.
 * 
 * KEY RESPONSIBILITIES:
 * 1. CHRONOLOGICAL ORDERING: Maintains all incoming ticks in perfect time order
 * 2. CANDLE AGGREGATION: Converts chaotic tick data into smooth 1-minute candles
 * 3. TECHNICAL CALCULATIONS: Computes VWAP, EMA, RSI, ALMA, Chaikin indicators
 * 4. MEMORY MANAGEMENT: Automatically prunes old data to prevent memory bloat
 * 5. REAL-TIME UPDATES: Provides fresh indicators as market data flows in
 * 
 * WHY THIS MATTERS:
 * - Raw tick data is too noisy for reliable trading signals
 * - Technical indicators need historical context to be meaningful
 * - Candles provide the standard format that traders worldwide understand
 * - Memory management prevents system crashes during long trading sessions
 * 
 * FLOW:
 * Tick Data → Time Ordering → Candle Aggregation → Technical Analysis → Trading Signals
 */

// Constructor - Initialize the buffer with a specific time window
TimeOrderedTickBuffer::TimeOrderedTickBuffer(int64_t windowSizeMs)
    : m_windowSizeMs(windowSizeMs),
      m_lastCandleUpdateTime(0),
      m_calculator(std::make_unique<technical_calculator::TechnicalCalculator>())
{
    std::cout << "[TimeOrderedTickBuffer] Initialized with " << (windowSizeMs / 1000) 
              << " second time window" << std::endl;
}

// Destructor - unique_ptr automatically cleans up m_calculator
TimeOrderedTickBuffer::~TimeOrderedTickBuffer() = default;

/**
 * ========================================================================
 * addTick() - The Primary Data Ingestion Method
 * ========================================================================
 * 
 * This is where all market data enters our technical analysis pipeline.
 * Each tick represents a real-time market event (price change, trade, etc.)
 * 
 * CRITICAL OPERATIONS:
 * 1. Insert tick at correct chronological position (handles out-of-order data)
 * 2. Remove old ticks that fall outside our analysis window
 * 3. Trigger candle updates when sufficient new data has arrived
 * 
 * WHY CHRONOLOGICAL ORDER MATTERS:
 * - Technical indicators depend on accurate time sequences
 * - Out-of-order data can create false signals
 * - Historical analysis requires precise timing
 * 
 * PERFORMANCE NOTE:
 * Using std::map ensures O(log n) insertion and automatic ordering
 */
void TimeOrderedTickBuffer::addTick(const stock_data_tick::StockData& tick) {
    // Insert tick at correct chronological position (handles out-of-order data)
    m_orderedTicks[tick.timestamp] = tick;
    
    // Log the addition for debugging (limit spam in production)
    if (m_orderedTicks.size() % 100 == 0) {  // Log every 100th tick
        std::cout << "[TimeOrderedTickBuffer] Buffer now contains " 
                  << m_orderedTicks.size() << " chronologically ordered ticks" << std::endl;
    }
    
    // Remove ticks that have aged out of our analysis window
    // This prevents unlimited memory growth during long trading sessions
    pruneOldTicks();
    
    // Check if we should update our candle aggregations
    // We don't update on every tick (too expensive), only periodically
    if (shouldUpdateCandles()) {
        updateCandles();
    }
}

/**
 * ========================================================================
 * calculateIndicators() - The Technical Analysis Engine
 * ========================================================================
 * 
 * This method returns the latest calculated technical indicators.
 * These indicators are what your trading algorithms will use to make
 * buy/sell decisions.
 * 
 * INDICATORS PROVIDED:
 * - VWAP: Volume-Weighted Average Price (institutional benchmark)
 * - EMA9/26: Fast and slow exponential moving averages (trend detection)
 * - RSI: Relative Strength Index (momentum/overbought-oversold)
 * - ALMA: Arnaud Legoux Moving Average (advanced trend smoothing)
 * - Chaikin: Money flow indicator (buying vs selling pressure)
 * 
 * USAGE IN TRADING:
 * - Compare current price to VWAP for institutional edge
 * - EMA crossovers signal trend changes
 * - RSI above 70 = overbought, below 30 = oversold
 * - ALMA provides smooth trend direction
 * - Chaikin positive = money flowing in, negative = flowing out
 */
TechnicalIndicators TimeOrderedTickBuffer::calculateIndicators() {
    // Use our pre-aggregated candles to calculate all indicators
    // This is much more efficient than calculating from raw ticks
    return computeIndicatorsFromCandles();
}

// ========================================================================
// PRIVATE HELPER METHODS
// ========================================================================

/**
 * getCurrentTimestamp() - System Time Reference
 * 
 * Provides consistent millisecond-precision timestamps across the system.
 * Used for window calculations and candle timing.
 */
int64_t TimeOrderedTickBuffer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto value = now_ms.time_since_epoch();
    return value.count();
}

bool TimeOrderedTickBuffer::shouldUpdateCandles() {
    int64_t currentTime = getCurrentTimestamp();
    
    // Update candles if it's been long enough since the last update
    // This prevents excessive computation while keeping data fresh
    if (currentTime - m_lastCandleUpdateTime > m_candleUpdateFrequencyMs) {
        m_lastCandleUpdateTime = currentTime;
        std::cout << "[TimeOrderedTickBuffer] Triggering candle update - " 
                  << m_orderedTicks.size() << " ticks to process" << std::endl;
        return true;
    }
    return false;
}

void TimeOrderedTickBuffer::pruneOldTicks() {
    // Get current time for window calculation
    int64_t currentTime = getCurrentTimestamp();
    int64_t cutoffTime = currentTime - m_windowSizeMs;
    
    // Track how many ticks we remove for logging
    size_t removedCount = 0;
    size_t originalSize = m_orderedTicks.size();
    
    // Remove ticks older than our analysis window
    auto it = m_orderedTicks.begin();
    while (it != m_orderedTicks.end()) {
        if (it->first < cutoffTime) {  // Timestamp is the key
            it = m_orderedTicks.erase(it);
            removedCount++;
        } else {
            break; // Map is ordered, so we can stop here
        }
    }
    
    // Log pruning activity for monitoring
    if (removedCount > 0) {
        std::cout << "[TimeOrderedTickBuffer] Pruned " << removedCount 
                  << " old ticks, buffer now has " << m_orderedTicks.size() 
                  << " ticks spanning " << (m_windowSizeMs / 1000) << " seconds" << std::endl;
    }
}

void TimeOrderedTickBuffer::updateCandles() {
    // Clear previous candle data and create buckets for aggregation
    // Note: Don't clear m_oneMinCandles here - we'll prune old ones instead
    m_minuteBuckets.clear();  // Reuse existing map to avoid allocations
    
    std::cout << "[TimeOrderedTickBuffer] Aggregating " << m_orderedTicks.size() 
              << " ticks into 1-minute candles..." << std::endl;
    
    // Group ticks by minute and accumulate OHLCV data
    for (const auto& [timestamp, tick] : m_orderedTicks) {
        // Convert millisecond timestamp to minute index
        // Example: 1640995200000ms → minute 27349920
        int64_t minuteIndex = timestamp / MS_PER_MINUTE;
        
        // Update the candle for this minute with the tick data
        // TemporaryCandle::update() handles OHLC logic automatically
        m_minuteBuckets[minuteIndex].update(tick.last, tick.volume);
    }
    
    // Calculate window boundaries once for all pruning operations
    int64_t currentTime = getCurrentTimestamp();
    int64_t windowStart = (currentTime - m_windowSizeMs) / MS_PER_MINUTE;
    
    // Prune old candles from m_oneMinCandles to match our time window
    auto candleIt = m_oneMinCandles.begin();
    while (candleIt != m_oneMinCandles.end()) {
        int64_t candleMinute = candleIt->timestamp / MS_PER_MINUTE;
        if (candleMinute < windowStart) {
            candleIt = m_oneMinCandles.erase(candleIt);
        } else {
            break; // Candles are chronological, so we can stop here
        }
    }
    
    // Bump m_lastProcessedMinute when the window slides to prevent re-processing
    // old buckets that are now outside our time window
    if (m_lastProcessedMinute < windowStart - 1) {
        m_lastProcessedMinute = windowStart - 1;
    }
    
    // Maintain rolling ADL by removing old candles' MFV contributions
    // Remove MFV contributions from candles that aged out of window
    auto mfvIt = m_candleMFV.begin();
    while (mfvIt != m_candleMFV.end()) {
        if (mfvIt->first < windowStart) {
            m_runningADL -= mfvIt->second;  // Subtract old MFV
            mfvIt = m_candleMFV.erase(mfvIt);
        } else {
            break; // Map is ordered, so we can stop here
        }
    }
    
    // Clamp ADL after subtractions to prevent numerical drift
    // Note: Allow negative ADL as it represents selling pressure vs buying pressure
    // If your strategy requires non-negative ADL, change to: std::max(0.0, m_runningADL)
    if (!std::isfinite(m_runningADL)) {
        m_runningADL = 0.0;  // Reset to neutral if NaN/infinity detected
    }
    
    // Handle first-time initialization
    bool isFirstTime = std::isnan(m_emaADL_fast);
    
    // Only process NEW candles that haven't been processed before
    std::vector<Candle> newCandles;
    for (auto& [minuteIndex, tempCandle] : m_minuteBuckets) {
        // Skip candles we've already processed
        if (minuteIndex <= m_lastProcessedMinute) continue;
        if (tempCandle.isEmpty()) continue;
        
        Candle candle(
            tempCandle.open,           // First price of the minute
            tempCandle.high,           // Highest price of the minute
            tempCandle.low,            // Lowest price of the minute
            tempCandle.close,          // Last price of the minute
            tempCandle.volume,         // Total volume for the minute
            minuteIndex * MS_PER_MINUTE        // Convert back to millisecond timestamp
        );
        
        newCandles.push_back(candle);
    }
    
    // Sort new candles by timestamp to ensure chronological processing
    std::sort(newCandles.begin(), newCandles.end(),
              [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    
    // Process each new candle exactly once
    for (const auto& candle : newCandles) {
        // Update technical indicators with this new candle
        updateChaikinForCandle(candle, candle.timestamp / MS_PER_MINUTE, isFirstTime);
        updateRSIForCandle(candle.close);
        
        // Add to our candle collection
        m_oneMinCandles.push_back(candle);
        
        // Track that we've processed this minute
        m_lastProcessedMinute = candle.timestamp / MS_PER_MINUTE;
        
        // After first candle, no longer first time
        isFirstTime = false;
    }
    
    std::cout << "[TimeOrderedTickBuffer] Created " << m_oneMinCandles.size() 
              << " chronologically ordered 1-minute candles" << std::endl;
}

/**
 * computeIndicatorsFromCandles() - Advanced Technical Analysis
 * 
 * This is the culmination of our data processing pipeline. We take clean,
 * aggregated candle data and compute sophisticated technical indicators
 * that professional traders rely on for market analysis.
 * 
 * INDICATOR CALCULATIONS:
 * 
 * 1. VWAP (Volume-Weighted Average Price):
 *    - Shows the average price weighted by volume
 *    - Institutional traders use this as a benchmark
 *    - Price above VWAP = bullish, below = bearish
 * 
 * 2. EMA 9/26 (Exponential Moving Averages):
 *    - Fast (9) and slow (26) trend indicators
 *    - When fast > slow = uptrend, fast < slow = downtrend
 *    - Crossovers generate buy/sell signals
 * 
 * 3. ALMA (Arnaud Legoux Moving Average):
 *    - Advanced trend indicator with reduced lag
 *    - More responsive than traditional moving averages
 *    - Better for fast-moving markets
 * 
 * 4. RSI (Relative Strength Index):
 *    - Momentum oscillator (0-100 scale)
 *    - Above 70 = overbought (potential sell)
 *    - Below 30 = oversold (potential buy)
 * 
 * 5. Chaikin Oscillator:
 *    - Money flow indicator
 *    - Positive = buying pressure, negative = selling pressure
 *    - Helps confirm price movements with volume analysis
 * 
 * DATA REQUIREMENTS:
 * We need at least 26 candles for the slowest indicator (EMA26).
 * Without sufficient data, indicators would be unreliable.
 */
TechnicalIndicators TimeOrderedTickBuffer::computeIndicatorsFromCandles() {
    TechnicalIndicators indicators;
    
    // Verify we have sufficient historical data for reliable calculations

    // 26 candles = 26 minutes of data minimum for EMA26
    // if (m_oneMinCandles.size() < 26) {
    //     std::cout << "[TimeOrderedTickBuffer] Insufficient data for indicators: " 
    //               << m_oneMinCandles.size() << "/26 candles available" << std::endl;
    //     return indicators;  // Return empty indicators
    // }
    
    // std::cout << "[TimeOrderedTickBuffer] Computing technical indicators from " 
    //           << m_oneMinCandles.size() << " candles..." << std::endl;
    
    // Extract price and volume arrays for calculations
    std::vector<double> closes, highs, lows, volumes;
    for (const auto& candle : m_oneMinCandles) {
        closes.push_back(candle.close);    // Closing prices for trend analysis
        highs.push_back(candle.high);      // High prices for volatility
        lows.push_back(candle.low);        // Low prices for support levels
        volumes.push_back(candle.volume);  // Volume for VWAP and flow analysis
    }
    
    // Use VWAP from latest tick (already provided by IBKR)
    if (!m_orderedTicks.empty()) {
        indicators.vwap = m_orderedTicks.rbegin()->second.vwap;
    } else {
        indicators.vwap = 0.0;
    }
    
    // Calculate Fast and Slow EMAs - Primary trend indicators
    // These form the backbone of most trend-following strategies
    indicators.ema9 = m_calculator->calculateEMA(closes, 9);   // Fast trend
    indicators.ema26 = m_calculator->calculateEMA(closes, 26); // Slow trend
    
    // Calculate ALMA - Advanced trend smoothing
    // Less lag than traditional MAs, better for fast markets
    indicators.alma = m_calculator->calculateALMA(
        closes, 
        technical_calculator::config::ALMA_WINDOW_SIZE,  // Window size
        technical_calculator::config::ALMA_SIGMA,        // Smoothing factor
        technical_calculator::config::ALMA_OFFSET);      // Phase offset
    
    // Use incremental RSI calculation - maintained in rolling state
    // This provides consistent RSI values that match the time window approach
    indicators.rsi = m_lastRSI;
    
    // Calculate Chaikin Oscillator - Money flow analysis
    // Use our incrementally calculated value for better performance and accuracy
    indicators.chaikin = m_lastChaikin;
    
    std::cout << "[TimeOrderedTickBuffer] Calculated indicators: "
              << "VWAP=" << std::fixed << std::setprecision(4) << indicators.vwap
              << ", EMA9=" << indicators.ema9 
              << ", EMA26=" << indicators.ema26
              << ", RSI=" << std::setprecision(1) << indicators.rsi
              << ", ALMA=" << std::setprecision(4) << indicators.alma
              << ", Chaikin=" << std::setprecision(6) << indicators.chaikin << std::endl;
    
    return indicators;
}

/**
 * updateChaikinForCandle() - Consolidated Chaikin Oscillator Calculation
 * 
 * Single entry point for Chaikin calculation with rolling window support.
 * Computes MFM, MFV, updates ADL, EMAs, and stores MFV for rolling window.
 */
void TimeOrderedTickBuffer::updateChaikinForCandle(const Candle& candle, int64_t minuteIndex, bool isFirstTime) {
    if (candle.high == candle.low) return;

    // Compute MFM and clamp to [-1,1]
    double mfm = (2.0 * candle.close - candle.high - candle.low) 
               / (candle.high - candle.low);
    
    #if __cplusplus >= 201703L  // C++17 or later
        mfm = std::clamp(mfm, -1.0, 1.0);
    #else
        mfm = std::max(-1.0, std::min(1.0, mfm));
    #endif

    // Verify no NaN/infinity before proceeding with EMA updates
    if (!std::isfinite(mfm) || !std::isfinite(candle.volume)) return;

    // Compute MFV
    double mfv = mfm * candle.volume;
    
    // Additional safety check for MFV
    if (!std::isfinite(mfv)) return;
    
    // Update running ADL
    m_runningADL += mfv;

    // Update EMAs (seed on first time, incremental otherwise)
    if (isFirstTime) {
        m_emaADL_fast = m_emaADL_slow = m_runningADL;
    } else {
        m_emaADL_fast += ALPHA_FAST * (m_runningADL - m_emaADL_fast);
        m_emaADL_slow += ALPHA_SLOW * (m_runningADL - m_emaADL_slow);
    }

    // Compute Chaikin
    m_lastChaikin = m_emaADL_fast - m_emaADL_slow;

    // Store MFV for rolling window
    m_candleMFV[minuteIndex] = mfv;
}

/**
 * updateRSIForCandle() - Incremental RSI Calculation with Wilder's Smoothing
 * 
 * Maintains rolling RSI state without recalculating from scratch.
 * Uses Wilder's smoothing method for accurate RSI calculation.
 */
void TimeOrderedTickBuffer::updateRSIForCandle(double close) {
    // First candle ever - initialize
    if (std::isnan(m_prevClose)) {
        m_prevClose = close;
        return;
    }

    double change = close - m_prevClose;
    m_prevClose = close;

    // Warm-up phase: collect the first RSI_PERIOD changes to seed with SMA
    if (m_rsiWarmupCount < RSI_PERIOD) {
        if (change > 0) {
            m_avgGain += change;
        } else {
            m_avgLoss += -change;
        }
        ++m_rsiWarmupCount;
        
        // Complete the seed when we have enough data
        if (m_rsiWarmupCount == RSI_PERIOD) {
            m_avgGain /= RSI_PERIOD;
            m_avgLoss /= RSI_PERIOD;
        }
        
        m_lastRSI = 50.0;  // Stay neutral until seed is complete
        return;
    }

    // Apply Wilder's smoothing for ongoing RSI calculation
    double gain = change > 0 ? change : 0.0;
    double loss = change < 0 ? -change : 0.0;
    
    m_avgGain = (m_avgGain * (RSI_PERIOD - 1) + gain) / RSI_PERIOD;
    m_avgLoss = (m_avgLoss * (RSI_PERIOD - 1) + loss) / RSI_PERIOD;
    
    // Calculate RSI with safety check for division by zero
    if (m_avgLoss < 1e-8) {
        m_lastRSI = 100.0;
    } else {
        double rs = m_avgGain / m_avgLoss;
        m_lastRSI = 100.0 - (100.0 / (1.0 + rs));
    }
}

} // namespace time_ordered_tick_buffer 