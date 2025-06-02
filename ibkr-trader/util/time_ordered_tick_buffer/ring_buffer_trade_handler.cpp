#include "ring_buffer_trade_handler.hpp"
#include <format>
#include <iostream>

namespace ring_buffer_trade_handler {

using TI = time_ordered_tick_buffer::TechnicalIndicators;

constexpr double VWAP_MAX_DIST = 0.005;   // 0.5 %
constexpr double SPREAD_MAX    = 0.04;    // $0.04
constexpr int    VOL_LOOKBACK  = 20;

RingBufferTradeHandler::RingBufferTradeHandler(
        time_ordered_tick_buffer::TimeOrderedTickBuffer& b,
        volume_profile_map::VolumeProfileMap&            v,
        raw_data_model::RawDataModel&                    m)
    : m_buf(b), m_vol(v), m_model(m) {}

// ───────────────────────────────────────────────────────────────────────────────
bool RingBufferTradeHandler::evaluate(const stock_data_tick::StockData& tick)
{
    if (!(volSurge() && supertrendBull() && tightSpread() &&
          rsiMomentum() && emaStack() && vwapProximity() && orderBookEdge()))
        return false;

    std::cout << std::format("[RBTH] ✅ trade signal for {} at {:.2f}",
                             tick.symbol, tick.last) << '\n';
    return true;
}

// helper impls ---------------------------------------------------------------
bool RingBufferTradeHandler::volSurge() const
{
    auto &candles = m_buf.getMinuteRing();
    const auto   n = std::min<std::size_t>(candles.size(), VOL_LOOKBACK);
    if (n < 5) return false;

    double volSum = 0.0;
    for (std::size_t i = 0; i < n; ++i) volSum += candles[i].volume;
    double avg = volSum / n;

    return candles.back().volume > avg * 1.5;
}

bool RingBufferTradeHandler::supertrendBull() const
{
    // trivial bullish proxy: close>open on last candle AND last 3 rising
    auto& c = m_buf.getMinuteRing();
    if (c.size() < 4) return false;
    const auto n = c.size()-1;
    return c[n].close>c[n].open && c[n].close>c[n-1].close &&
           c[n-1].close>c[n-2].close && c[n-2].close>c[n-3].close;
}

bool RingBufferTradeHandler::tightSpread() const
{
    stock_data_tick::StockData t;
    return m_model.latestTick(t) && (t.ask - t.bid) <= SPREAD_MAX;
}

bool RingBufferTradeHandler::rsiMomentum() const
{
    TI ind = m_buf.calculateIndicators();
    return ind.rsi > 55.0;          // mild bullish bias
}

bool RingBufferTradeHandler::emaStack() const
{
    TI ind = m_buf.calculateIndicators();
    return ind.ema9 > ind.ema26;
}

bool RingBufferTradeHandler::vwapProximity() const
{
    stock_data_tick::StockData t;
    if (!m_model.latestTick(t)) return false;
    TI ind = m_buf.calculateIndicators();
    double diff = std::fabs(t.last - ind.vwap) / ind.vwap;
    return diff <= VWAP_MAX_DIST;
}

bool RingBufferTradeHandler::orderBookEdge() const
{
    stock_data_tick::StockData t;
    if (!m_model.latestTick(t)) return false;

    // total liquidity within ±0.3 % of last price
    double lo = t.last * 0.997;
    double hi = t.last * 1.003;
    int bidVol = m_vol.get_total_inventory(lo, t.bid);
    int askVol = m_vol.get_total_inventory(t.ask, hi);

    return (bidVol + askVol > 0) && (bidVol > askVol * 1.3);
}
} // namespace ring_buffer_trade_handler
