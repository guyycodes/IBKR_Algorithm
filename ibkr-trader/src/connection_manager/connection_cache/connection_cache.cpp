// connection_cache.cpp

#include "connection_cache.hpp"
#include "../decoder/decoder.hpp"  // Include IBKRDecoder for special size value handling
#include <iostream>
#include <chrono>

namespace connection {

// Initialize the static data cache
std::unordered_map<std::string, stock_data_tick::StockData> ConnectionCache::dataCache;

stock_data_tick::StockData& ConnectionCache::getSymbolData(const std::string& symbol) {
    // Get existing cached data for this symbol or create a new entry if it doesn't exist
    return dataCache[symbol];
}

double ConnectionCache::decodeSpecialValue(double value, int field) {
    // Check if this is a special size value using the IBKRDecoder
    if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(value)) {
        return ibkr_decoder::IBKRDecoder::interpretSizeValue(value, field);
    }
    return value;
}

stock_data_tick::StockData ConnectionCache::mergeWithCache(
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
) {
    // Get the cached data
    auto& cachedData = getSymbolData(symbol);
    
    // Update timestamp in the cached data
    cachedData.timestamp = timestamp;
    
    // Create a new StockData object that combines fresh data with cached data
    stock_data_tick::StockData stockData;
    stockData.symbol = symbol;
    stockData.timestamp = timestamp;
    stockData.exchange = exchange.empty() ? cachedData.exchange : exchange;
    
    // Fill in with new data or fall back to cached values for each field
    stockData.last = price > 0 ? price : cachedData.last;
    stockData.volume = volume > 0 ? volume : cachedData.volume;
    stockData.bid = bid > 0 ? bid : cachedData.bid;
    stockData.ask = ask > 0 ? ask : cachedData.ask;
    stockData.bidSize = bidSize > 0 ? bidSize : cachedData.bidSize;
    stockData.askSize = askSize > 0 ? askSize : cachedData.askSize;
    
    // Add OHLC data if available
    stockData.open = open > 0 ? open : cachedData.open;
    stockData.high = high > 0 ? high : cachedData.high;
    stockData.low = low > 0 ? low : cachedData.low;
    stockData.close = close > 0 ? close : cachedData.close;
    stockData.wap = wap > 0 ? wap : cachedData.wap;
    
    // Update cache with new data for future use
    cachedData = stockData;
    
    return stockData;
}

int ConnectionCache::pruneOldEntries(int maxAgeMinutes) {
    try {
        // Get cache size before pruning
        size_t beforeSize = dataCache.size();
        
        // Convert current time to nanoseconds since epoch (same format as StockData.timestamp)
        auto now = std::chrono::high_resolution_clock::now();
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        // Calculate cutoff time (60 minutes before now)
        uint64_t cutoff_ns = now_ns - (static_cast<uint64_t>(maxAgeMinutes) * 60 * 1000000000ULL);
        
        // Log time information in human-readable format
        auto now_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto cutoff_time_t = now_time_t - (maxAgeMinutes * 60);
        
        std::cout << "[Cache] Current time: " << std::ctime(&now_time_t) 
                  << "       Cutoff time: " << std::ctime(&cutoff_time_t)
                  << "       Cache size before pruning: " << beforeSize << std::endl;
        
        int removedCount = 0;
        
        // Use the erase-remove idiom with iterators for safe removal during iteration
        for (auto it = dataCache.begin(); it != dataCache.end();) {
            if (it->second.timestamp < cutoff_ns) {
                // This entry is older than our time window
                std::cout << "[Cache] Pruning old entry for symbol: " << it->first 
                          << " (age: " << (now_ns - it->second.timestamp) / 1000000000 / 60 
                          << " minutes old)" << std::endl;
                
                it = dataCache.erase(it);
                removedCount++;
            } else {
                // This entry is still within our time window
                ++it;
            }
        }
        
        // Log summary of pruning operation
        std::cout << "[Cache] Pruning summary: " 
                  << removedCount << " entries removed, "
                  << beforeSize << " → " << dataCache.size() << " entries" << std::endl;
        
        return removedCount;
    } catch (const std::exception& e) {
        std::cerr << "[Cache][ERROR] Exception during cache pruning: " << e.what() << std::endl;
        return 0;
    }
}

} // namespace connection



