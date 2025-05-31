#include "ring_buffer_trade_handler.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>

namespace ring_buffer_trade_handler {

bool RingBufferTradeHandler::checkForTradeOpportunity(const stock_data_tick::StockData& newTick) {
    // Get current technical indicators
    auto indicators = m_tickBuffer->calculateIndicators();
    
    // Get recent candles for filtering
    std::vector<time_ordered_tick_buffer::Candle> candles = getCandlesFromBuffer();
    
    if (candles.empty()) {
        std::cout << "[RingBufferTradeHandler] No candles available for analysis" << std::endl;
        return false;
    }
    
    // Run through all filters
    bool opportunity = passesInitialFilters(candles, indicators.vwap, indicators.ema9, 
                                          indicators.ema26, indicators.alma, indicators.rsi);
    
    if (opportunity) {
        std::cout << "[RingBufferTradeHandler] Initial filters passed - POTENTIAL OPPORTUNITY!" << std::endl;
        std::cout << "  Price: " << newTick.last << ", VWAP: " << indicators.vwap 
                  << ", EMA9: " << indicators.ema9 << ", EMA26: " << indicators.ema26
                  << ", RSI: " << indicators.rsi << ", ALMA: " << indicators.alma << std::endl;
        std::cout << "  Note: Use confirmEntry(currentPrice) for final confirmation using ring buffer data" << std::endl;
    }
    
    return opportunity;
}

bool RingBufferTradeHandler::passesInitialFilters(
    const std::vector<time_ordered_tick_buffer::Candle>& candles, 
    double vwap, double ema9, double ema26, double alma, double rsi) {
    
    if (candles.size() < static_cast<size_t>(m_supertrendPeriod)) {
        return false;  // Not enough data to compute short supertrend
    }

    // 1) Short-Period Supertrend
    bool isSupertrendBullish = checkShortPeriodSupertrend(candles);
    if (!isSupertrendBullish) {
        return false;
    }

    // 2) Volume Surge
    bool volumeOkay = checkVolumeSurge(candles);
    if (!volumeOkay) {
        return false;
    }

    // 3) VWAP Distance
    bool withinVWAPRange = checkVWAPDistance(candles.back().close, vwap);
    if (!withinVWAPRange) {
        return false;
    }

    // 4) RSI with SMA Overlay - check for upward pressure
    bool hasRsiUptrendPressure = checkRSI(candles);
    if (!hasRsiUptrendPressure) {
        return false;
    }

    // 5) EMA Cross - check if fast EMA (9) is above slow EMA (26)
    bool isFastEmaAboveSlow = checkEmaCross(ema9, ema26);
    if (!isFastEmaAboveSlow) {
        return false;
    }

    // 6) ALMA below EMAs - check if ALMA is below both EMA9 and EMA26
    bool isAlmaBelowEmas = checkAlmaBelowEmas(alma, ema9, ema26);
    if (!isAlmaBelowEmas) {
        return false;
    }

    // All checks passed => valid candidate
    return true;
}

bool RingBufferTradeHandler::confirmEntry(double currentPrice) {
    // 1) Spread Check
    if (!checkBidAskSpread()) {
        std::cout << "[RingBufferTradeHandler] Entry confirmation failed: Bid-ask spread too wide" << std::endl;
        return false;
    }

    // 2) Order Book Imbalance
    if (!checkOrderBookImbalance()) {
        std::cout << "[RingBufferTradeHandler] Entry confirmation failed: Insufficient order book imbalance" << std::endl;
        return false;
    }

    // 3) Micro Pullback
    if (!checkMicroPullback()) {
        std::cout << "[RingBufferTradeHandler] Entry confirmation failed: Micro pullback pattern not suitable" << std::endl;
        return false;
    }

    // 4) Tape Momentum
    if (!checkTapeMomentum()) {
        std::cout << "[RingBufferTradeHandler] Entry confirmation failed: Insufficient tape momentum" << std::endl;
        return false;
    }

    // If all checks pass, we consider it a valid entry signal
    std::cout << "[RingBufferTradeHandler] ENTRY CONFIRMED! All filters passed." << std::endl;
    return true;
}

// Entry Signal Confirmation Methods (reading from ring buffer)

bool RingBufferTradeHandler::checkBidAskSpread() {
    OrderBookSnapshot ob = getCurrentOrderBook();
    double spread = ob.bestAskPrice - ob.bestBidPrice;
    return (spread <= m_maxSpreadThreshold);
}

bool RingBufferTradeHandler::checkOrderBookImbalance() {
    OrderBookSnapshot ob = getCurrentOrderBook();
    if (ob.totalAskSize <= 0.0) {
        return false; 
    }
    double ratio = (ob.totalBidSize / ob.totalAskSize) * 100.0;
    return (ratio >= m_orderBookImbalancePct);
}

bool RingBufferTradeHandler::checkMicroPullback() {
    std::vector<MicroCandle> microCandles = getRecentMicroCandles();
    
    if (microCandles.size() < static_cast<size_t>(m_microPullbackPeriod)) {
        // Not enough micro-candles; skip or be lenient
        return true;
    }

    int startIndex = static_cast<int>(microCandles.size()) - m_microPullbackPeriod;
    double highestHigh = microCandles[startIndex].high;
    double lowestLow   = microCandles[startIndex].low;

    for (int i = startIndex; i < (int)microCandles.size(); ++i) {
        highestHigh = std::max(highestHigh, microCandles[i].high);
        lowestLow   = std::min(lowestLow,   microCandles[i].low);
    }

    double range = highestHigh - lowestLow;
    if (range <= 0.0) {
        return true; 
    }

    double lastClose = microCandles.back().close;
    double pullbackFromHigh = highestHigh - lastClose;
    double pullbackRatio    = pullbackFromHigh / range; 

    // Example logic: we want a moderate pullback between 10% and m_microPullbackPct
    if (pullbackRatio >= 0.1 && pullbackRatio <= m_microPullbackPct) {
        return true; 
    }
    return false;
}

bool RingBufferTradeHandler::checkTapeMomentum() {
    std::vector<TapePrint> tape = getRecentTapeData();
    
    if (tape.empty()) {
        return true; 
    }

    int startIndex = std::max(0, (int)tape.size() - m_tapeWindow);
    int buyCount = 0;
    int totalCount = 0;

    for (int i = startIndex; i < (int)tape.size(); ++i) {
        if (tape[i].isBuy) {
            buyCount++;
        }
        totalCount++;
    }

    if (totalCount == 0) {
        return false;
    }
    double buyPct = (static_cast<double>(buyCount) / totalCount) * 100.0;
    // Example threshold: at least 60% buys => bullish momentum
    return (buyPct >= 60.0);
}

// Original filter method stubs - implement these based on your specific requirements

bool RingBufferTradeHandler::checkShortPeriodSupertrend(
    const std::vector<time_ordered_tick_buffer::Candle>& candles) {
    // TODO: Implement supertrend calculation
    return true; // Placeholder
}

bool RingBufferTradeHandler::checkVolumeSurge(
    const std::vector<time_ordered_tick_buffer::Candle>& candles) {
    // TODO: Implement volume surge check
    return true; // Placeholder
}

bool RingBufferTradeHandler::checkVWAPDistance(double currentPrice, double vwap) {
    // TODO: Implement VWAP distance check
    return true; // Placeholder
}

bool RingBufferTradeHandler::checkRSI(
    const std::vector<time_ordered_tick_buffer::Candle>& candles) {
    // TODO: Implement RSI with SMA overlay check
    return true; // Placeholder
}

bool RingBufferTradeHandler::checkEmaCross(double ema9, double ema26) {
    return ema9 > ema26; // Simple implementation
}

bool RingBufferTradeHandler::checkAlmaBelowEmas(double alma, double ema9, double ema26) {
    return alma < ema9 && alma < ema26; // Simple implementation
}

// Helper methods to extract data from ring buffer

OrderBookSnapshot RingBufferTradeHandler::getCurrentOrderBook() {
    // TODO: Extract current order book data from ring buffer
    // This should call something like m_tickBuffer->getCurrentOrderBook()
    OrderBookSnapshot ob;
    ob.bestBidPrice = 0.0;
    ob.bestAskPrice = 0.0;
    ob.totalBidSize = 0.0;
    ob.totalAskSize = 0.0;
    return ob; // Placeholder
}

std::vector<TapePrint> RingBufferTradeHandler::getRecentTapeData() {
    // TODO: Extract recent tape/trade data from ring buffer
    // This should call something like m_tickBuffer->getRecentTrades(m_tapeWindow)
    std::vector<TapePrint> tape;
    // You'll need to convert from ring buffer's trade data to TapePrint format
    return tape; // Placeholder - returns empty vector
}

std::vector<MicroCandle> RingBufferTradeHandler::getRecentMicroCandles() {
    // TODO: Extract recent micro-candles from ring buffer
    // This could be very short timeframe candles (e.g., 5-second or 10-second candles)
    // Call something like m_tickBuffer->getMicroCandles(m_microPullbackPeriod)
    std::vector<MicroCandle> microCandles;
    // You'll need to convert from ring buffer's candle data to MicroCandle format
    return microCandles; // Placeholder - returns empty vector
}

std::vector<time_ordered_tick_buffer::Candle> RingBufferTradeHandler::getCandlesFromBuffer() {
    // TODO: Get candles from tick buffer
    // You'll need to implement getRecentCandles() in TimeOrderedTickBuffer first
    return {}; // Placeholder - returns empty vector
}

} // namespace ring_buffer_trade_handler