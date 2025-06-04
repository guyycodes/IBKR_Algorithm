// model_manager.hpp
// ───────────────────────────────────────────────────────────────────────────────

#ifndef MODEL_MANAGER_HPP
#define MODEL_MANAGER_HPP
// ────────────────────────────────────────────────────────────────────────────────
//  ModelManager – symbol-scoped data hub (no threads)
// ────────────────────────────────────────────────────────────────────────────────
#include "raw_data_model/raw_data_model.hpp"
#include "volume_profile/volume_profile_map.hpp"
#include "../connection_manager/connection_manager.hpp"
#include "../util/time_ordered_tick_buffer/time_ordered_tick_buffer.hpp"
#include "../util/time_ordered_tick_buffer/ring_buffer_trade_handler.hpp"
#include "../models/stock_data_tick/stock_data_tick.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// namespace raw_data_model { struct TradingParams; class RawDataModel; }

namespace model_manager {

enum class TimeWindowUnit { SECONDS, MINUTES, HOURS };

class ModelManager {
public:
    ModelManager(std::string  symbol,
                 std::size_t  windowSize = 60,
                 TimeWindowUnit unit    = TimeWindowUnit::MINUTES);
    ~ModelManager();

    /* IBKR connectivity ---------------------------------------------------- */
    bool connectToIBKR();
    void disconnectFromIBKR();
    bool isConnected()                 const { return m_connected; }
    std::string getConnectionStatus()  const;

    /* main processing loop (called by AppState worker) --------------------- */
    std::vector<stock_data_tick::StockData> batchQueueData(std::size_t maxBatch);

    /* queue management (keeps STK_Q bounded to prevent memory issues) ------- */
    std::size_t processQueueData(); // Returns number of items processed/removed

    /* model inspection helpers -------------------------------------------- */
    std::string                           getSymbol()     const;
    std::size_t                           getTickCount()  const;
    const stock_data_tick::StockData*     getLatestTick() const;   // nullptr if none

    std::vector<stock_data_tick::StockData>
    getTicksInWindow() const;     // copy (diagnostics only – avoid in hot loops)

    const raw_data_model::TradingParams&  getParams()     const;
    std::pair<std::size_t, TimeWindowUnit>getTimeWindow() const;

    void setTimeWindow(std::size_t sz, TimeWindowUnit unit);
    void clearTicks();

    /* JSON initialization ----------------------------------------------- */
    bool initFromJson(const nlohmann::json& js);

    /* connection-retry state (queried by LocalAPI diagnostics) ------------- */
    int  getConnectionAttempts() const { return m_connectionAttempts; }
    static constexpr int getMaxConnectionAttempts() { return MAX_CONN_ATTEMPTS; }

    /* ticker feed (called by connection layer) ----------------------------- */
    void addTick (const stock_data_tick::StockData& tick);          // level-1/5s bar
    void addTradeTick(double price, int volume);                    // time-&-sales

    /* debug control -------------------------------------------------------- */
    void setDebugLogging(bool enabled) { m_debug_logging = enabled; }
    bool getDebugLogging() const { return m_debug_logging; }

private:
    /* pruning -------------------------------------------------------------- */
    void pruneOldData();
    std::chrono::milliseconds windowToDuration() const;

    /* members -------------------------------------------------------------- */
    std::unique_ptr<ring_buffer_trade_handler::RingBufferTradeHandler> m_rbHandler;
    std::unique_ptr<volume_profile_map::VolumeProfileMap>              m_volProfile;
    std::unique_ptr<raw_data_model::RawDataModel>                      m_rawModel;
    std::unique_ptr<connection_manager::ConnectionManager>             m_connMgr;
    std::unique_ptr<time_ordered_tick_buffer::TimeOrderedTickBuffer>   m_toBuffer;

    std::string          m_symbol;        // Add basic symbol storage
    std::size_t          m_windowSize;
    TimeWindowUnit       m_windowUnit;
    std::chrono::steady_clock::time_point m_lastPrune;

    int                    m_reqId{-1};
    std::atomic<bool>      m_connected{false};
    std::atomic<bool>      m_debug_logging{false};  // Control verbose logging

    // retry bookkeeping
    std::atomic<int>       m_connectionAttempts{0};
    std::chrono::steady_clock::time_point m_lastAttempt;
    static constexpr int   MAX_CONN_ATTEMPTS           = 5;
    static constexpr int   RETRY_DELAY_SECONDS         = 7;

    mutable std::mutex     m_mx;
};

} // namespace model_manager
#endif
