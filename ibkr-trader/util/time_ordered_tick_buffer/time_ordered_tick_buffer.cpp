// time_ordered_tick_buffer.cpp

#include "time_ordered_tick_buffer.hpp"
#include "ring_buffer_trade_handler.hpp"
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

// legacy code, now implemented in the ring_buffer_trade_handler.cpp file
// Chaikin Oscillator Configuration
// static constexpr int    FAST_PERIOD        = 3;                        // Fast EMA period (3 candles)
// static constexpr int    SLOW_PERIOD        = 10;                       // Slow EMA period (10 candles)
// static constexpr int    FAST_PERIOD_MS     = FAST_PERIOD * 60 * 1000;  // 3 minutes in milliseconds
// static constexpr int    SLOW_PERIOD_MS     = SLOW_PERIOD * 60 * 1000;  // 10 minutes in milliseconds
// static constexpr double ALPHA_FAST         = 2.0 / (FAST_PERIOD + 1);  // Fast EMA smoothing factor
// static constexpr double ALPHA_SLOW         = 2.0 / (SLOW_PERIOD + 1);  // Slow EMA smoothing factor

// // Price EMA Configuration
// static constexpr int    PRICE_EMA_FAST     = 9;                        // Fast price EMA period
// static constexpr int    PRICE_EMA_SLOW     = 26;                       // Slow price EMA period
// static constexpr double ALPHA_PRICE_FAST   = 2.0 / (PRICE_EMA_FAST + 1);  // Fast price EMA smoothing
// static constexpr double ALPHA_PRICE_SLOW   = 2.0 / (PRICE_EMA_SLOW + 1);  // Slow price EMA smoothing

// Time Conversion Constants
static constexpr int64_t MS_PER_MINUTE     = 60 * 1000;                // Milliseconds per minute

/**
 * ========================================================================
 * TimeOrderedTickBuffer - Ring Buffer Management and Candle Aggregation
 * ========================================================================
 * 
 * PURPOSE:
 * This class manages ring buffers and candle aggregation. Technical indicator
 * calculations have been moved to RingBufferTradeHandler to separate concerns.
 * 
 * KEY RESPONSIBILITIES:
 * 1. CHRONOLOGICAL ORDERING: Maintains all incoming ticks in perfect time order
 * 2. CANDLE AGGREGATION: Converts chaotic tick data into smooth 1-minute candles
 * 3. RING BUFFER MANAGEMENT: Manages fixed-size buffers for efficient access
 * 4. MEMORY MANAGEMENT: Automatically prunes old data to prevent memory bloat
 * 5. DATA PROVISION: Provides clean data to calculation engines
 * 
 * FLOW:
 * Tick Data → Time Ordering → Candle Aggregation → Ring Buffer Storage → Calculator Access
 */

// Constructor - Initialize the buffer with a specific time window
TimeOrderedTickBuffer::TimeOrderedTickBuffer(int64_t windowSizeMs)
    : m_windowSizeMs(windowSizeMs),
      m_windowMinutes(static_cast<size_t>(std::max<int64_t>(1, windowSizeMs / MS_PER_MINUTE))),
      m_lastCandleUpdateTime(0),
      m_candleUpdateFrequencyMs(1000),  // Default 1 second update frequency
      m_priceRingHead(0),
      m_priceRingCount(0),
      m_candleRingHead(0),
      m_candleRingCount(0),
      m_lastProcessedMinute(-1)
{
    // Initialize fixed-size ring buffers for O(1) operations
    m_candleRing.resize(m_windowMinutes); // Purpose: Stores finalized candles for each minute slot
    m_minuteRing.resize(m_windowMinutes); // Purpose: Stores temporary candles being built for each minute slot
    // Initialize price ring for ALMA calculation (size will be set by calculator)
    m_priceRing.resize(9);  // Default size, calculator may resize  // Purpose: Stores price data for ALMA calculation
    
    m_minuteIndices.resize(m_windowMinutes, -1);  // Initialize to invalid 
    
    // Initialize all price ring slots to prevent NaN accumulation
    std::fill(m_priceRing.begin(), m_priceRing.end(), 0.0);
    
    std::cout << "[TimeOrderedTickBuffer] Initialized with " << (windowSizeMs / 1000) 
              << " second time window (" << m_windowMinutes << " minute ring buffers)" << std::endl;
}

