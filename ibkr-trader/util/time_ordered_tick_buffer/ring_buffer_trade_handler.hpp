#pragma once
#include "../../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"
#include "../../src/models/volume_profile/volume_profile_map.hpp"
#include "../../src/models/raw_data_model/raw_data_model.hpp"

namespace ring_buffer_trade_handler {

class RingBufferTradeHandler {
public:
    RingBufferTradeHandler(time_ordered_tick_buffer::TimeOrderedTickBuffer& buf,
                           volume_profile_map::VolumeProfileMap&           vol,
                           raw_data_model::RawDataModel&                   model);

    [[nodiscard]] bool evaluate(const stock_data_tick::StockData& tick);

    // Ultra-low latency ring buffer monitoring (15-second real-time monitoring)
    void monitorRingBuffersRealTime();
    void printMinuteRing();
    void printCandleRing(); 
    void printPriceRing();

private:
    // helpers return true when the filter passes (commented out for ring buffer focus)
    [[nodiscard]] bool volSurge()        const;
    [[nodiscard]] bool supertrendBull()  const;
    [[nodiscard]] bool tightSpread()     const;
    [[nodiscard]] bool rsiMomentum()     const;
    [[nodiscard]] bool emaStack()        const;
    [[nodiscard]] bool vwapProximity()   const;
    [[nodiscard]] bool orderBookEdge()   const;

    double  m_prevCloseForATR      = std::numeric_limits<double>::quiet_NaN();
    double  m_atr                  = std::numeric_limits<double>::quiet_NaN();
    int     m_atrWarmupCount       = 0;

    time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buf;
    volume_profile_map::VolumeProfileMap&            m_vol;
    raw_data_model::RawDataModel&                    m_model;
};
} // namespace ring_buffer_trade_handler
