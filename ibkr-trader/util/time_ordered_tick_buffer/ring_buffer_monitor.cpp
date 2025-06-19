#include "ring_buffer_monitor.hpp"
#include <sstream>
#include <cmath>  // for std::isfinite
#include <fstream>  // for CSV file reading
#include "models/stock_data_tick/stock_data_tick.hpp"  // for StockData

namespace ring_buffer_trade_handler
{

// --------------------------------------------------------------------
// 1) Let's define a compile-time or run-time switch to use CSV or not
// --------------------------------------------------------------------
static constexpr bool USE_CSV_FOR_TESTING = true;  // set true to feed from CSV
static const char*   TEST_CSV_PATH = "/workspace/ibkr-trader/util/HEFKF/build/test_csv/test_data_downtrend.csv";
static constexpr int TICK_POLL_INTERVAL = 250; // 4Hz
static constexpr int FULL_SNAPSHOT_INTERVAL = 60000; // 60s
// --------------------------------------------------------------------
// 2) Helper function to parse one line of CSV into StockData
//    (Adjust column indexes to match your actual CSV structure.)
// --------------------------------------------------------------------
static stock_data_tick::StockData parseCSVLine(const std::string& line)
{
    stock_data_tick::StockData sd{};
    // CSV columns: Symbol, Timestamp, Last, Bid, Ask, BidSize, AskSize, Volume,
    //             Open, High, Low, Close, Spread, VWAP, RSI, ...
    // We'll parse only the columns we actually need.

    std::stringstream ss(line);
    std::string token;
    int colIndex = 0;

    while (std::getline(ss, token, ',')) {
        switch (colIndex) {
            case 0: // Symbol
                sd.symbol = token;
                break;
            case 1: // Timestamp
                sd.timestamp = std::stoll(token);
                break;
            case 2: // Last
                sd.last = std::stod(token);
                break;
            case 3: // Bid
                sd.bid = std::stod(token);
                break;
            case 4: // Ask
                sd.ask = std::stod(token);
                break;
            case 7: // Volume
                sd.volume = std::stod(token);
                break;
            case 12: // Spread
                sd.spread = std::stod(token);
                break;
            default:
                // Skip columns we don't need
                break;
        }
        ++colIndex;
    }
    return sd;
}

// Helper function to check whether a tick is "valid enough" for Kalman
static bool isValidForKalman(const stock_data_tick::StockData& t)
{
    // Example checks; tailor them to your data requirements:
    if (!std::isfinite(t.last)  || t.last  <= 0.0) return false;
    if (!std::isfinite(t.volume) || t.volume < 0.0) return false;
    if (!std::isfinite(t.bid)   || t.bid   <= 0.0) return false;
    if (!std::isfinite(t.ask)   || t.ask   <= 0.0) return false;
    if (!std::isfinite(t.spread) || t.spread <= 0.0) return false;
    if (t.ask < t.bid) return false;  // Spread must be >= 0

    // If all checks pass, good to go
    return true;
}

RingBufferMonitor::RingBufferMonitor(time_ordered_tick_buffer::TimeOrderedTickBuffer& buf,
                                     RingBufferTradeHandler&                          handler)
    : m_buf(buf)
    , m_handler(handler)
{
    if (USE_CSV_FOR_TESTING) {
        std::ifstream fin(TEST_CSV_PATH);
        if (!fin.is_open()) {
            std::cerr << "[RingBufferMonitor] ⚠️ Could not open CSV file: " << TEST_CSV_PATH << "\n";
        } else {
            std::string line;
            bool firstLine = true; // skip header row
            while (std::getline(fin, line)) {
                if (firstLine) {
                    firstLine = false;
                    continue; // skip the CSV header
                }
                if (!line.empty()) {
                    // parse line → StockData, store in m_testTicks
                    auto sd = parseCSVLine(line);
                    m_testTicks.push_back(sd);
                }
            }
            std::cout << "[RingBufferMonitor] Loaded " << m_testTicks.size()
                      << " ticks from CSV\n";
        }
    }
}

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

inline void RingBufferMonitor::emplaceKalmanTicks(const stock_data_tick::StockData& src) noexcept
{
    // Create KalmanTick from StockData
    KalmanTick k;
    k.ts = static_cast<uint64_t>(src.timestamp);  // Cast timestamp_t to uint64_t
    k.px = src.last;
    k.volume = static_cast<double>(src.volume); // Cast volume_t to double
    k.spread = src.ask - src.bid;
    k.bid = src.bid;
    k.ask = src.ask;
    

    // k.vwap = src.vwap;
    // k.priceChange = src.priceChange;
    // k.barRange = src.barRange;
    // k.imbalance = src.imbalance;
    // k.momentum = src.momentum;
    
    // Push to buffers at 2Hz rate
    m_kalman.push(k);  
}

/*----------------------------------------------------------------------*/

void RingBufferMonitor::monitorLoop(std::chrono::seconds maxDuration) noexcept
{
    try {
        auto deadline          = std::chrono::steady_clock::now() + maxDuration;
        auto lastFullSnapshot  = std::chrono::steady_clock::now();
        const auto FULL_SNAPSHOT_INTERVAL = std::chrono::milliseconds{FULL_SNAPSHOT_INTERVAL};  // 60s
        const auto TICK_POLL_INTERVAL    = std::chrono::milliseconds{TICK_POLL_INTERVAL};    // 4Hz

        int  iteration         = 0;
        int  tickAccessCount   = 0;

        // Acquire the monitor's lock once outside the loop
        std::unique_lock<std::mutex> lk(m_mutex);

        std::cout << "\n🚀 [RingBufferMonitor] Real‑time monitoring engaged "
                  << "(4Hz tick polling, 60s full snapshots)\n";
                  
        if (USE_CSV_FOR_TESTING) {
            std::cout << "📁 [CSV Mode] Feeding " << m_testTicks.size() 
                      << " ticks from CSV at 4Hz rate\n";
        }

        while (m_running.load() && std::chrono::steady_clock::now() < deadline)
        {
            // Wait up to 500ms for either:
            //  (a) a stop signal (m_running = false), or
            //  (b) a manual snapshot request (m_snapshotRequested),
            //  (c) or simply timeout after 500ms if nothing happens.
            m_cv.wait_for(lk, TICK_POLL_INTERVAL, [this]
            {
                return !m_running.load() || m_snapshotRequested.exchange(false);
            });

            // If we've been signaled to stop, exit right away.
            if (!m_running.load())
                break;

            // --- Outside the lock, to let other threads proceed quickly ---
            lk.unlock();

            // ===============================
            // (A) If CSV testing, add 1 tick from CSV into the pipeline
            // ===============================
            if (USE_CSV_FOR_TESTING && (m_csvIndex < m_testTicks.size())) {
                const auto& csvTick = m_testTicks[m_csvIndex++];
                // This feeds the tick into TimeOrderedTickBuffer
                m_buf.addTick(csvTick);
                
                // Log when CSV data is exhausted
                if (m_csvIndex == m_testTicks.size()) {
                    std::cout << "\n📊 [CSV Mode] All " << m_testTicks.size() 
                              << " CSV ticks have been fed into the system\n";
                }
            }

            // ===============================
            // (B) Then do the normal snapshot logic
            // ===============================
            // 1) Poll for the latest snapshot of ticks (2Hz)
            auto snap = m_buf.getSnapshot();
            if (!snap) {
                std::cout << "[RingBufferMonitor] (no snapshot yet)\n";
                // Re‑lock before next loop iteration
                lk.lock();
                continue;
            }

            try {
                // 2) Check if it's time for the 60s "full" snapshot
                auto now = std::chrono::steady_clock::now();
                bool printFullSnapshot = ((now - lastFullSnapshot) >= FULL_SNAPSHOT_INTERVAL);
                if (printFullSnapshot)
                {
                    lastFullSnapshot = now;
                    ++iteration;

                    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
                    std::cout << "📊 [DATA FREQUENCY] Snapshot #" << iteration << " contains:\n"
                              << "   • " << snap->recentTicks.size()
                              << " real‑time ticks (last 2 seconds, max 200)\n"
                              << "   • Last snapshot time: " << snap->tickSnapshotTime << " ms\n";
                    
                    // Print ring buffers, indicators, etc.
                    printMinuteRing(*snap);
                    printCandleRing(*snap);
                    printPriceRing(*snap);
                    printTechnicalIndicators();
                    printRecentTicks(*snap);
                }

                // ────────────────────────────
                // (A) High-frequency (2Hz) Kalman feed
                // ────────────────────────────
                if (!snap->recentTicks.empty()) {
                    tickAccessCount++;

                    // Find the newest valid tick by scanning from back (newest) to front (oldest)
                    const stock_data_tick::StockData* newest_valid = nullptr;
                    for (auto rit = snap->recentTicks.rbegin();
                         rit != snap->recentTicks.rend(); 
                         ++rit)
                    {
                        const auto & st = rit->second; // The StockData
                        if (isValidForKalman(st)) {
                            newest_valid = &st;
                            break;
                        }
                    }

                    // If we found a valid tick, feed it into the Kalman buffer
                    if (newest_valid) {
                        emplaceKalmanTicks(*newest_valid);
                    } else {
                        // No valid ticks in the last 2s => skip this cycle
                        // (optionally log or do nothing)
                        std::cout << "[monitorLoop] No valid ticks found in last 2s.\n";
                    }
                }

                // ────────────────────────────
                // (B) Print Kalman ring states every 60s
                // ────────────────────────────
                if (printFullSnapshot) {
                    printKalmanRing("KALMAN BUFFER", m_kalman, 5);
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[RingBufferMonitor] ⚠️ Exception in snapshot: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[RingBufferMonitor] ⚠️ Unknown exception in snapshot\n";
            }

            // Re‑lock for the next iteration's wait_for
            lk.lock();
        }

        std::cout << "\n🛑 [RingBufferMonitor] Monitoring finished:\n"
                  << "   • Full snapshots printed: " << iteration << "\n"
                  << "   • Tick data accessed ~" << tickAccessCount << " times (2Hz)\n";
                  
        if (USE_CSV_FOR_TESTING) {
            std::cout << "   • CSV ticks processed: " << m_csvIndex 
                      << " of " << m_testTicks.size() << "\n";
        }

    } catch (const std::exception& ex) {
        std::cerr << "[RingBufferMonitor] Uncaught exception: " << ex.what() << '\n';
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

void RingBufferMonitor::printRecentTicks(
        const time_ordered_tick_buffer::MonitorSnapshot& s) const
{
    std::cout << "\n4️⃣  RECENT TICKS (last 2 seconds, max 200):\n";
    std::cout << "   Total ticks: " << s.recentTicks.size() 
              << " | Snapshot time: " << s.tickSnapshotTime << " ms\n";
    
    if (s.recentTicks.empty()) {
        std::cout << "   💤 No ticks in the last 2 seconds\n";
        return;
    }
    
    // Show first and last few ticks
    const std::size_t showStart = std::min<std::size_t>(3, s.recentTicks.size());
    const std::size_t showEnd = std::min<std::size_t>(3, s.recentTicks.size());
    
    std::cout << "   📊 First " << showStart << " ticks:\n";
    for (std::size_t i = 0; i < showStart; ++i) {
        const auto& [ts, tick] = s.recentTicks[i];
        std::cout << "      • " << ts << " ms: $" << std::fixed << std::setprecision(2) 
                  << tick.last << " (vol: " << tick.volume << ")\n";
    }
    
    if (s.recentTicks.size() > showStart + showEnd) {
        std::cout << "      ... (" << (s.recentTicks.size() - showStart - showEnd) 
                  << " more ticks) ...\n";
    }
    
    if (s.recentTicks.size() > showStart) {
        std::cout << "   📊 Last " << showEnd << " ticks:\n";
        for (std::size_t i = s.recentTicks.size() - showEnd; i < s.recentTicks.size(); ++i) {
            const auto& [ts, tick] = s.recentTicks[i];
            std::cout << "      • " << ts << " ms: $" << std::fixed << std::setprecision(2) 
                      << tick.last << " (vol: " << tick.volume << ")\n";
        }
    }
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
    std::cout << "\n5️⃣  " << title << " (snapshot):\n";
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
