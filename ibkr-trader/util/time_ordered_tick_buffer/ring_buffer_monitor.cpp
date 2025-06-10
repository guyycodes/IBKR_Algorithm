#include "ring_buffer_monitor.hpp"
#include <sstream>

namespace ring_buffer_trade_handler
{

RingBufferMonitor::RingBufferMonitor(time_ordered_tick_buffer::TimeOrderedTickBuffer& buf,
                                     RingBufferTradeHandler&                          handler)
    : m_buf(buf)
    , m_handler(handler)
{}

/*----------------------------------------------------------------------*/

RingBufferMonitor::~RingBufferMonitor()
{
    stop();        // guarantees a clean exit
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::start(std::chrono::seconds maxDuration)
{
    if (m_running.exchange(true)) {       // already running
        std::cerr << "[RingBufferMonitor] ⚠️  Requested start(), but thread is already active\n";
        return;
    }

    m_thread = std::thread(&RingBufferMonitor::monitorLoop, this, maxDuration);
    std::cout << "[RingBufferMonitor] 🚀 Logging thread started\n";
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::stop()
{
    if (!m_running.exchange(false))       // already stopped
        return;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_cv.notify_all();                    // wake the worker if it is waiting
    }

    if (m_thread.joinable())
        m_thread.join();

    std::cout << "[RingBufferMonitor] ✅ Logging thread joined\n";
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::requestSnapshot() noexcept
{
    // If the monitor is active wake it; else do nothing.
    if (m_running.load()) {
        m_snapshotRequested.store(true, std::memory_order_relaxed);
        m_cv.notify_one();
    }
}

/*----------------------------------------------------------------------*/

inline void RingBufferMonitor::emplaceKalmanTicks(const time_ordered_tick_buffer::Tick& src) noexcept
{
    // TODO: Complete tick ingestion hook implementation
    // 1. Fix method signature: change from time_ordered_tick_buffer::Tick to stock_data_tick::StockData
    // 2. Find appropriate hook location in TimeOrderedTickBuffer::addTick() method
    // 3. Add call to monitor->emplaceKalmanTicks(tick) in the tick processing pipeline
    // 4. Verify ≤30ns performance target for all three buffer pushes
    // 5. Test with real market data to ensure correct frequency distribution
    
    // Create KalmanTick with current timestamp and source price
    KalmanTick k{std::chrono::steady_clock::now(), src.price};
    
    // Push to all three buffers - no locks, just three pointer bumps
    m_kalman1m.push(k);   // 1-minute buffer (2Hz target)
    m_kalman5m.push(k);   // 5-minute buffer (1Hz target)  
    m_kalman20m.push(k);  // 20-minute buffer (1Hz target)
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::monitorLoop(std::chrono::seconds maxDuration) noexcept
{
    try {
        auto deadline   = std::chrono::steady_clock::now() + maxDuration;
        int  iteration  = 0;

        std::unique_lock<std::mutex> lk(m_mutex);

        std::cout << "\n🚀 [RingBufferMonitor] Real‑time monitoring engaged\n";

        while (m_running.load() && std::chrono::steady_clock::now() < deadline)
        {
            // Wait either for an external wake‑up (requestSnapshot) or for 100 ms.
            m_cv.wait_for(lk, std::chrono::milliseconds{60000}, // 1 minute
                          [this] { return !m_running.load() || m_snapshotRequested.exchange(false); });

            if (!m_running.load())
                break;

            // ----- produce snapshot (outside the lock, keep wait‑time short) -----
            lk.unlock();

            // Load snapshot once per iteration
            auto snap = m_buf.getSnapshot();
            if (!snap) {
                std::cout << "[RingBufferMonitor] (no snapshot yet)\n";
                lk.lock();
                continue;
            }

            try {
                std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
                std::cout << "📸 [SNAPSHOT #" << ++iteration << "] Ring Buffer Contents:\n";

                printMinuteRing(*snap);
                printCandleRing(*snap);
                printPriceRing(*snap);
                printTechnicalIndicators();
                
                // Print Kalman ring buffer snapshots
                printKalmanRing("KALMAN 1-MINUTE BUFFER", m_kalman1m, 5);
                printKalmanRing("KALMAN 5-MINUTE BUFFER", m_kalman5m, 5);
                printKalmanRing("KALMAN 20-MINUTE BUFFER", m_kalman20m, 5);
            } catch (const std::exception& e) {
                std::cerr << "[RingBufferMonitor] ⚠️ Exception in snapshot: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[RingBufferMonitor] ⚠️ Unknown exception in snapshot\n";
            }

            lk.lock();
        }

        std::cout << "\n🛑 [RingBufferMonitor] Monitoring finished ("
                  << iteration << " snapshots)\n";
    }
    catch (const std::exception& ex) {
        std::cerr << "[RingBufferMonitor] Uncaught exception: "
                  << ex.what() << '\n';
    }
}

/*----------------------------------------------------------------------*
 *                                Helpers                               *
 *----------------------------------------------------------------------*/

void RingBufferMonitor::printMinuteRing(
        const time_ordered_tick_buffer::MonitorSnapshot& s) const
{
    std::cout << "\n1️⃣  MINUTE RING (snapshot):\n";
    std::cout << "   Size: " << s.minuteRing.size() << '\n';

    int valid = 0;
    for (size_t i = 0; i < s.minuteRing.size(); ++i) {
        if (s.minuteIdx[i] != -1 && !s.minuteRing[i].isEmpty()) {
            ++valid;
            const auto& c = s.minuteRing[i];
            std::cout << "   📦 Slot[" << i << "] Minute:" << s.minuteIdx[i]
                      << " | OHLCV: " << std::fixed << std::setprecision(2)
                      << c.open << '/' << c.high << '/'
                      << c.low  << '/' << c.close
                      << " Vol:" << int(c.volume) << '\n';
        }
    }
    if (valid == 0) std::cout << "   💤 No active minute aggregations\n";
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::printCandleRing(
        const time_ordered_tick_buffer::MonitorSnapshot& s) const
{
    std::cout << "\n2️⃣  CANDLE RING (snapshot):\n";
    std::cout << "   Size: " << s.candleRing.size()
              << " slots | Head: " << s.candleHead
              << " | Valid: " << s.candleCount << '\n';

    if (s.candleCount == 0)
    {
        std::cout << "   💤 No completed candles yet\n";
        return;
    }

    const std::size_t show = std::min<std::size_t>(3, s.candleCount);
    for (std::size_t i = 0; i < show; ++i)
    {
        std::size_t idx = (s.candleHead + s.candleRing.size() - s.candleCount + i) % s.candleRing.size();
        const auto& c   = s.candleRing[idx];

        std::cout << "   🕯️  Candle[" << idx << "] @" << c.timestamp
                  << " ms (" << (c.timestamp / 1000) << " s)"
                  << " | OHLCV: " << std::fixed << std::setprecision(2)
                  << c.open << "/" << c.high << "/"
                  << c.low  << "/" << c.close
                  << " Vol:" << static_cast<int>(c.volume) << '\n';
    }
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::printPriceRing(
        const time_ordered_tick_buffer::MonitorSnapshot& s) const
{
    std::cout << "\n3️⃣  PRICE RING (snapshot):\n";
    std::cout << "   Size: " << s.priceRing.size()
              << " slots | Head: " << s.priceHead
              << " | Valid: " << s.priceCount << '\n';

    if (s.priceCount == 0)
    {
        std::cout << "   💤 No prices yet\n";
        return;
    }

    const std::size_t show = std::min<std::size_t>(5, s.priceCount);
    std::cout << "   💰 Recent Prices: ";
    for (std::size_t i = 0; i < show; ++i)
    {
        std::size_t idx = (s.priceHead + s.priceRing.size() - s.priceCount + i) % s.priceRing.size();
        std::cout << std::fixed << std::setprecision(2) << s.priceRing[idx];
        if (i < show - 1) std::cout << " → ";
    }
    std::cout << '\n';
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::printTechnicalIndicators() const
{
    const auto ind = m_handler.computeIndicatorsFromCandles();
    auto snap = m_buf.getSnapshot();

    auto fmt = [](double v, int p = 4) {
        std::ostringstream os; 
        os << std::fixed << std::setprecision(p) << v;
        return os.str();
    };

    std::cout << "\n📊 TECHNICAL INDICATORS:\n";

    // --- VWAP (always valid once we have ≥1 candle) ---
    std::cout << "   💎 VWAP:  $" << (std::isfinite(ind.vwap) ? fmt(ind.vwap) : "n/a") << '\n';

    // --- EMA 9 & 26 with readiness banner -------------
    std::cout << "   📈 EMA9:  ";
    if (ind.ema9Ready) {
        std::cout << '$' << fmt(ind.ema9);
    } else {
        std::cout << "(warming " << (snap ? snap->candleCount : 0) << "/9)";
    }
    std::cout << '\n';

    std::cout << "   📈 EMA26: ";
    if (ind.ema26Ready) {
        std::cout << '$' << fmt(ind.ema26);
    } else {
        std::cout << "(warming " << (snap ? snap->candleCount : 0) << "/26)";
    }
    std::cout << '\n';

    // --- Other indicators (same pattern optional) -----
    std::cout << "   ⚡ RSI:   " << (std::isfinite(ind.rsi) ? fmt(ind.rsi, 1) + "%" : "n/a") << '\n'
              << "   📏 ATR:   " << (std::isfinite(ind.atr) ? '$' + fmt(ind.atr) : "n/a") << '\n';

    std::cout << "   🎯 ALMA:  ";
    if (ind.almaReady) {
        std::cout << '$' << fmt(ind.alma);
    } else {
        std::cout << "(warming " << (snap ? snap->priceCount : 0)
                  << '/' << (snap ? snap->priceRing.size() : 0) << ")";
    }
    std::cout << '\n';
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::printKalmanRing(const char* title, const auto& ring, size_t show) const
{
    std::cout << "\n4️⃣  " << title << " (snapshot):\n";
    std::cout << "   Size: " << ring.size() 
              << " slots | Capacity: " << ring.capacity << '\n';
    
    if (ring.empty()) {
        std::cout << "   💤 No Kalman ticks yet\n";
        return;
    }
    
    // Show latest price
    std::cout << "   💰 Latest Price: $" 
              << std::fixed << std::setprecision(2) << ring.latest().px << '\n';
    
    // Show recent tick history
    const std::size_t showCount = std::min(show, ring.size());
    std::cout << "   📈 Recent Ticks: ";
    for (std::size_t i = 0; i < showCount; ++i) {
        const auto& tick = ring.at(i);
        std::cout << std::fixed << std::setprecision(2) << tick.px;
        if (i < showCount - 1) std::cout << " → ";
    }
    std::cout << '\n';
}

} // namespace ring_buffer_trade_handler
