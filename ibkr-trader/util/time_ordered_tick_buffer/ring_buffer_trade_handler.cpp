#include "ring_buffer_trade_handler.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

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

// ═══════════════════════════════════════════════════════════════════════════════
// ULTRA-LOW LATENCY RING BUFFER MONITORING SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════

void RingBufferTradeHandler::monitorRingBuffersRealTime() {
    std::cout << "\n🚀 [ULTRA-LOW LATENCY] Starting 15-second ring buffer monitoring...\n";
    std::cout << "📊 Reading 3 ring buffers in real-time:\n";
    std::cout << "   1️⃣  Minute Ring (TemporaryCandle aggregation)\n";
    std::cout << "   2️⃣  Candle Ring (Completed 1-min candles)\n"; 
    std::cout << "   3️⃣  Price Ring (ALMA calculation buffer)\n\n";
    
    auto startTime = std::chrono::steady_clock::now();
    auto endTime = startTime + std::chrono::seconds(15);
    
    int iteration = 0;
    while (std::chrono::steady_clock::now() < endTime) {
        std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "📸 [SNAPSHOT #" << ++iteration << "] Ring Buffer Contents:\n";
        
        // Read all 3 ring buffers with ultra-low latency
        printMinuteRing();
        printCandleRing(); 
        printPriceRing();
        
        // Ultra-fast 100ms polling for real-time updates
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\n✅ [MONITORING COMPLETE] 15-second ring buffer monitoring finished!\n";
    std::cout << "📈 Total snapshots captured: " << iteration << "\n\n";
}

void RingBufferTradeHandler::printMinuteRing() {
    const auto& minuteRing = m_buf.getMinuteRing();
    const auto& minuteIndices = m_buf.getMinuteIndices();
    
    std::cout << "\n1️⃣  MINUTE RING (Tick Aggregation Buffer):\n";
    std::cout << "   Size: " << minuteRing.size() << " slots | Window: " << m_buf.getWindowMinutes() << " minutes\n";
    
    int validSlots = 0;
    for (size_t i = 0; i < minuteRing.size() && i < 5; ++i) {  // Show first 5 slots
        if (minuteIndices[i] != -1 && !minuteRing[i].isEmpty()) {
            validSlots++;
            std::cout << "   📦 Slot[" << i << "] Minute:" << minuteIndices[i] 
                      << " | OHLCV: " << std::fixed << std::setprecision(2)
                      << minuteRing[i].open << "/" << minuteRing[i].high << "/"
                      << minuteRing[i].low << "/" << minuteRing[i].close 
                      << " Vol:" << (int)minuteRing[i].volume << "\n";
        }
    }
    if (validSlots == 0) {
        std::cout << "   💤 No active minute aggregations\n";
    }
}

void RingBufferTradeHandler::printCandleRing() {
    const auto& candleRing = m_buf.getCandleRing();
    size_t head = m_buf.getCandleRingHead();
    size_t count = m_buf.getCandleRingCount();
    
    std::cout << "\n2️⃣  CANDLE RING (Completed Candles Buffer):\n";
    std::cout << "   Size: " << candleRing.size() << " slots | Head: " << head 
              << " | Valid: " << count << "\n";
    
    if (count == 0) {
        std::cout << "   💤 No completed candles yet\n";
        return;
    }
    
    // Show last 3 completed candles (chronological order)
    size_t showCount = std::min<size_t>(3, count);
    for (size_t i = 0; i < showCount; ++i) {
        // Calculate index: start from head-count, then move forward
        size_t idx = (head + candleRing.size() - count + i) % candleRing.size();
        const auto& candle = candleRing[idx];
        
        std::cout << "   🕯️  Candle[" << idx << "] @ " << candle.timestamp/1000 
                  << " | OHLCV: " << std::fixed << std::setprecision(2)
                  << candle.open << "/" << candle.high << "/"
                  << candle.low << "/" << candle.close 
                  << " Vol:" << (int)candle.volume << "\n";
    }
}

void RingBufferTradeHandler::printPriceRing() {
    const auto& priceRing = m_buf.getPriceRing();
    size_t head = m_buf.getPriceRingHead();
    size_t count = m_buf.getPriceRingCount();
    
    std::cout << "\n3️⃣  PRICE RING (ALMA Calculation Buffer):\n";
    std::cout << "   Size: " << priceRing.size() << " slots | Head: " << head 
              << " | Valid: " << count << "\n";
    
    if (count == 0) {
        std::cout << "   💤 No prices yet\n";
        return;
    }
    
    // Show last 5 prices (chronological order)
    size_t showCount = std::min<size_t>(5, count);
    std::cout << "   💰 Recent Prices: ";
    for (size_t i = 0; i < showCount; ++i) {
        // Calculate index: start from head-count, then move forward
        size_t idx = (head + priceRing.size() - count + i) % priceRing.size();
        std::cout << std::fixed << std::setprecision(2) << priceRing[idx];
        if (i < showCount - 1) std::cout << " → ";
    }
    std::cout << "\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
// ORIGINAL TRADING LOGIC (COMMENTED OUT FOR RING BUFFER FOCUS)
// ═══════════════════════════════════════════════════════════════════════════════

// ───────────────────────────────────────────────────────────────────────────────
bool RingBufferTradeHandler::evaluate(const stock_data_tick::StockData& tick)
{
    // SIMPLIFIED: Just call the ring buffer monitor instead of complex trading logic
    // Comment out the original complex evaluation:
    /*
    if (!(volSurge() && supertrendBull() && tightSpread() &&
          rsiMomentum() && emaStack() && vwapProximity() && orderBookEdge()))
        return false;

    std::cout << "[RBTH] ✅ trade signal for " << tick.symbol << " at " 
              << std::fixed << std::setprecision(2) << tick.last << '\n';
    */
    
    // For now, just trigger monitoring on first call
    static bool hasStartedMonitoring = false;
    if (!hasStartedMonitoring) {
        hasStartedMonitoring = true;
        std::thread monitorThread(&RingBufferTradeHandler::monitorRingBuffersRealTime, this);
        monitorThread.detach(); // Let it run independently
    }
    
    return false; // Don't generate trade signals, just monitor
}

// Original helper methods (commented out to focus on ring buffer monitoring)
/*
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
*/

} // namespace ring_buffer_trade_handler
