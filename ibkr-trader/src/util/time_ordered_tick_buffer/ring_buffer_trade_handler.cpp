#include "ring_buffer_trade_handler.hpp"
#include <cmath>
#include <iostream>

namespace ring_buffer_trade_handler {

bool RingBufferTradeHandler::checkForTradeOpportunity(const stock_data_tick::StockData& newTick) {
    const auto& minuteRing = m_tickBuffer->getMinuteRing();
    const auto& minuteIndices = m_tickBuffer->getMinuteIndices();
    
    int64_t currentMinute = newTick.timestamp / MS_PER_MINUTE;
    size_t currentSlot = currentMinute % m_tickBuffer->getWindowMinutes();
    
    // Check if we have data for current minute
    if (minuteIndices[currentSlot] != currentMinute || minuteRing[currentSlot].isEmpty()) {
        return false;
    }
    
    const time_ordered_tick_buffer::TemporaryCandle& currentCandle = minuteRing[currentSlot];
    
    // Simple trade checks:
    
    // 1. Volume spike (current volume > 2x typical)
    if (currentCandle.volume > 1000.0) {  // Basic volume threshold
        double priceMove = std::abs(newTick.last - currentCandle.open) / currentCandle.open;
        if (priceMove > 0.005) {  // 0.5% price move with volume
            return true;
        }
    }
    
    // 2. Large price movement in current minute
    if (!currentCandle.isEmpty()) {
        double range = (currentCandle.high - currentCandle.low) / currentCandle.open;
        if (range > 0.01) {  // 1% range
            return true;
        }
    }
    
    return false;
}

} // namespace ring_buffer_trade_handler 