// Destructor - unique_ptr automatically cleans up
TimeOrderedTickBuffer::~TimeOrderedTickBuffer() = default;

// Set the calculator for technical indicators
void TimeOrderedTickBuffer::setCalculator(ring_buffer_trade_handler::RingBufferTradeHandler* calculator) {
    m_calculator = calculator;
    std::cout << "[TimeOrderedTickBuffer] Calculator set - technical indicators will be computed externally" << std::endl;
}

/**
 * ========================================================================
 * addTick() - The Primary Data Ingestion Method
 * ========================================================================
 * 
 * This is where all market data enters our ring buffer pipeline.
 * Each tick represents a real-time market event (price change, trade, etc.)
Tick 1 arrives (minute 100) → Start building candle for minute 100
Tick 2 arrives (minute 100) → Add to same candle (O(1))
Tick 3 arrives (minute 100) → Add to same candle (O(1))
...
Tick 12,000 arrives (minute 100) → Add to same candle (O(1))

Tick 12,001 arrives (minute 101) → 
  ✅ FINISH candle for minute 100 (emit it)
  🆕 START new candle for minute 101
 */
void TimeOrderedTickBuffer::addTick(const stock_data_tick::StockData& tick) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::cout << "[TimeOrderedTickBuffer] 🎯 TICK RECEIVED - Symbol: " << tick.symbol 
              << ", Timestamp: " << tick.timestamp << ", Last: $" << tick.last 
              << ", Volume: " << tick.volume << std::endl;

    // -------- 1) O(1) store for map consumers (keep for compatibility) -------
    size_t sizeBefore = m_orderedTicks.size();
    m_orderedTicks.emplace(tick.timestamp, tick);
    size_t sizeAfter = m_orderedTicks.size();
    
    std::cout << "[TimeOrderedTickBuffer] 📦 Tick stored in map - Buffer size: " << sizeBefore 
              << " → " << sizeAfter << " (growth: " << (sizeAfter > sizeBefore ? "✓" : "✗") << ")" << std::endl;

    // -------- 2) O(1) update of current minute candle ---------
    const int64_t minuteIdx = tick.timestamp / MS_PER_MINUTE;
    
    std::cout << "[TimeOrderedTickBuffer] 🕐 Tick minute: " << minuteIdx 
              << ", Working minute: " << m_workingMinute << std::endl;

    if (m_workingMinute == -1) {                    // very first tick ever
        std::cout << "[TimeOrderedTickBuffer] 🚀 FIRST TICK - Initializing working minute: " << minuteIdx << std::endl;
        m_workingMinute = minuteIdx;
    }

    if (minuteIdx == m_workingMinute) {
        // Same minute - just update the working candle (O(1))
        std::cout << "[TimeOrderedTickBuffer] ⚡ SAME MINUTE - Updating working candle (O(1))" << std::endl;
        m_workingCandle.update(tick.last, tick.volume);
        std::cout << "[TimeOrderedTickBuffer] 📊 Working candle updated: OHLCV=" 
                  << m_workingCandle.open << "/" << m_workingCandle.high << "/" 
                  << m_workingCandle.low << "/" << m_workingCandle.close 
                  << " Vol=" << m_workingCandle.volume << std::endl;
        
        // Prune old ticks periodically (not on every tick for performance)
        static int tickCount = 0;
        if (++tickCount % 100 == 0) {  // Every 100 ticks
            pruneOldTicks();
        }
        return;                                     // <<<< FAST PATH EXIT
    }

    if (minuteIdx < m_workingMinute) {
        // Out-of-order tick for an older minute
        std::cout << "[TimeOrderedTickBuffer] ⏪ OUT-OF-ORDER tick for minute " << minuteIdx 
                  << " (working: " << m_workingMinute << ")" << std::endl;
        handleOutOfOrderTick(minuteIdx, tick);
        return;
    }

    // -------- 3) minute rolled over → finalise old candle -----
    std::cout << "[TimeOrderedTickBuffer] 🔄 MINUTE ROLLOVER: " << m_workingMinute 
              << " → " << minuteIdx << " - Finalising working candle" << std::endl;
    
    finaliseWorkingCandle();                        // Emit completed candle

    // -------- 4) start new minute -----------------------------
    std::cout << "[TimeOrderedTickBuffer] 🆕 STARTING NEW MINUTE: " << minuteIdx << std::endl;
    m_workingMinute = minuteIdx;
    m_workingCandle = TemporaryCandle{};            // Reset
    m_workingCandle.update(tick.last, tick.volume);
    
    std::cout << "[TimeOrderedTickBuffer] 📊 New working candle initialized: OHLCV=" 
              << m_workingCandle.open << "/" << m_workingCandle.high << "/" 
              << m_workingCandle.low << "/" << m_workingCandle.close 
              << " Vol=" << m_workingCandle.volume << std::endl;
}

