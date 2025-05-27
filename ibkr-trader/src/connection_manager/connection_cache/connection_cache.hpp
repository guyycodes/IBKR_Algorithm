// =============== CACHING SYSTEM ===============
// This is a static cache that persists for the entire application lifetime
// It stores the last known complete state for each symbol to handle IBKR's
// incremental update pattern where only changed fields are sent
//
// IMPORTANT: This cache has no size limit or cleanup mechanism. It will grow
// without bounds as more symbols are added, which could lead to memory issues

// connection_cache.hpp

#ifndef CONNECTION_CACHE_HPP
#define CONNECTION_CACHE_HPP

#include <unordered_map>
#include <string>
#include <chrono>
#include "../../models/metrics_model/stock_data_tick.hpp"
#include "ibkr/cppclient/client/CommonDefs.h"

namespace connection {

/**
 * @brief ConnectionCache handles caching of market data to maintain state between partial updates.
 * 
 * This cache exists because IBKR sends incremental updates (only changed fields) rather than 
 * complete snapshots. The cache maintains the last known full state for each symbol.
 * 
 * The cache includes a pruning mechanism to keep only the last 60 minutes of data.
 */
class ConnectionCache {
public:
    /**
     * @brief Get the cached StockData for a symbol, or create a new entry if it doesn't exist.
     * @param symbol The stock symbol to get data for
     * @return Reference to the cached StockData
     */
    static stock_data_tick::StockData& getSymbolData(const std::string& symbol);
    
    /**
     * @brief Merge new data with cached data, updating only non-zero/non-empty fields.
     * @param symbol The stock symbol
     * @param timestamp Current timestamp
     * @param price Last price (or 0 if not updated)
     * @param volume Volume (or 0 if not updated)
     * @param bid Bid price (or 0 if not updated)
     * @param ask Ask price (or 0 if not updated)
     * @param bidSize Bid size (or 0 if not updated)
     * @param askSize Ask size (or 0 if not updated)
     * @param exchange Exchange (or empty if not updated)
     * @param open Open price (or 0 if not updated)
     * @param high High price (or 0 if not updated)
     * @param low Low price (or 0 if not updated)
     * @param close Close price (or 0 if not updated)
     * @param wap Weighted average price (or 0 if not updated)
     * @return StockData object with merged data
     */
    static stock_data_tick::StockData mergeWithCache(
        const std::string& symbol,
        uint64_t timestamp,
        double price,
        double volume,
        double bid,
        double ask,
        double bidSize,
        double askSize,
        const std::string& exchange,
        double open,
        double high,
        double low,
        double close,
        double wap
    );
    
    /**
     * @brief Process potential special size values, decoding if necessary.
     * @param value The value to check and possibly decode
     * @param field The tick type field for context
     * @return Decoded value
     */
    static double decodeSpecialValue(double value, int field);
    
    /**
     * @brief Prune entries older than the specified number of minutes
     * @param maxAgeMinutes Maximum age in minutes (default: 60)
     * @return Number of entries removed
     */
    static int pruneOldEntries(int maxAgeMinutes = 60);

private:
    // Cache that persists for the entire application lifetime
    static std::unordered_map<std::string, stock_data_tick::StockData> dataCache;
};

} // namespace connection

#endif // CONNECTION_CACHE_HPP


