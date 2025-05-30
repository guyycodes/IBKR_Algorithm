// connection_cache.cpp

#include "connection_cache.hpp"
#include "../decoder/decoder.hpp"  // Include IBKRDecoder for special size value handling
#include <iostream>
#include <chrono>
#include <iomanip>

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

// Enhanced merge method that tracks tick-by-tick changes
ConnectionCache::MergeResult ConnectionCache::mergeWithCacheAndTrackChanges(
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
    double vwap
) {
    // Get existing cached data for this symbol (creates entry if first time)
    // NOTE: For single-symbol usage, this is always the same object - no memory growth
    stock_data_tick::StockData& cached = dataCache[symbol];
    
    // Track if any tick-by-tick fields changed
    bool tickByTickChanged = false;
    
    // Check for tick-by-tick changes before updating
    // (bid, ask, bidSize, askSize, and calculated midPoint)
    if (bid > 0 && cached.bid != bid) {
        tickByTickChanged = true;
    }
    if (ask > 0 && cached.ask != ask) {
        tickByTickChanged = true;
    }
    if (bidSize > 0 && cached.bidSize != bidSize) {
        tickByTickChanged = true;
    }
    if (askSize > 0 && cached.askSize != askSize) {
        tickByTickChanged = true;
    }
    if (timestamp > 0 && cached.timestamp != timestamp) {
        tickByTickChanged = true;
    }
    
    // Calculate midPoint and check if it changed
    double newMidPoint = 0.0;
    bool usePreCalculatedMidPoint = (price > 0);  // Pre-calculated midPoint takes priority
    
    if (usePreCalculatedMidPoint) {
        // Use the pre-calculated midPoint that was passed in
        if (cached.last != price) {
            tickByTickChanged = true;
        }
    } else if (bid > 0 && ask > 0) {
        // Calculate midPoint from bid/ask only if no pre-calculated midPoint
        newMidPoint = (bid + ask) / 2.0;
        if (cached.last != newMidPoint) {
            tickByTickChanged = true;
        }
    }
    
    // IMPORTANT: All updates below are IN-PLACE modifications of the same cached object
    // No new memory allocations occur - we reuse the same StockData object indefinitely
    // Update only non-zero/non-empty fields (IBKR only sends changed data)
    if (timestamp > 0) cached.timestamp = timestamp; // timestamp comes from TICK-BY-TICK BID-ASK
    if (volume > 0) cached.volume = volume; // this is the entire exchange volume, not for a single symbol
    if (bid > 0) cached.bid = bid; 
    if (ask > 0) cached.ask = ask; 
    if (bidSize > 0) cached.bidSize = bidSize; 
    if (askSize > 0) cached.askSize = askSize; 
    if (!exchange.empty()) cached.exchange = exchange;
    if (open > 0) cached.open = open; // ← Only update if new data (updates every 5 seconds)
    if (high > 0) cached.high = high; // ← Only update if new data (updates every 5 seconds)
    if (low > 0) cached.low = low; // ← Only update if new data (updates every 5 seconds)
    if (close > 0) cached.close = close; // ← Only update if new data (updates every 5 seconds)
    if (vwap > 0) cached.vwap = vwap; 
    
    // Set the midPoint (prioritize pre-calculated over calculated)
    if (usePreCalculatedMidPoint) {
        cached.last = price;  // Use pre-calculated midPoint from tickByTickBidAsk callback
    } else if (newMidPoint > 0) {
        cached.last = newMidPoint;  // Use calculated midPoint from bid/ask
    }
    
    // Update the last update time to now (in-place update)
    // NOTE: This keeps the single symbol "fresh" so it's never pruned
    cached.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Check if data is complete (has essential tick-by-tick fields)
    bool isComplete = (cached.last > 0 && 
                                 cached.bid > 0 && 
                                 cached.ask > 0 && 
                                 cached.bidSize > 0 && 
                                 cached.askSize > 0 &&
                                 cached.timestamp > 0 &&
                                 cached.vwap > 0);

    
    // Debug: Log what fields we have/don't have
    if (!isComplete) {
        std::cout << "[Cache][DEBUG] ❌ Data incomplete for " << symbol << ":" << std::endl;
        std::cout << "  open: " << cached.open << " (optional)" << std::endl;
        std::cout << "  high: " << cached.high << " (optional)" << std::endl;
        std::cout << "  low: " << cached.low << " (optional)" << std::endl;
        std::cout << "  close: " << cached.close << " (optional)" << std::endl;
        std::cout << "  last: " << cached.last << " (required > 0)" << std::endl;
        std::cout << "  bid: " << cached.bid << " (required > 0)" << std::endl;
        std::cout << "  ask: " << cached.ask << " (required > 0)" << std::endl;
        std::cout << "  bidSize: " << cached.bidSize << " (optional)" << std::endl;
        std::cout << "  askSize: " << cached.askSize << " (optional)" << std::endl;
        std::cout << "  vwap: " << cached.vwap << " (optional)" << std::endl;
        std::cout << "  timestamp: " << cached.timestamp << " (required > 0)" << std::endl;
        std::cout << "  volume: " << cached.volume << " (required > 0)" << std::endl;
    } else {
        std::cout << "[Cache][DEBUG] ✅ Data ready for " << symbol << std::endl;
        std::cout << "  open: " << cached.open  << std::endl;
        std::cout << "  high: " << cached.high << std::endl;
        std::cout << "  low: " << cached.low << std::endl;
        std::cout << "  close: " << cached.close << std::endl;
        std::cout << "  last $: " << cached.last << std::endl;
        std::cout << "  bid: " << cached.bid << std::endl;
        std::cout << "  ask: " << cached.ask << std::endl;
        std::cout << "  bidSize: " << cached.bidSize << std::endl;
        std::cout << "  askSize: " << cached.askSize << std::endl;
        std::cout << "  vwap: " << cached.vwap << std::endl;
        std::cout << "  timestamp: " << cached.timestamp << std::endl;
        std::cout << "  volume: " << cached.volume << std::endl;
    }
    
    // Return result with change tracking
    MergeResult result;
    result.data = cached;  // Return a copy of the updated data
    result.tickByTickChanged = tickByTickChanged;
    result.isComplete = isComplete;
    
    return result;
}

