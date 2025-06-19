#ifndef RING_BUFFER_MONITOR_HPP
#define RING_BUFFER_MONITOR_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>  // for CSV testing

#include "ring_buffer_trade_handler.hpp"          // for indicator helpers
#include "time_ordered_tick_buffer.hpp"
#include "static_ring_buffer.hpp"                 // for high-frequency Kalman buffers
#include "kalman_tick.hpp"                        // for KalmanTick POD struct
#include "models/stock_data_tick/stock_data_tick.hpp"  // for StockData

namespace ring_buffer_trade_handler
{

/**
 * RingBufferMonitor
 * -----------------
 * Dedicated, low‑overhead logger that prints the contents of the three
 * ring‑buffers plus derived technical indicators in real‑time.
 *
 * A monitor object is *non‑copyable* but *movable*.
 * Start/stop are idempotent.  All resources are released in the destructor.
 */
class RingBufferMonitor
{
public:
    /**
     * @param buf     Reference to the live TimeOrderedTickBuffer.
     * @param handler Reference to the already‑existing trade handler
     *                (used only for its indicator calculations).
     */
    RingBufferMonitor(time_ordered_tick_buffer::TimeOrderedTickBuffer& buf,
                      RingBufferTradeHandler&                          handler);

    ~RingBufferMonitor();                         // RAII clean‑up

    /// Starts the background logging thread (no‑op if already running).
    /// @param maxDuration  How long to run before self‑termination (default: 3600 s).
    void start(std::chrono::seconds maxDuration = std::chrono::seconds{3600});

    /// Signals the thread to finish and joins it (no‑op if already stopped).
    void stop();

    /// Wake the thread immediately so that a new snapshot is printed
    /// *without* waiting for the next periodic timeout.
    void requestSnapshot() noexcept;

    /// Ingest a new tick into all three Kalman ring buffers.
    /// @param src Source tick from the main tick buffer
    /// @complexity O(1), target ≤30ns total for all three buffers
    /// @note Non-blocking SPSC write operation
    inline void emplaceKalmanTicks(const stock_data_tick::StockData& src) noexcept;

    // Get current bucket confidences (for external use)
    const hefkf_common::BucketConfidence20& get_current_bucket_1min() const { return current_bucket_1min_; }
    const hefkf_common::BucketConfidence20& get_current_bucket_5min() const { return current_bucket_5min_; }
    
    // -------------- Kalman ring buffer (public for IntegrationLoop access) --------------
    StaticRingBuffer<KalmanTick, 512>  m_kalman;  // 512 ≥ 300

    // Non‑copyable / movable
    RingBufferMonitor(const RingBufferMonitor&)            = delete;
    RingBufferMonitor& operator=(const RingBufferMonitor&) = delete;
    RingBufferMonitor(RingBufferMonitor&&)                 = default;
    RingBufferMonitor& operator=(RingBufferMonitor&&)      = default;

private:
    // ---------- worker ----------
    void monitorLoop(std::chrono::seconds maxDuration) noexcept;
    // ---------- helpers ----------
    void printMinuteRing(const time_ordered_tick_buffer::MonitorSnapshot& s)          const;
    void printCandleRing(const time_ordered_tick_buffer::MonitorSnapshot& s)          const;
    void printPriceRing(const time_ordered_tick_buffer::MonitorSnapshot& s)           const;
    void printRecentTicks(const time_ordered_tick_buffer::MonitorSnapshot& s)         const;
    void printTechnicalIndicators()                                                   const;
    void printKalmanRing(const char* title, const auto& ring, size_t show = 5)       const;

    // ---------- data ----------
    time_ordered_tick_buffer::TimeOrderedTickBuffer& m_buf;
    RingBufferTradeHandler&                          m_handler;

    std::thread              m_thread;
    std::atomic<bool>        m_running{false};
    std::atomic<bool>        m_snapshotRequested{false};
    std::mutex               m_mutex;
    std::condition_variable  m_cv;

    // -------------- CSV testing --------------
    std::vector<stock_data_tick::StockData> m_testTicks;  // CSV data loaded at startup
    std::size_t m_csvIndex{0};                             // Current position in CSV data
    
};

} // namespace ring_buffer_trade_handler

#endif // RING_BUFFER_MONITOR_HPP
