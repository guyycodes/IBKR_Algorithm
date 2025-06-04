#pragma once
// ───────────────────────────────────────────────────────────────────────────────
//  RawDataModel – per-symbol market-data container (no threads, no singletons)
// ───────────────────────────────────────────────────────────────────────────────
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <atomic>
#include <vector>

#include "../../util/stk_q/stk_q.hpp"
#include "../stock_data_tick/stock_data_tick.hpp"
#include <nlohmann/json.hpp>

namespace raw_data_model {

/*───────────────────────────────────────────────────────────────────────────────
 * TradingParams – user-supplied configuration delivered via JSON               */
struct TradingParams {
    int    lots               = 0;
    double margin             = 0.0;
    double stopLossPct        = 0.0;
    int    maxTrades          = 0;
    double lossThresholdPct   = 0.0;
    double winThresholdPct    = 0.0;
    double minWinRatePct      = 0.0;
    int    maxHoldSeconds     = 0;

    [[nodiscard]]
    bool from_json(const nlohmann::json& js) noexcept;   // returns false on parse error
};

/*───────────────────────────────────────────────────────────────────────────────
 * RawDataModel                                                                 */
class RawDataModel {
public:
    explicit RawDataModel(std::string symbol, int queueIntervalMs = 250);

    // ingestion -----------------------------------------------------------------
    void addTick(const stock_data_tick::StockData& tick);

    // consumption ---------------------------------------------------------------
    bool popNextTick(stk_q::STK_Q_Data& out);          // non-blocking
    std::vector<stock_data_tick::StockData> popNextTicks(std::size_t maxBatch); // batch processing
    [[nodiscard]] bool latestTick(stock_data_tick::StockData& out) const;
    std::vector<stock_data_tick::StockData> getTicksInTimeWindow(std::uint64_t cutoffTimeMs) const;

    // admin ---------------------------------------------------------------------
    [[nodiscard]] std::size_t queueSize() const noexcept;
    void                      clear()      noexcept;
    void                      removeOlderThan(uint64_t cutoffTimeMs) noexcept;
    std::error_code           initialise(const nlohmann::json& jsonCfg);

    // accessors -----------------------------------------------------------------
    [[nodiscard]] const std::string&  symbol()  const noexcept { return m_symbol; }
    [[nodiscard]] const TradingParams& params() const noexcept { return m_params; }

private:
    // helpers
    static stk_q::STK_Q_Data convertToQueue(const stock_data_tick::StockData& s);
    static stock_data_tick::StockData convertToStockData(const stk_q::STK_Q_Data& q);

    std::string                       m_symbol;
    TradingParams                     m_params;

    std::unique_ptr<stk_q::STK_Q>     m_queue;

    // fast reader synchronisation (avoid heavy mutex for immutable getters)
    mutable std::atomic_flag          m_spinLock = ATOMIC_FLAG_INIT;
    mutable std::mutex                m_mx;        // writers + heavy ops
};

} // namespace raw_data_model
