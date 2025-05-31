#ifndef RING_BUFFER_TRADE_HANDLER_HPP
#define RING_BUFFER_TRADE_HANDLER_HPP

#include "time_ordered_tick_buffer.hpp"
#include "../../models/metrics_model/stock_data_tick.hpp"

namespace ring_buffer_trade_handler {

class RingBufferTradeHandler {
public:
    explicit RingBufferTradeHandler(time_ordered_tick_buffer::TimeOrderedTickBuffer* tickBuffer) 
        : m_tickBuffer(tickBuffer) {}
    
    bool checkForTradeOpportunity(const stock_data_tick::StockData& newTick);
    
private:
    time_ordered_tick_buffer::TimeOrderedTickBuffer* m_tickBuffer;
    static constexpr int64_t MS_PER_MINUTE = 60 * 1000;
};

} // namespace ring_buffer_trade_handler

#endif // RING_BUFFER_TRADE_HANDLER_HPP 