// =============== CACHING SYSTEM ===============
// This is a static cache that persists for the entire application lifetime
// It stores the last known complete state for each symbol to handle IBKR's
// incremental update pattern where only changed fields are sent
//
// MEMORY CHARACTERISTICS FOR SINGLE-SYMBOL USAGE:
// - Each ConnectionCache instance handles ONLY ONE symbol
// - Cache contains exactly ONE StockData object that is reused indefinitely
// - All updates are IN-PLACE modifications - no new allocations after first creation
// - Memory footprint: ~200-300 bytes total (fixed, never grows)
// - Pruning is a safety mechanism but essentially a no-op for single-symbol usage

// connection_cache.hpp

#ifndef CONNECTION_CACHE_HPP
#define CONNECTION_CACHE_HPP

#include <unordered_map>
#include <string>
#include <chrono>
#include "../../models/metrics_model/stock_data_tick.hpp"
#include "ibkr/cppclient/client/CommonDefs.h"

// Forward declaration to avoid circular dependency
namespace ibkr_decoder {
    class IBKRDecoder;
}

namespace connection {

/**
 * @brief ConnectionCache handles caching of market data to maintain state between partial updates.
 * 
 * This cache exists because IBKR sends incremental updates (only changed fields) rather than 
 * complete snapshots. The cache maintains the last known full state for each symbol.
 * 
 * SINGLE-SYMBOL DESIGN:
 * Each connection owns its own instance to ensure thread safety and handles exactly ONE symbol.
 * The cache performs in-place updates on a single StockData object, ensuring fixed memory usage.
 * Pruning exists as a safety mechanism but is essentially a no-op for single-symbol usage.
 */
class ConnectionCache {
public:
    /**
     * @brief Constructor for ConnectionCache instance
     * @param decoder Reference to the decoder instance for this connection
     */
    ConnectionCache(ibkr_decoder::IBKRDecoder& decoder);
    
    /**
     * @brief Destructor for ConnectionCache instance
     */
    ~ConnectionCache() = default;
    
    /**
     * @brief Get the cached StockData for a symbol, or create a new entry if it doesn't exist.
     * @param symbol The stock symbol to get data for
     * @return Reference to the cached StockData
     */
    stock_data_tick::StockData& getSymbolData(const std::string& symbol);
    
    /**
     * @brief Merge new data with cached data and return both the merged data and change indicators
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
     * @return MergeResult struct containing merged data and change indicators
     */
    struct MergeResult {
        stock_data_tick::StockData data;
        bool tickByTickChanged;  // True if bid, ask, bidSize, askSize, or midPoint changed
        bool isComplete;         // True if data has all required fields
    };
    
    // Enhanced merge method that tracks tick-by-tick changes
    MergeResult mergeWithCacheAndTrackChanges(
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

    // Original method for backward compatibility
    stock_data_tick::StockData mergeWithCache(
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
    double decodeSpecialValue(double value, int field);
    
    /**
     * @brief Prune entries older than the specified number of minutes
     * @param maxAgeMinutes Maximum age in minutes (default: 60)
     * @return Number of entries removed
     */
    int pruneOldEntries(int maxAgeMinutes = 60);

private:
    // Instance cache that persists for this connection's lifetime
    std::unordered_map<std::string, stock_data_tick::StockData> dataCache;
    
    // Reference to the decoder instance for this connection
    ibkr_decoder::IBKRDecoder& m_decoder;
};

} // namespace connection

#endif // CONNECTION_CACHE_HPP


