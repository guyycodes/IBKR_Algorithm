#include "ring_buffer_trade_handler.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ring_buffer_trade_handler {

using TI = time_ordered_tick_buffer::TechnicalIndicators;

constexpr double VWAP_MAX_DIST = 0.005;   // 0.5 %
constexpr double SPREAD_MAX    = 0.04;    // $0.04
constexpr int    VOL_LOOKBACK  = 20;

RingBufferTradeHandler::RingBufferTradeHandler(
        time_ordered_tick_buffer::TimeOrderedTickBuffer& b,
        volume_profile_map::VolumeProfileMap&            v,
        raw_data_model::RawDataModel&                    m)
    : m_buf(b), m_vol(v), m_model(m) 
{
    // Initialize ALMA weights on construction
    initializeAlmaWeights();
    
    // Set this handler as the calculator for the buffer
    m_buf.setCalculator(this);
    
    std::cout << "[RingBufferTradeHandler] Initialized with technical indicator calculations" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TECHNICAL INDICATOR CALCULATIONS (moved from TimeOrderedTickBuffer)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * computeIndicatorsFromCandles() - Advanced Technical Analysis
 * 
 * This is the culmination of our data processing pipeline. We take clean,
 * aggregated candle data and compute sophisticated technical indicators
 * that professional traders rely on for market analysis.
 */
TI RingBufferTradeHandler::computeIndicatorsFromCandles() {
    TI indicators;
    
    // Early exit if no candles available
    if (m_buf.getCandleRingCount() == 0) {
        std::cout << "[RingBufferCalculator] No candles available - returning blank indicators" << std::endl;
        return indicators;  // All indicators default to 0.0
    }
    
    std::cout << "[RingBufferCalculator] Computing technical indicators from " 
              << m_buf.getCandleRingCount() << " ring buffer candles..." << std::endl;
    
    // Use VWAP from latest tick (already provided by IBKR)
    const auto& orderedTicks = m_buf.getOrderedTicks();
    if (!orderedTicks.empty()) {
        indicators.vwap = orderedTicks.rbegin()->second.vwap;
    } else {
        indicators.vwap = 0.0;
    }
    
    // Use incrementally maintained EMAs for O(1) performance
    indicators.ema9 = std::isnan(m_emaPriceFast) ? 0.0 : m_emaPriceFast;
    indicators.ema26 = std::isnan(m_emaPriceSlow) ? 0.0 : m_emaPriceSlow;
    
    // Use O(1) incremental ALMA calculation
    if (m_buf.getPriceRingCount() >= m_almaSizeWindow) {
        indicators.alma = std::isfinite(m_almaDot) ? m_almaDot : 0.0;
    } else {
        // Still warming up - fall back to traditional calculation temporarily
        std::vector<double> closes;
        
        // Iterate through ring buffer in chronological order
        if (m_buf.getCandleRingCount() > 0) {
            const auto& candleRing = m_buf.getCandleRing();
            size_t head = m_buf.getCandleRingHead();
            size_t count = m_buf.getCandleRingCount();
            size_t windowMinutes = m_buf.getWindowMinutes();
            
            size_t start = (head + windowMinutes - count) % windowMinutes;
            for (size_t i = 0; i < count; ++i) {
                size_t idx = (start + i) % windowMinutes;
                closes.push_back(candleRing[idx].close);
            }
        }
        
        if (closes.empty()) {
            indicators.alma = 0.0;
        } else {
            indicators.alma = calculateALMA(
                closes,
                m_almaSizeWindow,
                m_almaSigma,
                m_almaOffset);
        }
    }
    
    // Use incremental RSI and ATR calculations
    indicators.rsi = m_lastRSI;
    indicators.atr = std::isnan(m_atr) ? 0.0 : m_atr;
    
    // Debug output for calculated indicators
    std::cout << "[RingBufferCalculator] ✅ Calculated indicators: "
              << "VWAP=$" << std::fixed << std::setprecision(4) << indicators.vwap
              << ", EMA9=$" << indicators.ema9 
              << ", EMA26=$" << indicators.ema26
              << ", RSI=" << std::setprecision(1) << indicators.rsi << "%"
              << ", ATR=$" << std::setprecision(4) << indicators.atr
              << ", ALMA=$" << std::setprecision(4) << indicators.alma << std::endl;
    
    return indicators;
}

/**
 * updateRSIForCandle() - Incremental RSI Calculation with Wilder's Smoothing
 * 
 * Maintains rolling RSI state without recalculating from scratch.
 * Uses Wilder's smoothing method for accurate RSI calculation.
 */
void RingBufferTradeHandler::updateRSIForCandle(double close) {
    // Skip bad data to prevent NaN poisoning
    if (!std::isfinite(close)) {
        std::cout << "[RingBufferCalculator] ⚠️ Skipping RSI update - invalid close price" << std::endl;
        return;  // skip bad data
    }
    
    // First candle ever - initialize
    if (std::isnan(m_prevClose)) {
        m_prevClose = close;
        std::cout << "[RingBufferCalculator] 🚀 RSI initialization with close=$" 
                  << std::fixed << std::setprecision(4) << close << std::endl;
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
            std::cout << "[RingBufferCalculator] 📊 RSI warmup complete! AvgGain=" 
                      << std::setprecision(6) << m_avgGain << ", AvgLoss=" << m_avgLoss << std::endl;
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
    
    // Debug output every 10 calculations to avoid spam
    static int rsiDebugCounter = 0;
    if (++rsiDebugCounter % 10 == 0) {
        std::cout << "[RingBufferCalculator] 📈 RSI updated: " << std::setprecision(1) 
                  << m_lastRSI << "% (change=" << std::setprecision(4) << change << ")" << std::endl;
    }
}

/**
 * updateATRForCandle() - Incremental ATR Calculation with Wilder's Method
 * 
 * Maintains rolling ATR state without recalculating from scratch.
 * Uses proper Wilder's method: SMA seed for first 14 TRs, then exponential smoothing.
 */
void RingBufferTradeHandler::updateATRForCandle(const time_ordered_tick_buffer::Candle& c)
{
    if (!std::isfinite(c.high) || !std::isfinite(c.low) || !std::isfinite(c.close)) {
        std::cout << "[RingBufferCalculator] ⚠️ Skipping ATR update - invalid candle OHLC" << std::endl;
        return;                                                     // bad data guard
    }

    /* ---------- 1) compute True-Range ----------------------------------- */
    double tr;
    if (std::isnan(m_prevCloseForATR)) {                            // very first bar
        tr = c.high - c.low;                                        // TR₀
        std::cout << "[RingBufferCalculator] 🚀 ATR initialization with TR=$" 
                  << std::fixed << std::setprecision(4) << tr << std::endl;
    } else {
        tr = std::max({ c.high - c.low,
                        std::fabs(c.high - m_prevCloseForATR),
                        std::fabs(c.low  - m_prevCloseForATR) });
    }
    m_prevCloseForATR = c.close;                                    // stash for next call

    /* ---------- 2) SMA seed then Wilder smoothing ---------------------- */
    constexpr int N = ATR_PERIOD;                                   // 14 by default
    if (m_atrWarmupCount < N) {                                     // SEED
        if (std::isnan(m_atr)) m_atr = 0.0;
        m_atr += tr;
        if (++m_atrWarmupCount == N) {                              // finished seed
            m_atr /= N;                                             // convert sum → mean
            std::cout << "[RingBufferCalculator] 📊 ATR warmup complete! ATR=$" 
                      << std::setprecision(4) << m_atr << std::endl;
        }
    } else {                                                        // SMOOTH
        m_atr += (tr - m_atr) / N;                                  // Wilder
        
        // Debug output every 10 calculations to avoid spam
        static int atrDebugCounter = 0;
        if (++atrDebugCounter % 10 == 0) {
            std::cout << "[RingBufferCalculator] 📏 ATR updated: $" << std::setprecision(4) 
                      << m_atr << " (TR=$" << tr << ")" << std::endl;
        }
    }
}

/**
 * updatePriceEMAs() - Update Fast and Slow Price EMAs
 */
void RingBufferTradeHandler::updatePriceEMAs(double close) {
    if (!std::isfinite(close)) {
        std::cout << "[RingBufferCalculator] ⚠️ Skipping EMA update - invalid close price" << std::endl;
        return;
    }
    
    if (std::isnan(m_emaPriceFast)) {
        m_emaPriceFast = m_emaPriceSlow = close;
        std::cout << "[RingBufferCalculator] 🚀 EMA initialization with close=$" 
                  << std::fixed << std::setprecision(4) << close << std::endl;
    } else {
        m_emaPriceFast += ALPHA_PRICE_FAST * (close - m_emaPriceFast);
        m_emaPriceSlow += ALPHA_PRICE_SLOW * (close - m_emaPriceSlow);
        
        // Debug output every 10 calculations to avoid spam
        static int emaDebugCounter = 0;
        if (++emaDebugCounter % 10 == 0) {
            std::cout << "[RingBufferCalculator] 📈 EMAs updated: Fast=$" << std::setprecision(4) 
                      << m_emaPriceFast << ", Slow=$" << m_emaPriceSlow << std::endl;
        }
    }
}

/**
 * initializeAlmaWeights() - Pre-compute ALMA Weight Vector
 * 
 * Computes the Gaussian-like weight vector for ALMA once during initialization.
 * This enables O(1) incremental ALMA updates instead of O(M) recalculation.
 */
void RingBufferTradeHandler::initializeAlmaWeights()
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

    std::cout << "[RingBufferCalculator] 🎯 ALMA weights ready (M=" << M << ", σ=" << sigma
              << ", offset=" << offset << ")" << std::endl;
}

/**
 * updateAlmaIncremental() - O(1) ALMA Update
 * 
 * Updates the ALMA dot product incrementally while maintaining correct
 * weight alignment. Uses O(M) recomputation after warmup to avoid
 * alignment issues that cause NaN propagation.
 */
void RingBufferTradeHandler::updateAlmaIncremental(double newClose)
{
    // Skip bad data to prevent NaN poisoning
    if (!std::isfinite(newClose)) {
        std::cout << "[RingBufferCalculator] ⚠️ Skipping ALMA update - invalid close price" << std::endl;
        return;  // skip bad data
    }
    
    const size_t M = m_almaWeights.size();
    const auto& priceRing = m_buf.getPriceRing();
    size_t priceRingHead = m_buf.getPriceRingHead();
    size_t priceRingCount = m_buf.getPriceRingCount();

    if (priceRingCount < M) {               // warm-up
        m_almaDot += newClose * m_almaWeights[priceRingCount];
        
        if (priceRingCount == M - 1) {  // Just completed warmup
            std::cout << "[RingBufferCalculator] 🎯 ALMA warmup complete! Initial ALMA=$" 
                      << std::fixed << std::setprecision(4) << m_almaDot << std::endl;
        }
    } else {
        // Full recalculation to ensure alignment
        double dot = 0.0;
        size_t idx = priceRingHead;
        for (size_t w = 0; w < M; ++w) {
            dot += priceRing[idx] * m_almaWeights[w];
            idx = (idx + 1) % M;
        }
        m_almaDot = dot;
        
        // Debug output every 10 calculations to avoid spam
        static int almaDebugCounter = 0;
        if (++almaDebugCounter % 10 == 0) {
            std::cout << "[RingBufferCalculator] 🎯 ALMA updated: $" << std::setprecision(4) 
                      << m_almaDot << " (newClose=$" << newClose << ")" << std::endl;
        }
    }

    // Guarantee no NaN/Inf leaves this function
    if (!std::isfinite(m_almaDot)) m_almaDot = 0.0;
    m_almaDot = std::clamp(m_almaDot, -1e12, 1e12);  // Support high-value instruments like BRK-A
}

/**
 * calculateALMA() - ALMA Calculation
 * 
 * Computes the ALMA value for a given price series.
 * Uses optimized calculation to avoid unnecessary memory allocations.
 */
double RingBufferTradeHandler::calculateALMA(
    const std::vector<double>& prices,
    int windowSize,
    double sigma,
    double offset
) const
{
    // Need at least windowSize data points
    if ((int)prices.size() < windowSize || windowSize <= 0) {
        return 0.0;
    }

    // Constrain parameters to valid ranges
    sigma = std::max(0.1, std::min(sigma, 1.0));
    offset = std::max(0.0, std::min(offset, 10.0));

    // Calculate distribution center point
    double m = offset;
    
    // Calculate standard deviation factor
    double s = windowSize / (sigma * 10.0);

    // Build weights - optimized to avoid unnecessary memory allocations
    double sumW = 0.0;
    double weightedSum = 0.0;
    int startIdx = static_cast<int>(prices.size()) - windowSize;

    // Calculate weighted sum in a single pass
    for (int i = 0; i < windowSize; ++i) {
        double x = (double)i - m;
        double weight = std::exp(-(x * x) / (2.0 * s * s));
        sumW += weight;
        weightedSum += prices[startIdx + i] * weight;
    }

    // Normalize and return
    return (sumW > 0.0) ? (weightedSum / sumW) : 0.0;
}

/**
 * processNewCandle() - Process new candle for all indicators
 * 
 * This method is called when a new candle is created to update all indicators
 */
void RingBufferTradeHandler::processNewCandle(const time_ordered_tick_buffer::Candle& candle) {
    std::cout << "[RingBufferCalculator] 🕯️ Processing new candle: OHLCV=" 
              << std::fixed << std::setprecision(2) << candle.open << "/" << candle.high 
              << "/" << candle.low << "/" << candle.close << " Vol=" << (int)candle.volume << std::endl;
    
    // Skip candles with invalid OHLC data to prevent NaN poisoning
    if (!std::isfinite(candle.open) || !std::isfinite(candle.high) || 
        !std::isfinite(candle.low) || !std::isfinite(candle.close)) {
        std::cout << "[RingBufferCalculator] ⚠️ Skipping candle with invalid OHLC data" << std::endl;
        return;
    }
    
    // Update all technical indicators
    updateRSIForCandle(candle.close);
    updatePriceEMAs(candle.close);
    updateAlmaIncremental(candle.close);
    
    // Update ATR if we have at least 2 candles for TR calculation
    if (m_buf.getCandleRingCount() >= 2) {
        updateATRForCandle(candle);
    }
    
    std::cout << "[RingBufferCalculator] ✅ All indicators updated for new candle" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════════════
// RING BUFFER MONITORING SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════

void RingBufferTradeHandler::monitorRingBuffersRealTime() {
    std::cout << "\n🚀 [ULTRA-LOW LATENCY] Starting 15-second ring buffer monitoring...\n";
    std::cout << "📊 Reading 3 ring buffers in real-time:\n";
    std::cout << "   1️⃣  Minute Ring (TemporaryCandle aggregation)\n";
    std::cout << "   2️⃣  Candle Ring (Completed 1-min candles)\n"; 
    std::cout << "   3️⃣  Price Ring (ALMA calculation buffer)\n\n";
    
    auto startTime = std::chrono::steady_clock::now();
    auto endTime = startTime + std::chrono::seconds(3600); // 60 minutes × 60 seconds
    
    int iteration = 0;
    while (std::chrono::steady_clock::now() < endTime) {
        std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "📸 [SNAPSHOT #" << ++iteration << "] Ring Buffer Contents:\n";
        
        // Read all 3 ring buffers with ultra-low latency
        printMinuteRing();
        printCandleRing(); 
        printPriceRing();
        printTechnicalIndicators();
        
        // Ultra-fast 100ms polling for real-time updates
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\n✅ [MONITORING COMPLETE] 15-second ring buffer monitoring finished!\n";
    std::cout << "📈 Total snapshots captured: " << iteration << "\n\n";
}

void RingBufferTradeHandler::printMinuteRing() {
    const auto& minuteRing = m_buf.getMinuteRing();
    const auto& minuteIndices = m_buf.getMinuteIndices();
    
    std::cout << "\n1️⃣  MINUTE RING (Tick Aggregation Buffer):\n";
    std::cout << "   Size: " << minuteRing.size() << " slots | Window: " << m_buf.getWindowMinutes() << " minutes\n";
    
    int validSlots = 0;
    for (size_t i = 0; i < minuteRing.size() && i < 5; ++i) {  // Show first 5 slots
        if (minuteIndices[i] != -1 && !minuteRing[i].isEmpty()) {
            validSlots++;
            std::cout << "   📦 Slot[" << i << "] Minute:" << minuteIndices[i] 
                      << " | OHLCV: " << std::fixed << std::setprecision(2)
                      << minuteRing[i].open << "/" << minuteRing[i].high << "/"
                      << minuteRing[i].low << "/" << minuteRing[i].close 
                      << " Vol:" << (int)minuteRing[i].volume << "\n";
        }
    }
    if (validSlots == 0) {
        std::cout << "   💤 No active minute aggregations\n";
    }
}

void RingBufferTradeHandler::printCandleRing() {
    const auto& candleRing = m_buf.getCandleRing();
    size_t head = m_buf.getCandleRingHead();
    size_t count = m_buf.getCandleRingCount();
    
    std::cout << "\n2️⃣  CANDLE RING (Completed Candles Buffer):\n";
    std::cout << "   Size: " << candleRing.size() << " slots | Head: " << head 
              << " | Valid: " << count << "\n";
    
    if (count == 0) {
        std::cout << "   💤 No completed candles yet\n";
        return;
    }
    
    // Show last 3 completed candles (chronological order)
    size_t showCount = std::min<size_t>(3, count);
    for (size_t i = 0; i < showCount; ++i) {
        // Calculate index: start from head-count, then move forward
        size_t idx = (head + candleRing.size() - count + i) % candleRing.size();
        const auto& candle = candleRing[idx];
        
        std::cout << "   🕯️  Candle[" << idx << "] @ " << candle.timestamp/1000 
                  << " | OHLCV: " << std::fixed << std::setprecision(2)
                  << candle.open << "/" << candle.high << "/"
                  << candle.low << "/" << candle.close 
                  << " Vol:" << (int)candle.volume << "\n";
    }
}

void RingBufferTradeHandler::printPriceRing() {
    const auto& priceRing = m_buf.getPriceRing();
    size_t head = m_buf.getPriceRingHead();
    size_t count = m_buf.getPriceRingCount();
    
    std::cout << "\n3️⃣  PRICE RING (ALMA Calculation Buffer):\n";
    std::cout << "   Size: " << priceRing.size() << " slots | Head: " << head 
              << " | Valid: " << count << "\n";
    
    if (count == 0) {
        std::cout << "   💤 No prices yet\n";
        return;
    }
    
    // Show last 5 prices (chronological order)
    size_t showCount = std::min<size_t>(5, count);
    std::cout << "   💰 Recent Prices: ";
    for (size_t i = 0; i < showCount; ++i) {
        // Calculate index: start from head-count, then move forward
        size_t idx = (head + priceRing.size() - count + i) % priceRing.size();
        std::cout << std::fixed << std::setprecision(2) << priceRing[idx];
        if (i < showCount - 1) std::cout << " → ";
    }
    std::cout << "\n";
}

void RingBufferTradeHandler::printTechnicalIndicators() {
    auto indicators = computeIndicatorsFromCandles();
    
    std::cout << "\n📊 TECHNICAL INDICATORS (from RingBufferCalculator):\n";
    std::cout << "   💎 VWAP: $" << std::fixed << std::setprecision(4) << indicators.vwap << "\n";
    std::cout << "   📈 EMA9: $" << std::setprecision(4) << indicators.ema9 
              << " | EMA26: $" << indicators.ema26 << "\n";
    std::cout << "   ⚡ RSI: " << std::setprecision(1) << indicators.rsi << "%\n";
    std::cout << "   📏 ATR: $" << std::setprecision(4) << indicators.atr << "\n";
    std::cout << "   🎯 ALMA: $" << std::setprecision(4) << indicators.alma << "\n";
    
    // Additional debugging info about calculation state
    std::cout << "   🔧 Calc State: RSI warmup=" << m_rsiWarmupCount << "/" << RSI_PERIOD 
              << ", ATR warmup=" << m_atrWarmupCount << "/" << ATR_PERIOD 
              << ", ALMA count=" << m_buf.getPriceRingCount() << "/" << m_almaSizeWindow << "\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
// ORIGINAL TRADING LOGIC (COMMENTED OUT FOR RING BUFFER FOCUS)
// ═══════════════════════════════════════════════════════════════════════════════

// ───────────────────────────────────────────────────────────────────────────────
bool RingBufferTradeHandler::evaluate(const stock_data_tick::StockData& tick)
{
    // For now, just trigger monitoring on first call
    static bool hasStartedMonitoring = false;
    if (!hasStartedMonitoring) {
        hasStartedMonitoring = true;
        std::thread monitorThread(&RingBufferTradeHandler::monitorRingBuffersRealTime, this);
        monitorThread.detach(); // Let it run independently
        
        std::cout << "[RingBufferTradeHandler] 🚀 Started ring buffer monitoring thread" << std::endl;
    }
    
    return false; // Don't generate trade signals, just monitor and calculate
}

} // namespace ring_buffer_trade_handler