// Original method for backward compatibility - now uses the enhanced method
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
    double vwap
) {
    // Use the enhanced method and return just the data for backward compatibility
    auto result = mergeWithCacheAndTrackChanges(symbol, timestamp, price, volume, bid, ask, 
                                               bidSize, askSize, exchange, open, high, low, close, vwap);
    return result.data;
}

int ConnectionCache::pruneOldEntries(int maxAgeMinutes) {
    // PRUNING SAFETY MECHANISM:
    // This method is designed for multi-symbol caches but serves as a safety mechanism
    // for single-symbol usage. Since each ConnectionCache handles only one symbol
    // and we constantly update its timestamp to "now", this is essentially a no-op.
    // The single symbol will never be old enough to be pruned.
    
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
        // NOTE: For single-symbol usage, this loop will find the one entry but never remove it
        // because we constantly update its timestamp, keeping it "fresh"
        for (auto it = dataCache.begin(); it != dataCache.end();) {
            if (it->second.timestamp < cutoff_ns) {
                // This entry is older than our time window
                // NOTE: This should never happen for actively updated single-symbol cache
                std::cout << "[Cache] Pruning old entry for symbol: " << it->first 
                          << " (age: " << (now_ns - it->second.timestamp) / 1000000000 / 60 
                          << " minutes old)" << std::endl;
                
                it = dataCache.erase(it);
                removedCount++;
            } else {
                // This entry is still within our time window (always true for single-symbol)
                ++it;
            }
        }
        
        // Log summary of pruning operation
        // NOTE: For single-symbol usage, this will always show "0 entries removed"
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



