// ═══════════════════════════════════════════════════════════════════════════════
// SIMPLIFIED BINDINGS: Candle Ring + STK_Q Only for now
// bindings.hpp is used to bind the C++ code to Python
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
// #include "../src/models/stock_data_tick/stock_data_tick.hpp"
#include "../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"
#include "../util/stk_q/stk_q.hpp"

namespace py = pybind11;

// ═══════════════════════════════════════════════════════════════════════════════
// SIMPLIFIED BRIDGE: Candle Ring + STK_Q Only
// ═══════════════════════════════════════════════════════════════════════════════

// Comment out unused bindings
/*
inline void bind_stockdata(py::module_& m)
{
    using SD = stock_data_tick::StockData;
    py::class_<SD>(m, "StockData")
        .def(py::init<>())
        .def_readwrite("symbol",    &SD::symbol)
        .def_readwrite("timestamp", &SD::timestamp)
        .def_readwrite("last",      &SD::last)
        .def_readwrite("bid",       &SD::bid)
        .def_readwrite("ask",       &SD::ask)
        .def_readwrite("volume",    &SD::volume)
        .def_readwrite("bidSize",   &SD::bidSize)
        .def_readwrite("askSize",   &SD::askSize)
        .def_readwrite("lastSize",  &SD::lastSize)
        .def_readwrite("vwap",      &SD::vwap)
        .def_readwrite("exchange",  &SD::exchange);
}
*/

// Bind STK_Q_Data for training data
inline void bind_stk_q_data(py::module_& m)
{
    using SQD = stk_q::STK_Q_Data;
    py::class_<SQD>(m, "STK_Q_Data")
        .def(py::init<>())
        .def_readwrite("symbol",    &SQD::symbol)
        .def_readwrite("time",      &SQD::time)
        .def_readwrite("last",      &SQD::last)
        .def_readwrite("bid",       &SQD::bid)
        .def_readwrite("ask",       &SQD::ask)
        .def_readwrite("volume",    &SQD::volume)
        .def_readwrite("bidSize",   &SQD::bidSize)
        .def_readwrite("askSize",   &SQD::askSize)
        .def_readwrite("lastSize",  &SQD::lastSize)
        .def_readwrite("vwap",      &SQD::vwap)
        .def_readwrite("exchange",  &SQD::exchange)
        .def_readwrite("mid",       &SQD::mid)
        .def_readwrite("spread",    &SQD::spread)
        .def_readwrite("spreadPercent", &SQD::spreadPercent)
        .def_readwrite("imbalance", &SQD::imbalance)
        .def_readwrite("rsi",       &SQD::rsi)
        .def_readwrite("ema9",      &SQD::ema9)
        .def_readwrite("ema26",     &SQD::ema26)
        .def_readwrite("alma",      &SQD::alma)
        .def_readwrite("atr",       &SQD::atr);
}

/*
inline void bind_temporary_candle(py::module_& m)
{
    using TC = time_ordered_tick_buffer::TemporaryCandle;
    py::class_<TC>(m, "TemporaryCandle")
        .def(py::init<>())
        .def_readwrite("open",   &TC::open)
        .def_readwrite("high",   &TC::high)
        .def_readwrite("low",    &TC::low)
        .def_readwrite("close",  &TC::close)
        .def_readwrite("volume", &TC::volume)
        .def_readwrite("empty",  &TC::empty)
        .def("isEmpty", &TC::isEmpty);
}
*/

inline void bind_candle(py::module_& m)
{
    using C = time_ordered_tick_buffer::Candle;
    py::class_<C>(m, "Candle")
        .def(py::init<>())
        .def(py::init<double, double, double, double, double, int64_t>())
        .def_readwrite("open",      &C::open)
        .def_readwrite("high",      &C::high)
        .def_readwrite("low",       &C::low)
        .def_readwrite("close",     &C::close)
        .def_readwrite("volume",    &C::volume)
        .def_readwrite("timestamp", &C::timestamp);
}

/*
inline void bind_technical_indicators(py::module_& m)
{
    using TI = time_ordered_tick_buffer::TechnicalIndicators;
    py::class_<TI>(m, "TechnicalIndicators")
        .def(py::init<>())
        .def_readwrite("vwap",  &TI::vwap)
        .def_readwrite("rsi",   &TI::rsi)
        .def_readwrite("ema9",  &TI::ema9)
        .def_readwrite("ema26", &TI::ema26)
        .def_readwrite("alma",  &TI::alma)
        .def_readwrite("atr",   &TI::atr)
        .def("isValid", &TI::isValid);
}
*/