/**
 * ========================================================================
 * calculateIndicators() - Delegates to External Calculator
 * ========================================================================
 */
TechnicalIndicators TimeOrderedTickBuffer::calculateIndicators() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_calculator) {
        return m_calculator->computeIndicatorsFromCandles();
    } else {
        std::cout << "[TimeOrderedTickBuffer] ⚠️ No calculator set - returning blank indicators" << std::endl;
        return TechnicalIndicators{};  // Return blank indicators
    }
}

// ========================================================================
// PRIVATE HELPER METHODS
// ========================================================================

/**
 * getCurrentTimestamp() - System Time Reference
 */
int64_t TimeOrderedTickBuffer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto value = now_ms.time_since_epoch();
    return value.count();
}

bool TimeOrderedTickBuffer::shouldUpdateCandles() {
    // ===== LEGACY METHOD - NO LONGER USED =====
    // Incremental candle engine now handles candle creation in O(1) time
    // This method is kept for compatibility but always returns false
    std::cout << "[TimeOrderedTickBuffer] ⚠️ Legacy shouldUpdateCandles() called - "
              << "incremental engine now handles candles automatically" << std::endl;
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
    std::cout << "[TimeOrderedTickBuffer] 🕯️ ======= STARTING updateCandles() =======" << std::endl;
    std::cout << "[TimeOrderedTickBuffer] 📊 Initial state:" << std::endl;
    std::cout << "  - Ordered ticks: " << m_orderedTicks.size() << std::endl;
    std::cout << "  - Window minutes: " << m_windowMinutes << std::endl;
    std::cout << "  - Candle ring count: " << m_candleRingCount << std::endl;
    std::cout << "  - Last processed minute: " << m_lastProcessedMinute << std::endl;
    
    // Ring buffer approach: O(1) insertions, no pruning needed
    // Clear all slots to prevent stale data from being reprocessed
    std::fill(m_minuteIndices.begin(), m_minuteIndices.end(), -1);
    for (auto& tc : m_minuteRing) tc = TemporaryCandle{};
    
    std::cout << "[TimeOrderedTickBuffer] 🔄 Aggregating " << m_orderedTicks.size() 
              << " ticks into 1-minute candles using ring buffers..." << std::endl;
    
    // Group ticks by minute using ring buffer for O(1) access
    size_t ticksProcessed = 0;
    std::cout << "[TimeOrderedTickBuffer] 🔍 Processing ticks chronologically..." << std::endl;
    
    for (const auto& [timestamp, tick] : m_orderedTicks) {
        int64_t minuteIndex = timestamp / MS_PER_MINUTE;
        
        // Map minute to ring buffer slot using modulo (safe since m_windowMinutes > 0)
        size_t slot = static_cast<size_t>(minuteIndex % static_cast<int64_t>(m_windowMinutes));
        
        if (ticksProcessed < 5) {  // Log first 5 ticks for debugging
            std::cout << "[TimeOrderedTickBuffer] 📊 Tick #" << ticksProcessed << ": " 
                      << "timestamp=" << timestamp << ", minuteIndex=" << minuteIndex 
                      << ", slot=" << slot << ", price=" << tick.last 
                      << ", volume=" << tick.volume << std::endl;
        }
        
        // Check if this slot is for the current minute or needs reset
        if (m_minuteIndices[slot] != minuteIndex) {
            // New minute or stale slot - reset the temporary candle
            if (ticksProcessed < 5) {
                std::cout << "[TimeOrderedTickBuffer] 🆕 New minute detected for slot " << slot 
                          << " (was: " << m_minuteIndices[slot] << ", now: " << minuteIndex << ")" << std::endl;
            }
            m_minuteRing[slot] = TemporaryCandle{};
            m_minuteIndices[slot] = minuteIndex;
        }
        
        // Update the temporary candle for this minute
        m_minuteRing[slot].update(tick.last, tick.volume);
        ticksProcessed++;
    }
    
    std::cout << "[TimeOrderedTickBuffer] ✅ Processed " << ticksProcessed << " ticks total" << std::endl;
    
    // Calculate window boundaries
    int64_t currentTime = getCurrentTimestamp();
    int64_t windowStart = (currentTime - m_windowSizeMs) / MS_PER_MINUTE;
    
    std::cout << "[TimeOrderedTickBuffer] 🪟 Window boundaries:" << std::endl;
    std::cout << "  - Current time: " << currentTime << std::endl;
    std::cout << "  - Window start (minutes): " << windowStart << std::endl;
    std::cout << "  - Last processed minute: " << m_lastProcessedMinute << std::endl;
    
    // Process NEW candles from ring buffer
    std::vector<std::pair<int64_t, TemporaryCandle*>> newCandles;
    std::cout << "[TimeOrderedTickBuffer] 🔍 Scanning for new candles..." << std::endl;
    
    for (size_t i = 0; i < m_windowMinutes; ++i) {
        int64_t minuteIndex = m_minuteIndices[i];
        
        std::cout << "[TimeOrderedTickBuffer] 📍 Slot " << i << ": minuteIndex=" << minuteIndex;
        
        if (minuteIndex == -1) {
            std::cout << " → SKIP (empty slot)" << std::endl;
            continue;  // Empty slot
        }
        if (minuteIndex <= m_lastProcessedMinute) {
            std::cout << " → SKIP (already processed, lastProcessed=" << m_lastProcessedMinute << ")" << std::endl;
            continue;  // Already processed
        }
        if (minuteIndex < windowStart) {
            std::cout << " → SKIP (outside window, windowStart=" << windowStart << ")" << std::endl;
            continue;  // Outside window
        }
        if (m_minuteRing[i].isEmpty()) {
            std::cout << " → SKIP (no data)" << std::endl;
            continue;  // No data
        }
        
        std::cout << " → ✅ NEW CANDLE FOUND!" << std::endl;
        newCandles.emplace_back(minuteIndex, &m_minuteRing[i]);
    }
    
    std::cout << "[TimeOrderedTickBuffer] 🎯 Found " << newCandles.size() << " new candles to process" << std::endl;
    
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
        
        // Add to ring buffer with O(1) insertion
        addCandleToRing(candle);
        
        // Update price ring for ALMA calculation
        updatePriceRing(candle.close);
        
        // Delegate technical indicator calculations to external calculator
        if (m_calculator) {
            m_calculator->processNewCandle(candle);
        }
        
        // Track processing progress
        m_lastProcessedMinute = minuteIndex;
    }
    
    std::cout << "[TimeOrderedTickBuffer] Ring buffer contains " << m_candleRingCount 
              << " candles, " << newCandles.size() << " new candles processed" << std::endl;
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
 * updatePriceRing() - Updates price ring for ALMA calculation
 */
