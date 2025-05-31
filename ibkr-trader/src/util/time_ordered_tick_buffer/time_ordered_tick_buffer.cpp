#include "time_ordered_tick_buffer.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <limits>    // for quiet_NaN
#include <cmath> 
#include <cstdint>
#include <iterator>
#include <chrono>
#include <mutex>

namespace time_ordered_tick_buffer {

// Chaikin Oscillator Configuration
static constexpr int    FAST_PERIOD        = 3;                        // Fast EMA period (3 candles)
static constexpr int    SLOW_PERIOD        = 10;                       // Slow EMA period (10 candles)
static constexpr int    FAST_PERIOD_MS     = FAST_PERIOD * 60 * 1000;  // 3 minutes in milliseconds
static constexpr int    SLOW_PERIOD_MS     = SLOW_PERIOD * 60 * 1000;  // 10 minutes in milliseconds
static constexpr double ALPHA_FAST         = 2.0 / (FAST_PERIOD + 1);  // Fast EMA smoothing factor
static constexpr double ALPHA_SLOW         = 2.0 / (SLOW_PERIOD + 1);  // Slow EMA smoothing factor

// Price EMA Configuration
static constexpr int    PRICE_EMA_FAST     = 9;                        // Fast price EMA period
static constexpr int    PRICE_EMA_SLOW     = 26;                       // Slow price EMA period
static constexpr double ALPHA_PRICE_FAST   = 2.0 / (PRICE_EMA_FAST + 1);  // Fast price EMA smoothing
static constexpr double ALPHA_PRICE_SLOW   = 2.0 / (PRICE_EMA_SLOW + 1);  // Slow price EMA smoothing

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
      m_windowMinutes(static_cast<size_t>(std::max<int64_t>(1, windowSizeMs / MS_PER_MINUTE))),
      m_lastCandleUpdateTime(0),
      m_candleUpdateFrequencyMs(1000),  // Default 1 second update frequency
      m_calculator(std::make_unique<technical_calculator::TechnicalCalculator>()),
      m_almaDot(0.0),
      // --- NEW -------------------------------------------------------------
      m_priceRingHead(0),
      m_priceRingCount(0),
      m_candleRingHead(0),
      m_candleRingCount(0),
      m_lastProcessedMinute(-1),
      m_prevClose(std::numeric_limits<double>::quiet_NaN()),
      // m_emaADL_fast(std::numeric_limits<double>::quiet_NaN()),
      // m_emaADL_slow(std::numeric_limits<double>::quiet_NaN()),
      m_avgGain(0.0),
      m_avgLoss(0.0),
      m_rsiWarmupCount(0),
      m_lastRSI(50.0),
      m_atr(std::numeric_limits<double>::quiet_NaN()),
      m_atrWarmupCount(0),
      m_emaPriceFast(std::numeric_limits<double>::quiet_NaN()),
      m_emaPriceSlow(std::numeric_limits<double>::quiet_NaN()),
      m_almaSizeWindow(9),
      m_almaSigma(0.85),
      m_almaOffset(6.0)
      // m_lastChaikin(0.0)  // Initialize Chaikin to neutral starting value (commented out)
      // --------------------------------------------------------------------
{
    // Initialize fixed-size ring buffers for O(1) operations
    m_candleRing.resize(m_windowMinutes);
    m_minuteRing.resize(m_windowMinutes);
    m_minuteIndices.resize(m_windowMinutes, -1);  // Initialize to invalid
    
    m_priceRing.resize(m_almaSizeWindow);
    
    // Initialize all price ring slots to prevent NaN accumulation
    std::fill(m_priceRing.begin(), m_priceRing.end(), 0.0);
    
    initializeAlmaWeights();
    
    std::cout << "[TimeOrderedTickBuffer] Initialized with " << (windowSizeMs / 1000) 
              << " second time window (" << m_windowMinutes << " minute ring buffers)" << std::endl;
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
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Insert tick at correct chronological position (handles out-of-order data)
    m_orderedTicks.emplace(tick.timestamp, tick);
    
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
    std::lock_guard<std::mutex> lock(m_mutex);
    
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
    
    // Remove future ticks (handles clock jumps backward from NTP corrections)
    it = m_orderedTicks.upper_bound(currentTime);
    while (it != m_orderedTicks.end()) {
        it = m_orderedTicks.erase(it);
        removedCount++;
    }
    
    // Log pruning activity for monitoring
    if (removedCount > 0) {
        std::cout << "[TimeOrderedTickBuffer] Pruned " << removedCount 
                  << " old/future ticks, buffer now has " << m_orderedTicks.size() 
                  << " ticks spanning " << (m_windowSizeMs / 1000) << " seconds" << std::endl;
    }
}

void TimeOrderedTickBuffer::updateCandles() {
    // Ring buffer approach: O(1) insertions, no pruning needed
    // Clear all slots to prevent stale data from being reprocessed
    std::fill(m_minuteIndices.begin(), m_minuteIndices.end(), -1);
    for (auto& tc : m_minuteRing) tc = TemporaryCandle{};
    
    std::cout << "[TimeOrderedTickBuffer] Aggregating " << m_orderedTicks.size() 
              << " ticks into 1-minute candles using ring buffers..." << std::endl;
    
    // Group ticks by minute using ring buffer for O(1) access
    for (const auto& [timestamp, tick] : m_orderedTicks) {
        int64_t minuteIndex = timestamp / MS_PER_MINUTE;
        
        // Map minute to ring buffer slot using modulo (safe since m_windowMinutes > 0)
        size_t slot = static_cast<size_t>(minuteIndex % static_cast<int64_t>(m_windowMinutes));
        
        // Check if this slot is for the current minute or needs reset
        if (m_minuteIndices[slot] != minuteIndex) {
            // New minute or stale slot - reset the temporary candle
            m_minuteRing[slot] = TemporaryCandle{};
            m_minuteIndices[slot] = minuteIndex;
        }
        
        // Update the temporary candle for this minute
        m_minuteRing[slot].update(tick.last, tick.volume);
    }
    
    // Calculate window boundaries
    int64_t currentTime = getCurrentTimestamp();
    int64_t windowStart = (currentTime - m_windowSizeMs) / MS_PER_MINUTE;
    
    // Maintain rolling ADL by removing old candles' MFV contributions
    // auto mfvIt = m_candleMFV.begin();
    // while (mfvIt != m_candleMFV.end()) {
    //     if (mfvIt->first < windowStart) {
    //         m_runningADL -= mfvIt->second;
    //         mfvIt = m_candleMFV.erase(mfvIt);
    //     } else {
    //         break;
    //     }
    // }
    
    // Clamp ADL to prevent numerical drift
    // if (!std::isfinite(m_runningADL)) {
    //     m_runningADL = 0.0;
    // }
    
    // bool isFirstTime = std::isnan(m_emaADL_fast);
    bool isFirstTime = true;  // Simplified since Chaikin is disabled
    
    // Process NEW candles from ring buffer
    std::vector<std::pair<int64_t, TemporaryCandle*>> newCandles;
    for (size_t i = 0; i < m_windowMinutes; ++i) {
        int64_t minuteIndex = m_minuteIndices[i];
        if (minuteIndex == -1) continue;  // Empty slot
        if (minuteIndex <= m_lastProcessedMinute) continue;  // Already processed
        if (minuteIndex < windowStart) continue;  // Outside window
        if (m_minuteRing[i].isEmpty()) continue;  // No data
        
        newCandles.emplace_back(minuteIndex, &m_minuteRing[i]);
    }
    
    // Sort by minute index for chronological processing
    std::sort(newCandles.begin(), newCandles.end());
    
    // Process each new candle
    for (const auto& [minuteIndex, tempCandle] : newCandles) {
        Candle candle(
            tempCandle->open,
            tempCandle->high,
            tempCandle->low,
            tempCandle->close,
            tempCandle->volume,
            minuteIndex * MS_PER_MINUTE
        );
        
        // Skip candles with invalid OHLC data to prevent NaN poisoning
        if (!std::isfinite(candle.open) || !std::isfinite(candle.high) || 
            !std::isfinite(candle.low) || !std::isfinite(candle.close)) {
            std::cout << "[TimeOrderedTickBuffer] Skipping candle with invalid OHLC data at minute " 
                      << minuteIndex << std::endl;
            continue;  // Skip this candle entirely
        }
        
        // Update technical indicators
        // updateChaikinForCandle(candle, minuteIndex, isFirstTime);
        updateRSIForCandle(candle.close);
        
        // Update price EMAs incrementally
        if (std::isnan(m_emaPriceFast)) {
            m_emaPriceFast = m_emaPriceSlow = candle.close;
        } else {
            m_emaPriceFast += ALPHA_PRICE_FAST * (candle.close - m_emaPriceFast);
            m_emaPriceSlow += ALPHA_PRICE_SLOW * (candle.close - m_emaPriceSlow);
        }
        
        // Add to ring buffer with O(1) insertion
        addCandleToRing(candle);
        
        // Update ATR incrementally with access to previous candle
        if (m_candleRingCount >= 2) {  // Need at least 2 candles for TR calculation
            // Get previous candle from ring buffer
            size_t prevIdx = (m_candleRingHead + m_windowMinutes - 2) % m_windowMinutes;
            const Candle& prevCandle = m_candleRing[prevIdx];
            updateATRForCandle(prevCandle, candle);
        }
        
        // Update ALMA incrementally with O(1) operation
        updateAlmaIncremental(candle.close);
        
        // Track processing progress
        m_lastProcessedMinute = minuteIndex;
        isFirstTime = false;
    }
    
    std::cout << "[TimeOrderedTickBuffer] Ring buffer contains " << m_candleRingCount 
              << " candles, ALMA updated incrementally" << std::endl;
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
    
    // Early exit if no candles available
    if (m_candleRingCount == 0) {
        std::cout << "[TimeOrderedTickBuffer] No candles available - returning blank indicators" << std::endl;
        return indicators;  // All indicators default to 0.0
    }
    
    std::cout << "[TimeOrderedTickBuffer] Computing technical indicators from " 
              << m_candleRingCount << " ring buffer candles..." << std::endl;
    
    // Use VWAP from latest tick (already provided by IBKR)
    if (!m_orderedTicks.empty()) {
        indicators.vwap = m_orderedTicks.rbegin()->second.vwap;
    } else {
        indicators.vwap = 0.0;
    }
    
    // Use incrementally maintained EMAs for O(1) performance
    indicators.ema9 = std::isnan(m_emaPriceFast) ? 0.0 : m_emaPriceFast;
    indicators.ema26 = std::isnan(m_emaPriceSlow) ? 0.0 : m_emaPriceSlow;
    
    // Use O(1) incremental ALMA calculation
    if (m_priceRingCount >= m_almaSizeWindow) {
        indicators.alma = std::isfinite(m_almaDot) ? m_almaDot : 0.0;
    } else {
        // Still warming up - fall back to traditional calculation temporarily
        std::vector<double> closes;
        
        // Iterate through ring buffer in chronological order
        if (m_candleRingCount > 0) {
            size_t start = (m_candleRingHead + m_windowMinutes - m_candleRingCount) % m_windowMinutes;
            for (size_t i = 0; i < m_candleRingCount; ++i) {
                size_t idx = (start + i) % m_windowMinutes;
                closes.push_back(m_candleRing[idx].close);
            }
        }
        
        if (closes.empty()) {
            indicators.alma = 0.0;
        } else {
            indicators.alma = m_calculator->calculateALMA(
                closes,
                m_almaSizeWindow,
                m_almaSigma,
                m_almaOffset);
        }
    }
    
    // Use incremental RSI and ATR calculations
    indicators.rsi = m_lastRSI;
    indicators.atr = std::isnan(m_atr) ? 0.0 : m_atr;
    // indicators.chaikin = m_lastChaikin;
    
    // Save stream state to prevent side effects
    std::ios_base::fmtflags originalFlags = std::cout.flags();
    std::streamsize originalPrecision = std::cout.precision();
    
    std::cout << "[TimeOrderedTickBuffer] Calculated indicators: "
              << "VWAP=" << std::fixed << std::setprecision(4) << indicators.vwap
              << ", EMA9=" << indicators.ema9 
              << ", EMA26=" << indicators.ema26
              << ", RSI=" << std::setprecision(1) << indicators.rsi
              << ", ATR=" << std::setprecision(4) << indicators.atr
              << ", ALMA=" << std::setprecision(4) << indicators.alma << " (fixed incremental)" << std::endl;
              // << ", Chaikin=" << std::setprecision(6) << indicators.chaikin << std::endl;
    
    // Restore original stream state
    std::cout.flags(originalFlags);
    std::cout.precision(originalPrecision);
    
    return indicators;
}

/**
 * updateRSIForCandle() - Incremental RSI Calculation with Wilder's Smoothing
 * 
 * Maintains rolling RSI state without recalculating from scratch.
 * Uses Wilder's smoothing method for accurate RSI calculation.
 */
void TimeOrderedTickBuffer::updateRSIForCandle(double close) {
    // Skip bad data to prevent NaN poisoning
    if (!std::isfinite(close)) {
        return;  // skip bad data
    }
    
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

/**
 * initializeAlmaWeights() - Pre-compute ALMA Weight Vector
 * 
 * Computes the Gaussian-like weight vector for ALMA once during initialization.
 * This enables O(1) incremental ALMA updates instead of O(M) recalculation.
 * 
 * FIXED: Matches the traditional ALMA algorithm from TechnicalCalculator
 */
void TimeOrderedTickBuffer::initializeAlmaWeights()
{
    const size_t M      = m_almaSizeWindow;
    const double sigma  = m_almaSigma;
    const double offset = m_almaOffset; 

    m_almaWeights.resize(M);

    // Centre of the Gaussian on the window
    const double m = offset * (static_cast<double>(M) - 1.0);

    // Standard deviation of the Gaussian
    const double s = static_cast<double>(M) / sigma;

    double norm = 0.0;
    for (size_t i = 0; i < M; ++i) {
        const double x = static_cast<double>(i) - m;
        m_almaWeights[i] = std::exp(-(x * x) / (2.0 * s * s));
        norm += m_almaWeights[i];
    }
    for (double &w : m_almaWeights) w /= norm;

    std::cout << "[TimeOrderedTickBuffer] ALMA weights ready (M=" << M << ", σ=" << sigma
              << ", offset=" << offset << ")\n";
}

/**
 * addCandleToRing() - O(1) Ring Buffer Insertion
 * 
 * Adds a new candle to the fixed-size ring buffer, automatically
 * overwriting the oldest candle when the buffer is full.
 */
void TimeOrderedTickBuffer::addCandleToRing(const Candle& candle) {
    // Store candle in ring buffer
    m_candleRing[m_candleRingHead] = candle;
    
    // Advance head pointer with wraparound
    m_candleRingHead = (m_candleRingHead + 1) % m_windowMinutes;
    
    // Track how many valid slots we have (up to buffer size)
    if (m_candleRingCount < m_windowMinutes) {
        ++m_candleRingCount;
    }
}

/**
 * updateAlmaIncremental() - O(1) ALMA Update
 * 
 * Updates the ALMA dot product incrementally while maintaining correct
 * weight alignment. Uses O(M) recomputation after warmup to avoid
 * alignment issues that cause NaN propagation.
 */
void TimeOrderedTickBuffer::updateAlmaIncremental(double newClose)
{
    // Skip bad data to prevent NaN poisoning
    if (!std::isfinite(newClose)) {
        return;  // skip bad data
    }
    
    const size_t M = m_almaWeights.size();

    m_priceRing[m_priceRingHead] = newClose;

    if (m_priceRingCount < M) {               // warm-up
        m_almaDot += newClose * m_almaWeights[m_priceRingCount];
        ++m_priceRingCount;
        m_priceRingHead = (m_priceRingHead + 1) % M;
    } else {
        m_priceRingHead = (m_priceRingHead + 1) % M;  // head now oldest

        double dot = 0.0;
        size_t  idx = m_priceRingHead;
        for (size_t w = 0; w < M; ++w) {
            dot += m_priceRing[idx] * m_almaWeights[w];
            idx = (idx + 1) % M;
        }
        m_almaDot = dot;
    }

    // ---- new: guarantee no NaN/Inf leaves this function ---------------
    if (!std::isfinite(m_almaDot)) m_almaDot = 0.0;
    m_almaDot = std::clamp(m_almaDot, -1e12, 1e12);  // Support high-value instruments like BRK-A
}

/**
 * updateATRForCandle() - Incremental ATR Calculation with Wilder's Method
 * 
 * Maintains rolling ATR state without recalculating from scratch.
 * Uses proper Wilder's method: SMA seed for first 14 TRs, then exponential smoothing.
 */
void TimeOrderedTickBuffer::updateATRForCandle(const Candle& prev, const Candle& curr) {
    // Skip bad data to prevent NaN poisoning
    if (!std::isfinite(curr.close) || !std::isfinite(curr.high) || !std::isfinite(curr.low) ||
        !std::isfinite(prev.close) || !std::isfinite(prev.high) || !std::isfinite(prev.low)) {
        return;  // skip bad data
    }
    
    double tr = std::max({ curr.high - curr.low,
                          std::fabs(curr.high - prev.close),
                          std::fabs(curr.low - prev.close) });

    if (m_atrWarmupCount < ATR_PERIOD) {          // seeding
        if (std::isnan(m_atr)) m_atr = 0.0;
        m_atr += tr;
        ++m_atrWarmupCount;

        if (m_atrWarmupCount == ATR_PERIOD)
            m_atr /= ATR_PERIOD;                  // finish SMA seed
    }
    else {                                        // Wilder smoothing
        m_atr += (tr - m_atr) / ATR_PERIOD;       // 100% numerically stable
    }
}

/*
/**
 * updateChaikinForCandle() - Consolidated Chaikin Oscillator Calculation
 * 
 * Single entry point for Chaikin calculation with rolling window support.
 * Computes MFM, MFV, updates ADL, EMAs, and stores MFV for rolling window.
 */
// void TimeOrderedTickBuffer::updateChaikinForCandle(const Candle& candle,
//                                                    int64_t minuteIndex,
//                                                    bool    isFirstTime)
// {
//     const double range = candle.high - candle.low;
//     const double mfm   = (range == 0.0) ? 0.0
//                          : std::clamp((2.0 * candle.close - candle.high - candle.low) / range,
//                                       -1.0, 1.0);

//     const double mfv = mfm * candle.volume;

//     // --- running ADL and EMA update ---------------------------------
//     m_runningADL += mfv;

//     if (isFirstTime) {
//         m_emaADL_fast = m_emaADL_slow = m_runningADL;
//     } else {
//         m_emaADL_fast += ALPHA_FAST * (m_runningADL - m_emaADL_fast);
//         m_emaADL_slow += ALPHA_SLOW * (m_runningADL - m_emaADL_slow);
//     }

//     m_lastChaikin = m_emaADL_fast - m_emaADL_slow;
//     m_candleMFV[minuteIndex] = mfv;
// }

} // namespace time_ordered_tick_buffer 