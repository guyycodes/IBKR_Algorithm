/**
 * ==============================================================================
 * Ultra-Low Latency Ring Buffer Trade Handler  
 * ==============================================================================
 * 
 * Reads from 3 fixed-size ring buffers maintained by TimeOrderedTickBuffer:
 * 1. Minute Ring (TemporaryCandle aggregation)
 * 2. Candle Ring (Completed 1-min candles) 
 * 3. Price Ring (ALMA calculation buffer)
 * 
 * Core mission: provide real-time ring buffer monitoring + technical analysis
 */

#ifndef RING_BUFFER_TRADE_HANDLER_HPP
#define RING_BUFFER_TRADE_HANDLER_HPP

#include <atomic>
#include <limits>
#include <memory>
#include <vector>
#include <mutex>

#include <map>

// Forward declaration to avoid circular dependency
namespace ring_buffer_trade_handler {
    class RingBufferMonitor;
}

#include "time_ordered_tick_buffer.hpp"
#include "models/volume_profile/volume_profile_map.hpp"
#include "models/raw_data_model/raw_data_model.hpp"
#include "models/stock_data_tick/stock_data_tick.hpp"

namespace ring_buffer_trade_handler {

// constexpr double VWAP_MAX_DIST = 0.005;   // 0.5 %
// constexpr double SPREAD_MAX    = 0.04;    // $0.04
// constexpr int    VOL_LOOKBACK  = 20;

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

class RingBufferTradeHandler {
public:
    RingBufferTradeHandler(time_ordered_tick_buffer::TimeOrderedTickBuffer& buf,
                           volume_profile_map::VolumeProfileMap&           vol,
                           raw_data_model::RawDataModel&                   model);

    // Destructor for clean thread shutdown
    ~RingBufferTradeHandler();

    // ── trading entry point ────────────────────────────────────────────
    bool evaluate(const stock_data_tick::StockData& tick);

    // ── monitor control (idempotent) ───────────────────────────────────
    void startMonitoring();   // spawns RingBufferMonitor
    void stopMonitoring();    // joins RingBufferMonitor

    // Ultra-low latency ring buffer monitoring (15-second real-time monitoring)
    // void monitorRingBuffersRealTime();
    // void printMinuteRing();
    // void printCandleRing(); 
    // void printPriceRing();
    // void printTechnicalIndicators();
    
    // ── technical‑analysis interface used by RingBufferMonitor ─────────
    time_ordered_tick_buffer::TechnicalIndicators computeIndicatorsFromCandles();
    void   processNewCandle(const time_ordered_tick_buffer::Candle& c);

    

private:
    // helpers for indicator maintenance
    void updateRSIForCandle(double close);
    void updateATRForCandle(const time_ordered_tick_buffer::Candle& candle);
    void updatePriceEMAs(double close);
    void updateAlmaIncremental(double newClose);
    void initializeAlmaWeights();
    double calculateALMA(const std::vector<double>& prices,
                         int windowSize,
                         double sigma,
                         double offset) const;
    time_ordered_tick_buffer::TechnicalIndicators computeIndicatorsInternal();

    // ── data sources ───────────────────────────────────────────────────
    time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buf;
    volume_profile_map::VolumeProfileMap&            m_vol;
    raw_data_model::RawDataModel&                    m_model;

    // ── external monitor ───────────────────────────────────────────────────
    std::unique_ptr<RingBufferMonitor> m_monitor;
    std::mutex m_monitorMtx;
    
    // ── atomic snapshot for race-free indicator access ─────────────────────
    std::shared_ptr<time_ordered_tick_buffer::TechnicalIndicators> m_snapshot;
    mutable std::mutex m_snapshotMtx;
    
    // ── state for incremental indicator updates ────────────────────────
    // RSI
    double m_prevClose = std::numeric_limits<double>::quiet_NaN();
    double m_avgGain   = 0.0;
    double m_avgLoss   = 0.0;
    double m_lastRSI   = 50.0;
    static constexpr int RSI_PERIOD = 14;
    int m_rsiWarmupCount = 0;
    
    // ATR
    double m_atr = std::numeric_limits<double>::quiet_NaN();
    static constexpr int ATR_PERIOD = 14;
    int m_atrWarmupCount = 0;
    double m_prevCloseForATR = std::numeric_limits<double>::quiet_NaN();
    
    // EMAs
    double m_emaPriceFast = std::numeric_limits<double>::quiet_NaN();
    double m_emaPriceSlow = std::numeric_limits<double>::quiet_NaN();
    static constexpr int    PRICE_EMA_FAST  = 9;
    static constexpr int    PRICE_EMA_SLOW  = 26;
    static constexpr double ALPHA_PRICE_FAST = 2.0 / (PRICE_EMA_FAST + 1);
    static constexpr double ALPHA_PRICE_SLOW = 2.0 / (PRICE_EMA_SLOW + 1);
    
    // ALMA
    std::vector<double> m_almaWeights;         // Pre-computed ALMA weight vector
    double m_almaDot         = 0.0;            // Running ALMA dot product
    size_t  m_almaSizeWindow = 9;
    double  m_almaSigma      = 0.85;
    double  m_almaOffset     = 6.0;
    
};

} // namespace ring_buffer_trade_handler

#endif // RING_BUFFER_TRADE_HANDLER_HPP