void TimeOrderedTickBuffer::updatePriceRing(double newClose) {
    // Skip bad data to prevent NaN poisoning
    if (!std::isfinite(newClose)) {
        return;
    }
    
    // Add price to ring buffer
    m_priceRing[m_priceRingHead] = newClose;
    
    // Advance head pointer with wraparound
    m_priceRingHead = (m_priceRingHead + 1) % m_priceRing.size();
    
    // Track how many valid slots we have
    if (m_priceRingCount < m_priceRing.size()) {
        ++m_priceRingCount;
    }
}

// ========================================================================
// INCREMENTAL CANDLE ENGINE METHODS
// ========================================================================

/**
 * finaliseWorkingCandle() - Called once per minute to emit completed candle
 */
void TimeOrderedTickBuffer::finaliseWorkingCandle() {
    if (m_workingCandle.isEmpty()) {
        std::cout << "[TimeOrderedTickBuffer] 💤 Working candle is empty - nothing to finalize for minute " 
                  << m_workingMinute << std::endl;
        return;
    }

    std::cout << "[TimeOrderedTickBuffer] 🕯️ FINALIZING candle for minute " << m_workingMinute 
              << " - OHLCV: " << m_workingCandle.open << "/" << m_workingCandle.high << "/" 
              << m_workingCandle.low << "/" << m_workingCandle.close 
              << " Vol: " << m_workingCandle.volume << std::endl;

    // Create completed candle
    Candle candle(
        m_workingCandle.open,
        m_workingCandle.high,
        m_workingCandle.low,
        m_workingCandle.close,
        m_workingCandle.volume,
        m_workingMinute * MS_PER_MINUTE
    );

    // Skip candles with invalid OHLC data to prevent NaN poisoning
    if (!std::isfinite(candle.open) || !std::isfinite(candle.high) || 
        !std::isfinite(candle.low) || !std::isfinite(candle.close)) {
        std::cout << "[TimeOrderedTickBuffer] ⚠️ Skipping candle with invalid OHLC data at minute " 
                  << m_workingMinute << std::endl;
        return;
    }

    // Add to candle ring buffer (O(1))
    addCandleToRing(candle);
    
    // Update price ring for ALMA calculation (O(1))
    updatePriceRing(candle.close);
    
    // Delegate technical indicator calculations to external calculator
    if (m_calculator) {
        std::cout << "[TimeOrderedTickBuffer] 📊 Sending candle to calculator for processing..." << std::endl;
        m_calculator->processNewCandle(candle);
    }

    // Also store in minute ring for compatibility with existing monitoring code
    // 
    // NOTE: Ring buffer slot calculation using absolute minutes-since-epoch
    // =====================================================================
    // 
    // This ring buffer uses absolute "minutes-since-epoch" as the key, which means
    // the first slot used depends on WHEN the program started, not slot 0.
    // 
    // Example:
    //   minuteIndex = 1,749,244,357,914 ms / 60,000 = 29,154,072 minutes-since-epoch
    //   slot = 29,154,072 % 60 = 12
    // 
    // So the first minute gets stored in slot 12, not slot 0. This is CORRECT behavior!
    // 
    // Why this is proper:
    // - Ring buffer has 60 slots (one per minute in 60-minute window)
    // - Each minute maps to: minute % 60 = slot
    // - Physical slot depends on when program starts (not a bug!)
    // - After 48 more minutes (60-12), it wraps to slot 0, then 1, 2, etc.
    // - Old slots get overwritten in true ring-buffer fashion
    // 
    // This ensures the 60-minute sliding window works correctly regardless of
    // what wall-clock time the program started.
    size_t slot = static_cast<size_t>(m_workingMinute % static_cast<int64_t>(m_windowMinutes));
    m_minuteRing[slot] = m_workingCandle;
    m_minuteIndices[slot] = m_workingMinute;
    
    // Track processing progress
    m_lastProcessedMinute = m_workingMinute;
    
    std::cout << "[TimeOrderedTickBuffer] ✅ Candle finalized and added to rings - "
              << "Candle ring count: " << m_candleRingCount << std::endl;
}

/**
 * handleOutOfOrderTick() - Handle late/out-of-order ticks
 */
void TimeOrderedTickBuffer::handleOutOfOrderTick(int64_t minuteIdx, const stock_data_tick::StockData& tick) {
    size_t slot = static_cast<size_t>(minuteIdx % static_cast<int64_t>(m_windowMinutes));
    
    // Check if this minute is still editable (not too old)
    if (m_minuteIndices[slot] == minuteIdx) {
        std::cout << "[TimeOrderedTickBuffer] 🔄 Updating out-of-order tick for minute " 
                  << minuteIdx << " in slot " << slot << std::endl;
        m_minuteRing[slot].update(tick.last, tick.volume);
        
        // If this was already finalized, we'd need to recalculate indicators
        // For now, we just update the minute ring data
        std::cout << "[TimeOrderedTickBuffer] ⚠️ Out-of-order tick processed - may need indicator recalculation" << std::endl;
    } else {
        std::cout << "[TimeOrderedTickBuffer] ❌ Out-of-order tick for minute " << minuteIdx 
                  << " is too old (slot " << slot << " now contains minute " 
                  << m_minuteIndices[slot] << ")" << std::endl;
    }
}

} // namespace time_ordered_tick_buffer 