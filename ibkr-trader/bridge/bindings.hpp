// #pragma once
// #include <pybind11/pybind11.h>
// #include <pybind11/stl.h>
// #include <pybind11/numpy.h>
// #include "../src/models/stock_data_tick/stock_data_tick.hpp"
// #include "../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"

// namespace py = pybind11;

// // ═══════════════════════════════════════════════════════════════════════════════
// // FAST BRIDGE: C++ ➜ Python for Ring Buffer Data
// // ═══════════════════════════════════════════════════════════════════════════════

// inline void bind_stockdata(py::module_& m)
// {
//     using SD = stock_data_tick::StockData;
//     py::class_<SD>(m, "StockData")
//         .def(py::init<>())
//         .def_readwrite("symbol",    &SD::symbol)
//         .def_readwrite("timestamp", &SD::timestamp)
//         .def_readwrite("last",      &SD::last)
//         .def_readwrite("bid",       &SD::bid)
//         .def_readwrite("ask",       &SD::ask)
//         .def_readwrite("volume",    &SD::volume)
//         .def_readwrite("bidSize",   &SD::bidSize)
//         .def_readwrite("askSize",   &SD::askSize)
//         .def_readwrite("lastSize",  &SD::lastSize)
//         .def_readwrite("vwap",      &SD::vwap)
//         .def_readwrite("exchange",  &SD::exchange);
// }

// inline void bind_temporary_candle(py::module_& m)
// {
//     using TC = time_ordered_tick_buffer::TemporaryCandle;
//     py::class_<TC>(m, "TemporaryCandle")
//         .def(py::init<>())
//         .def_readwrite("open",   &TC::open)
//         .def_readwrite("high",   &TC::high)
//         .def_readwrite("low",    &TC::low)
//         .def_readwrite("close",  &TC::close)
//         .def_readwrite("volume", &TC::volume)
//         .def_readwrite("empty",  &TC::empty)
//         .def("isEmpty", &TC::isEmpty);
// }

// inline void bind_candle(py::module_& m)
// {
//     using C = time_ordered_tick_buffer::Candle;
//     py::class_<C>(m, "Candle")
//         .def(py::init<>())
//         .def(py::init<double, double, double, double, double, int64_t>())
//         .def_readwrite("open",      &C::open)
//         .def_readwrite("high",      &C::high)
//         .def_readwrite("low",       &C::low)
//         .def_readwrite("close",     &C::close)
//         .def_readwrite("volume",    &C::volume)
//         .def_readwrite("timestamp", &C::timestamp);
// }

// inline void bind_technical_indicators(py::module_& m)
// {
//     using TI = time_ordered_tick_buffer::TechnicalIndicators;
//     py::class_<TI>(m, "TechnicalIndicators")
//         .def(py::init<>())
//         .def_readwrite("vwap",  &TI::vwap)
//         .def_readwrite("rsi",   &TI::rsi)
//         .def_readwrite("ema9",  &TI::ema9)
//         .def_readwrite("ema26", &TI::ema26)
//         .def_readwrite("alma",  &TI::alma)
//         .def_readwrite("atr",   &TI::atr)
//         .def("isValid", &TI::isValid);
// }

// // ═══════════════════════════════════════════════════════════════════════════════
// // ULTRA-FAST RING BUFFER WRAPPER FOR PYTHON
// // ═══════════════════════════════════════════════════════════════════════════════

// class RingBufferWrapper {
// public:
//     explicit RingBufferWrapper(time_ordered_tick_buffer::TimeOrderedTickBuffer& buffer)
//         : m_buffer(buffer) {}

//     // Fast access to minute ring (tick aggregation)
//     py::list getMinuteRingData() const {
//         const auto& minuteRing = m_buffer.getMinuteRing();
//         const auto& minuteIndices = m_buffer.getMinuteIndices();
        
//         py::list result;
//         for (size_t i = 0; i < minuteRing.size(); ++i) {
//             if (minuteIndices[i] != -1 && !minuteRing[i].isEmpty()) {
//                 py::dict slot;
//                 slot["slot"] = i;
//                 slot["minute"] = minuteIndices[i];
//                 slot["candle"] = minuteRing[i];
//                 result.append(slot);
//             }
//         }
//         return result;
//     }

//     // Fast access to completed candles ring
//     py::list getCandleRingData() const {
//         const auto& candleRing = m_buffer.getCandleRing();
//         size_t head = m_buffer.getCandleRingHead();
//         size_t count = m_buffer.getCandleRingCount();
        
//         py::list result;
//         if (count > 0) {
//             // Return in chronological order
//             for (size_t i = 0; i < count; ++i) {
//                 size_t idx = (head + candleRing.size() - count + i) % candleRing.size();
//                 py::dict candleData;
//                 candleData["index"] = idx;
//                 candleData["candle"] = candleRing[idx];
//                 result.append(candleData);
//             }
//         }
//         return result;
//     }

//     // Fast access to price ring (ALMA buffer)
//     py::list getPriceRingData() const {
//         const auto& priceRing = m_buffer.getPriceRing();
//         size_t head = m_buffer.getPriceRingHead();
//         size_t count = m_buffer.getPriceRingCount();
        
//         py::list result;
//         if (count > 0) {
//             // Return in chronological order
//             for (size_t i = 0; i < count; ++i) {
//                 size_t idx = (head + priceRing.size() - count + i) % priceRing.size();
//                 result.append(priceRing[idx]);
//             }
//         }
//         return result;
//     }

//     // Get all ring buffer stats
//     py::dict getRingBufferStats() const {
//         py::dict stats;
//         stats["minute_ring_size"] = m_buffer.getMinuteRing().size();
//         stats["window_minutes"] = m_buffer.getWindowMinutes();
//         stats["candle_ring_head"] = m_buffer.getCandleRingHead();
//         stats["candle_ring_count"] = m_buffer.getCandleRingCount();
//         stats["price_ring_head"] = m_buffer.getPriceRingHead();
//         stats["price_ring_count"] = m_buffer.getPriceRingCount();
//         return stats;
//     }

//     // Get latest technical indicators
//     time_ordered_tick_buffer::TechnicalIndicators getIndicators() const {
//         return m_buffer.calculateIndicators();
//     }

// private:
//     time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buffer;
// };

// inline void bind_ring_buffer_wrapper(py::module_& m)
// {
//     py::class_<RingBufferWrapper>(m, "RingBufferWrapper")
//         .def(py::init<time_ordered_tick_buffer::TimeOrderedTickBuffer&>())
//         .def("getMinuteRingData", &RingBufferWrapper::getMinuteRingData)
//         .def("getCandleRingData", &RingBufferWrapper::getCandleRingData)
//         .def("getPriceRingData", &RingBufferWrapper::getPriceRingData)
//         .def("getRingBufferStats", &RingBufferWrapper::getRingBufferStats)
//         .def("getIndicators", &RingBufferWrapper::getIndicators);
// } 