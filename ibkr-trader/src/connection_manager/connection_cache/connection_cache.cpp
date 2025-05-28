// connection_cache.cpp

#include "connection_cache.hpp"
#include "../decoder/decoder.hpp"  // Include IBKRDecoder for special size value handling
#include <iostream>
#include <chrono>

namespace connection {

// Constructor implementation
ConnectionCache::ConnectionCache(ibkr_decoder::IBKRDecoder& decoder) 
    : m_decoder(decoder) {
}

stock_data_tick::StockData& ConnectionCache::getSymbolData(const std::string& symbol) {
    // Get existing cached data for this symbol or create a new entry if it doesn't exist
    return dataCache[symbol];
}

double ConnectionCache::decodeSpecialValue(double value, int field) {
    if (m_decoder.isSpecialSizeValue(value)) {
        return m_decoder.interpretSizeValue(value, field);
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
    // Get existing cached data for this symbol
    stock_data_tick::StockData& cached = dataCache[symbol];
    
    // Update only non-zero/non-empty fields (IBKR only sends changed data)
    if (timestamp > 0) cached.timestamp = timestamp;
    if (price > 0) cached.last = price;
    if (volume > 0) cached.volume = volume;
    if (bid > 0) cached.bid = bid;
    if (ask > 0) cached.ask = ask;
    if (bidSize > 0) cached.bidSize = bidSize;
    if (askSize > 0) cached.askSize = askSize;
    if (!exchange.empty()) cached.exchange = exchange;
    if (open > 0) cached.open = open;
    if (high > 0) cached.high = high;
    if (low > 0) cached.low = low;
    if (close > 0) cached.close = close;
    if (wap > 0) cached.wap = wap;
    
    // Update the last update time to now
    cached.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Return a copy of the updated data
    return cached;
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



