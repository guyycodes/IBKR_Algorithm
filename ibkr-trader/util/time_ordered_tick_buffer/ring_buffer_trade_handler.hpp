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

#include <iostream>
#include <vector>
#include <map>
#include <limits>
#include "time_ordered_tick_buffer.hpp"
#include "models/volume_profile/volume_profile_map.hpp"
#include "models/raw_data_model/raw_data_model.hpp"
#include "models/stock_data_tick/stock_data_tick.hpp"

namespace ring_buffer_trade_handler {

class RingBufferTradeHandler {
public:
    RingBufferTradeHandler(
        time_ordered_tick_buffer::TimeOrderedTickBuffer& buf,
        volume_profile_map::VolumeProfileMap& vol,
        raw_data_model::RawDataModel& model);

    // Main entry point - evaluates tick and triggers monitoring
    bool evaluate(const stock_data_tick::StockData& tick);

    // Ultra-low latency ring buffer monitoring (15-second real-time monitoring)
    void monitorRingBuffersRealTime();
    void printMinuteRing();
    void printCandleRing(); 
    void printPriceRing();
    void printTechnicalIndicators();
    
    // Technical indicator calculation methods (moved from TimeOrderedTickBuffer)
    time_ordered_tick_buffer::TechnicalIndicators computeIndicatorsFromCandles();
    void updateRSIForCandle(double close);
    void updateATRForCandle(const time_ordered_tick_buffer::Candle& candle);
    void updateAlmaIncremental(double newClose);
    void updatePriceEMAs(double close);
    double calculateALMA(
        const std::vector<double>& prices,
        int windowSize,
        double sigma,
        double offset
    ) const;
    
    // ALMA initialization
    void initializeAlmaWeights();
    
    // Process new candle for all indicators
    void processNewCandle(const time_ordered_tick_buffer::Candle& candle);

private:
    // References to data sources
    time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buf;
    volume_profile_map::VolumeProfileMap& m_vol;
    raw_data_model::RawDataModel& m_model;
    
    // Technical indicator state (moved from TimeOrderedTickBuffer)
    
    // RSI state for incremental calculation
    double m_prevClose = std::numeric_limits<double>::quiet_NaN();
    double m_avgGain = 0.0;
    double m_avgLoss = 0.0;
    double m_lastRSI = 50.0;
    static constexpr int RSI_PERIOD = 14;
    int m_rsiWarmupCount = 0;
    
    // ATR state for incremental calculation
    double m_atr = std::numeric_limits<double>::quiet_NaN();
    static constexpr int ATR_PERIOD = 14;
    int m_atrWarmupCount = 0;
    double m_prevCloseForATR = std::numeric_limits<double>::quiet_NaN();
    
    // Price EMA state for incremental calculation
    double m_emaPriceFast = std::numeric_limits<double>::quiet_NaN();
    double m_emaPriceSlow = std::numeric_limits<double>::quiet_NaN();
    
    // ALMA state for incremental calculation
    std::vector<double> m_almaWeights;         // Pre-computed ALMA weight vector
    double m_almaDot = 0.0;                    // Running ALMA dot product
    size_t m_almaSizeWindow = 9;
    double m_almaSigma = 0.85;
    double m_almaOffset = 6.0;
    
    // EMA configuration constants
    static constexpr int PRICE_EMA_FAST = 9;
    static constexpr int PRICE_EMA_SLOW = 26;
    static constexpr double ALPHA_PRICE_FAST = 2.0 / (PRICE_EMA_FAST + 1);
    static constexpr double ALPHA_PRICE_SLOW = 2.0 / (PRICE_EMA_SLOW + 1);
};

} // namespace ring_buffer_trade_handler

#endif // RING_BUFFER_TRADE_HANDLER_HPP