// ═══════════════════════════════════════════════════════════════════════════════
// SIMPLIFIED CANDLE RING WRAPPER (Completed 1-min candles only)
// ═══════════════════════════════════════════════════════════════════════════════

class CandleRingWrapper {
public:
    explicit CandleRingWrapper(time_ordered_tick_buffer::TimeOrderedTickBuffer& buffer)
        : m_buffer(buffer) {}

    // Get completed candles ring (THIS IS WHAT WE NEED)
    py::list getCandleRingData() const {
        const auto& candleRing = m_buffer.getCandleRing();
        size_t head = m_buffer.getCandleRingHead();
        size_t count = m_buffer.getCandleRingCount();
        
        py::list result;
        if (count > 0) {
            // Return in chronological order
            for (size_t i = 0; i < count; ++i) {
                size_t idx = (head + candleRing.size() - count + i) % candleRing.size();
                py::dict candleData;
                candleData["index"] = idx;
                candleData["candle"] = candleRing[idx];
                result.append(candleData);
            }
        }
        return result;
    }

    // Get candle ring stats
    py::dict getCandleRingStats() const {
        py::dict stats;
        stats["candle_ring_head"] = m_buffer.getCandleRingHead();
        stats["candle_ring_count"] = m_buffer.getCandleRingCount();
        stats["window_minutes"] = m_buffer.getWindowMinutes();
        return stats;
    }

private:
    time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buffer;
};

// ═══════════════════════════════════════════════════════════════════════════════
// STK_Q WRAPPER (Historical Data Storage)
// ═══════════════════════════════════════════════════════════════════════════════

class STK_Q_Wrapper {
public:
    explicit STK_Q_Wrapper(stk_q::STK_Q& stkq)
        : m_stkq(stkq) {}

    // Get latest N ticks
    py::list getLatestNTicks(size_t n) const {
        py::list result;
        
        stk_q::STK_Q_Data data;
        if (m_stkq.peekLatest(data)) {
            py::dict tickData;
            tickData["symbol"] = data.symbol;
            tickData["time"] = data.time;
            tickData["last"] = data.last;
            tickData["bid"] = data.bid;
            tickData["ask"] = data.ask;
            tickData["volume"] = data.volume;
            tickData["bidSize"] = data.bidSize;
            tickData["askSize"] = data.askSize;
            tickData["vwap"] = data.vwap;
            tickData["exchange"] = data.exchange;
            tickData["mid"] = data.mid;
            tickData["spread"] = data.spread;
            tickData["spreadPercent"] = data.spreadPercent;
            tickData["imbalance"] = data.imbalance;
            tickData["rsi"] = data.rsi;
            tickData["ema9"] = data.ema9;
            tickData["ema26"] = data.ema26;
            tickData["alma"] = data.alma;
            tickData["atr"] = data.atr;
            result.append(tickData);
        }
        
        return result;
    }

    // Get STK_Q stats
    py::dict getSTK_Q_Stats() const {
        py::dict stats;
        stats["total_ticks"] = m_stkq.size();
        stats["is_empty"] = m_stkq.empty();
        
        // Get time range if data exists
        stk_q::STK_Q_Data oldest, newest;
        if (m_stkq.peek(oldest) && m_stkq.peekLatest(newest)) {
            stats["oldest_timestamp"] = oldest.time;
            stats["newest_timestamp"] = newest.time;
            stats["time_span_ms"] = newest.time - oldest.time;
        }
        
        return stats;
    }

    // Clear all data
    void clearAllData() {
        m_stkq.clear();
    }

    // Remove old data
    void removeDataOlderThan(int64_t cutoffTimeMs) {
        m_stkq.removeOlderThan(static_cast<uint64_t>(cutoffTimeMs));
    }

private:
    stk_q::STK_Q& m_stkq;
};

inline void bind_candle_ring_wrapper(py::module_& m)
{
    py::class_<CandleRingWrapper>(m, "CandleRingWrapper")
        .def(py::init<time_ordered_tick_buffer::TimeOrderedTickBuffer&>())
        .def("getCandleRingData", &CandleRingWrapper::getCandleRingData)
        .def("getCandleRingStats", &CandleRingWrapper::getCandleRingStats);
}

inline void bind_stk_q_wrapper(py::module_& m)
{
    py::class_<STK_Q_Wrapper>(m, "STK_Q_Wrapper")
        .def(py::init<stk_q::STK_Q&>())
        .def("getLatestNTicks", &STK_Q_Wrapper::getLatestNTicks)
        .def("getSTK_Q_Stats", &STK_Q_Wrapper::getSTK_Q_Stats)
        .def("clearAllData", &STK_Q_Wrapper::clearAllData)
        .def("removeDataOlderThan", &STK_Q_Wrapper::removeDataOlderThan);
} 