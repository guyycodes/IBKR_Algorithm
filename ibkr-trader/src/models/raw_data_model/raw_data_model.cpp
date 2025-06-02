#include "raw_data_model.hpp"
#include <chrono>
#include <iostream>

namespace raw_data_model {

using Clock = std::chrono::system_clock;

/*───────────────────────────────────────────────────────────────────────────────
 * TradingParams::from_json                                                     */
bool TradingParams::from_json(const nlohmann::json& js) noexcept
{
    try {
        lots               = js.value("lots",               0);
        margin             = js.value("margin",             0.0);
        stopLossPct        = js.value("stopLoss",           0.0);
        maxTrades          = js.value("maxTrades",          0);
        lossThresholdPct   = js.value("lossThreshold",      0.0);
        winThresholdPct    = js.value("winThreshold",       0.0);
        minWinRatePct      = js.value("minWinRate",         0.0);
        maxHoldSeconds     = js.value("maxHoldSeconds",     0);
        return true;
    }
    catch (...) { return false; }
}

/*───────────────────────────────────────────────────────────────────────────────
 * ctor                                                                         */
RawDataModel::RawDataModel(std::string sym, int intervalMs)
    : m_symbol(std::move(sym))
      // TODO: Implement stk_q later
      // m_queue(std::make_unique<stk_q::STK_Q>())
{
    // TODO: Implement stk_q later
    // m_queue->setIntervalMs(intervalMs);     // make STK_Q resolution configurable
    std::clog << "[RawDataModel] " << m_symbol << " created (" << intervalMs << " ms filter interval)" << '\n';
}

/*───────────────────────────────────────────────────────────────────────────────
 * addTick – single entry-point for new market ticks                            */
void RawDataModel::addTick(const stock_data_tick::StockData& tick)
{
    std::lock_guard lg{m_mx};
    // TODO: Implement stk_q later
    // m_queue->push( convertToQueue(tick, m_symbol) );
    std::clog << "[RawDataModel] Tick received for " << m_symbol << ": last=" << tick.last << '\n';
}

/*───────────────────────────────────────────────────────────────────────────────
 * popNextTick – consumer side (used by ModelManager worker)                    */
// TODO: Implement stk_q later
// bool RawDataModel::popNextTick(stk_q::STK_Q_Data& out)
// {
//     // STK_Q methods are already thread-safe
//     return m_queue->pop(out);
// }

/*───────────────────────────────────────────────────────────────────────────────
 * latestTick – lightweight peek of newest element                              */
bool RawDataModel::latestTick(stock_data_tick::StockData& out) const
{
    // TODO: Implement stk_q later - for now just return false
    return false;
    
    /*
    stk_q::STK_Q_Data qd;
    if (!m_queue->peekLatest(qd)) return false;

    // spin-lock around trivial copy to avoid heavy mutex
    while (m_spinLock.test_and_set(std::memory_order_acquire));
    out.symbol    = m_symbol;
    out.timestamp = static_cast<stock_data_tick::timestamp_t>(qd.time) * 1'000'000; // μs→ns
    out.exchange  = qd.exchange;
    
    out.bid       = qd.bid;
    out.ask       = qd.ask;
    out.last      = qd.last;
    out.bidSize   = qd.bidSize;
    out.askSize   = qd.askSize;
    out.lastSize  = qd.lastSize;
    out.volume    = qd.volume;

    // OHLC data
    out.open      = qd.open;
    out.high      = qd.high;
    out.low       = qd.low;
    out.close     = qd.close;

    // Derived metrics
    out.mid       = qd.mid;
    out.spread    = qd.spread;
    out.spreadPercent = qd.spreadPercent;
    out.vwap      = qd.vwap;
    out.imbalance = qd.imbalance;
    
    // Technical indicators
    out.rsi       = qd.rsi;
    out.ema9      = qd.ema9;
    out.ema26     = qd.ema26;
    out.alma      = qd.alma;
    out.atr       = qd.atr;

    m_spinLock.clear(std::memory_order_release);
    return true;
    */
}

/*───────────────────────────────────────────────────────────────────────────────
 * queueSize / clear                                                            */
std::size_t RawDataModel::queueSize() const noexcept 
{ 
    // TODO: Implement stk_q later
    return 0;
    // return m_queue->size(); 
}

void RawDataModel::clear() noexcept      
{ 
    // TODO: Implement stk_q later
    // m_queue->clear(); 
}

/*───────────────────────────────────────────────────────────────────────────────
 * initialise – load TradingParams from JSON                                    */
std::error_code RawDataModel::initialise(const nlohmann::json& js)
{
    if (!js.contains(m_symbol))               // key missing
        return std::make_error_code(std::errc::invalid_argument);

    const auto& symNode = js.at(m_symbol);
    if (!symNode.contains("params"))
        return std::make_error_code(std::errc::bad_message);

    if (!m_params.from_json(symNode.at("params")))
        return std::make_error_code(std::errc::bad_message);

    return {};
}

/*───────────────────────────────────────────────────────────────────────────────
 * convertToQueue – helper (StockData → STK_Q_Data)                             */
// TODO: Implement stk_q later
/*
stk_q::STK_Q_Data RawDataModel::convertToQueue(const stock_data_tick::StockData& s,
                                               const std::string&                symbol)
{
    stk_q::STK_Q_Data q;
    q.symbol   = symbol;
    q.time     = static_cast<long>(s.timestamp / 1'000'000); // ns → ms
    q.bid      = s.bid;
    q.ask      = s.ask;
    q.last     = s.last;
    q.bidSize  = static_cast<int>(s.bidSize);
    q.askSize  = static_cast<int>(s.askSize);
    q.volume   = static_cast<int>(s.volume);
    q.vwap     = s.vwap;

    // derived metrics that STK_Q consumers might need
    q.mid            = (s.bid + s.ask) * 0.5;
    q.spread         = s.ask - s.bid;
    q.spreadPercent  = (q.mid > 0.0) ? (q.spread / q.mid) * 100.0 : 0.0;
    q.imbalance      = s.imbalance;
    q.rsi            = s.rsi;
    q.ema9           = s.ema9;
    q.ema26          = s.ema26;
    q.alma           = s.alma;
    q.atr            = s.atr;

    // backwards compatibility
    q.price          = q.last;
    q.size           = q.volume;
    return q;
}
*/

} // namespace raw_data_model
