// #pragma once
// #include <pybind11/pybind11.h>
// #include <pybind11/stl.h>
// #include <pybind11/numpy.h>
// #include "../src/models/stock_data_tick/stock_data_tick.hpp"
// #include "../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"
// #include "../util/stk_q/stk_q.hpp"

// namespace py = pybind11;

// // ═══════════════════════════════════════════════════════════════════════════════
// // FAST BRIDGE: C++ ➜ Python for Ring Buffer Data + ML Training Data
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

// // Bind STK_Q_Data for training data
// inline void bind_stk_q_data(py::module_& m)
// {
//     using SQD = stk_q::STK_Q_Data;
//     py::class_<SQD>(m, "STK_Q_Data")
//         .def(py::init<>())
//         .def_readwrite("symbol",    &SQD::symbol)
//         .def_readwrite("time",      &SQD::time)
//         .def_readwrite("last",      &SQD::last)
//         .def_readwrite("bid",       &SQD::bid)
//         .def_readwrite("ask",       &SQD::ask)
//         .def_readwrite("volume",    &SQD::volume)
//         .def_readwrite("bidSize",   &SQD::bidSize)
//         .def_readwrite("askSize",   &SQD::askSize)
//         .def_readwrite("lastSize",  &SQD::lastSize)
//         .def_readwrite("vwap",      &SQD::vwap)
//         .def_readwrite("exchange",  &SQD::exchange)
//         .def_readwrite("mid",       &SQD::mid)
//         .def_readwrite("spread",    &SQD::spread)
//         .def_readwrite("spreadPercent", &SQD::spreadPercent)
//         .def_readwrite("imbalance", &SQD::imbalance)
//         .def_readwrite("rsi",       &SQD::rsi)
//         .def_readwrite("ema9",      &SQD::ema9)
//         .def_readwrite("ema26",     &SQD::ema26)
//         .def_readwrite("alma",      &SQD::alma)
//         .def_readwrite("atr",       &SQD::atr);
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
// // ULTRA-FAST RING BUFFER WRAPPER FOR PYTHON (Real-Time Analysis)
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

// // ═══════════════════════════════════════════════════════════════════════════════
// // ML TRAINING DATA WRAPPER FOR STK_Q (Historical Data Storage)
// // ═══════════════════════════════════════════════════════════════════════════════

// class TrainingDataWrapper {
// public:
//     explicit TrainingDataWrapper(stk_q::STK_Q& stkq)
//         : m_stkq(stkq) {}

//     // Get all historical data for ML training (potentially massive datasets)
//     py::list getAllTrainingData() const {
//         py::list result;
        
//         // Get the entire STK_Q dataset - this could be huge!
//         stk_q::STK_Q_Data data;
        
//         // Note: STK_Q doesn't have an iterator interface, so we'll need to use a different approach
//         // For now, let's provide size and sampling methods
//         return result;
//     }

//     // Get training data within time range (essential for ML)
//     py::list getDataInTimeRange(int64_t startTimeMs, int64_t endTimeMs) const {
//         py::list result;
        
//         // We'd need to add this method to STK_Q for efficient range queries
//         // For now, return placeholder
//         std::cout << "[TrainingDataWrapper] Getting data from " << startTimeMs 
//                   << " to " << endTimeMs << " (not yet implemented)" << std::endl;
        
//         return result;
//     }

//     // Sample data for ML training (e.g., every Nth tick)
//     py::list sampleTrainingData(size_t sampleEveryN) const {
//         py::list result;
        
//         // Implementation would sample every Nth tick from STK_Q
//         std::cout << "[TrainingDataWrapper] Sampling every " << sampleEveryN 
//                   << " ticks (not yet implemented)" << std::endl;
                  
//         return result;
//     }

//     // Get latest N ticks for training
//     py::list getLatestNTicks(size_t n) const {
//         py::list result;
        
//         // For ML, we often need the most recent N ticks
//         for (size_t i = 0; i < n; ++i) {
//             stk_q::STK_Q_Data data;
//             if (m_stkq.peekLatest(data)) {
//                 py::dict tickData;
//                 tickData["symbol"] = data.symbol;
//                 tickData["time"] = data.time;
//                 tickData["last"] = data.last;
//                 tickData["bid"] = data.bid;
//                 tickData["ask"] = data.ask;
//                 tickData["volume"] = data.volume;
//                 tickData["bidSize"] = data.bidSize;
//                 tickData["askSize"] = data.askSize;
//                 tickData["vwap"] = data.vwap;
//                 tickData["exchange"] = data.exchange;
//                 tickData["mid"] = data.mid;
//                 tickData["spread"] = data.spread;
//                 tickData["spreadPercent"] = data.spreadPercent;
//                 tickData["imbalance"] = data.imbalance;
//                 tickData["rsi"] = data.rsi;
//                 tickData["ema9"] = data.ema9;
//                 tickData["ema26"] = data.ema26;
//                 tickData["alma"] = data.alma;
//                 tickData["atr"] = data.atr;
//                 result.append(tickData);
                
//                 // Note: We can only peek at latest, not iterate backwards
//                 // This would need STK_Q enhancement for proper ML data access
//                 break;
//             }
//         }
        
//         return result;
//     }

//     // Get training dataset statistics
//     py::dict getTrainingStats() const {
//         py::dict stats;
//         stats["total_ticks"] = m_stkq.size();
//         stats["is_empty"] = m_stkq.empty();
        
//         // Get time range if data exists
//         stk_q::STK_Q_Data oldest, newest;
//         if (m_stkq.peek(oldest) && m_stkq.peekLatest(newest)) {
//             stats["oldest_timestamp"] = oldest.time;
//             stats["newest_timestamp"] = newest.time;
//             stats["time_span_ms"] = newest.time - oldest.time;
//             stats["time_span_hours"] = (newest.time - oldest.time) / (1000.0 * 60.0 * 60.0);
//         }
        
//         return stats;
//     }

//     // Clear all training data (use with caution!)
//     void clearAllData() {
//         m_stkq.clear();
//         std::cout << "[TrainingDataWrapper] All training data cleared!" << std::endl;
//     }

//     // Remove old training data (memory management)
//     void removeDataOlderThan(int64_t cutoffTimeMs) {
//         size_t sizeBefore = m_stkq.size();
//         m_stkq.removeOlderThan(static_cast<uint64_t>(cutoffTimeMs));
//         size_t sizeAfter = m_stkq.size();
        
//         std::cout << "[TrainingDataWrapper] Pruned " << (sizeBefore - sizeAfter) 
//                   << " old training ticks, " << sizeAfter << " remaining" << std::endl;
//     }

// private:
//     stk_q::STK_Q& m_stkq;
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

// inline void bind_training_data_wrapper(py::module_& m)
// {
//     py::class_<TrainingDataWrapper>(m, "TrainingDataWrapper")
//         .def(py::init<stk_q::STK_Q&>())
//         .def("getAllTrainingData", &TrainingDataWrapper::getAllTrainingData)
//         .def("getDataInTimeRange", &TrainingDataWrapper::getDataInTimeRange)
//         .def("sampleTrainingData", &TrainingDataWrapper::sampleTrainingData)
//         .def("getLatestNTicks", &TrainingDataWrapper::getLatestNTicks)
//         .def("getTrainingStats", &TrainingDataWrapper::getTrainingStats)
//         .def("clearAllData", &TrainingDataWrapper::clearAllData)
//         .def("removeDataOlderThan", &TrainingDataWrapper::removeDataOlderThan);
// } 