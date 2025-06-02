// stock_data.cpp

#include "stock_data_tick.hpp"
#include <algorithm>
#include <cmath>

namespace stock_data_tick {

// StockData class implementations
StockData::StockData(const std::string& sym, timestamp_t ts, double bidPrice, double askPrice, double lastPrice)
    : symbol(sym), timestamp(ts), bid(bidPrice), ask(askPrice), last(lastPrice) {
    calculateDerivedMetrics();
}

StockData::StockData(const std::string& sym, const std::string& dt, double o, double h, double l, double c, volume_t vol)
    : symbol(sym), dateTime(dt), open(o), high(h), low(l), close(c), volume(vol) {
    last = close;
    calculateDerivedMetrics();
}

void StockData::calculateDerivedMetrics() {
    // Basic derived values
    mid = (bid + ask) / 2.0;
    spread = ask - bid;
    spreadPercent = (bid > 0) ? (spread / mid) * 100.0 : 0.0;
    
    // Volume-based metrics
    if (bidSize + askSize > 0) {
        imbalance = static_cast<double>(bidSize - askSize) / (bidSize + askSize);
    }
    
    // Note: VWAP is calculated by the time-ordered buffer, not here
    // We removed WAP calculation since VWAP is the preferred metric
}

void StockData::calculateDepthMetrics() {
    // Calculate the full order book imbalance if we have depth data
    if (!bidLevels.empty() || !askLevels.empty()) {
        // Sum up all bid and ask sizes
        volume_t totalBidSize = 0;
        volume_t totalAskSize = 0;
        
        for (const auto& level : bidLevels) {
            totalBidSize += level.second.size;
        }
        
        for (const auto& level : askLevels) {
            totalAskSize += level.second.size;
        }
        
        // Calculate imbalance based on full depth
        if (totalBidSize + totalAskSize > 0) {
            depthImbalance = static_cast<double>(totalBidSize - totalAskSize) / 
                            (totalBidSize + totalAskSize);
        }
    }
}

void StockData::updateDepth(bool isBid, int position, double price, volume_t size) {
    if (isBid) {
        if (size == 0) {
            // Remove this price level
            bidLevels.erase(price);
        } else {
            // Update or add this price level
            bidLevels[price] = PriceLevel(price, size);
        }
    } else {
        if (size == 0) {
            // Remove this price level
            askLevels.erase(price);
        } else {
            // Update or add this price level
            askLevels[price] = PriceLevel(price, size);
        }
    }
    
    // Update the best bid/ask if this is the top of book
    if (position == 0) {
        if (isBid) {
            if (size > 0) {
                bid = price;
                bidSize = size;
            } else if (bidLevels.empty()) {
                bid = 0;
                bidSize = 0;
            } else {
                // Find the best bid (highest price)
                auto it = bidLevels.rbegin();
                bid = it->first;
                bidSize = it->second.size;
            }
        } else {
            if (size > 0) {
                ask = price;
                askSize = size;
            } else if (askLevels.empty()) {
                ask = 0;
                askSize = 0;
            } else {
                // Find the best ask (lowest price)
                auto it = askLevels.begin();
                ask = it->first;
                askSize = it->second.size;
            }
        }
    }
    
    // Store the position for reference
    depthMktDataPosition = position;
    
    // Recalculate metrics
    calculateDerivedMetrics();
}

void StockData::updateDepthL2(bool isBid, int position, double price, volume_t size, 
                           const std::string& marketMaker, bool isSmartDepth) {
    if (isBid) {
        if (size == 0) {
            // Remove this price level
            bidLevels.erase(price);
        } else {
            // Update or add this price level with market maker info
            bidLevels[price] = PriceLevel(price, size, marketMaker, isSmartDepth);
        }
    } else {
        if (size == 0) {
            // Remove this price level
            askLevels.erase(price);
        } else {
            // Update or add this price level with market maker info
            askLevels[price] = PriceLevel(price, size, marketMaker, isSmartDepth);
        }
    }
    
    // Similar update to the basic updateDepth method
    if (position == 0) {
        if (isBid) {
            if (size > 0) {
                bid = price;
                bidSize = size;
            } else if (bidLevels.empty()) {
                bid = 0;
                bidSize = 0;
            } else {
                auto it = bidLevels.rbegin();
                bid = it->first;
                bidSize = it->second.size;
            }
        } else {
            if (size > 0) {
                ask = price;
                askSize = size;
            } else if (askLevels.empty()) {
                ask = 0;
                askSize = 0;
            } else {
                auto it = askLevels.begin();
                ask = it->first;
                askSize = it->second.size;
            }
        }
    }
    
    depthMktDataPosition = position;
    calculateDerivedMetrics();
}

void StockData::updateTick(double bidPrice, double askPrice, double lastPrice, 
                           volume_t bidVol, volume_t askVol, volume_t lastVol) {
    bid = bidPrice;
    ask = askPrice;
    last = lastPrice;
    bidSize = bidVol;
    askSize = askVol;
    lastSize = lastVol;
    
    // Update OHLC if this is newer data
    if (last > 0) {
        if (open == 0) open = last;
        high = std::max(high, last);
        low = (low == 0) ? last : std::min(low, last);
        close = last;
        volume += lastVol;
        // Note: count field was removed since it's tracked elsewhere
    }
    
    calculateDerivedMetrics();
}

bool StockData::hasSignificantImbalance(double threshold) const {
    return std::abs(imbalance) > threshold;
}

bool StockData::hasTightSpread(double maxSpreadPercent) const {
    return spreadPercent < maxSpreadPercent;
}

bool StockData::hasSufficientLiquidity(volume_t minSize) const {
    return (bidSize >= minSize && askSize >= minSize);
}

std::string StockData::formatTimestamp() const {
    time_t time = timestamp / 1000; // Convert from ms to seconds
    struct tm timeinfo;
    char buffer[80];
    
    #ifdef _WIN32
    localtime_s(&timeinfo, &time);
    #else
    localtime_r(&time, &timeinfo);
    #endif
    
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return std::string(buffer);
}

} // namespace stock_data_tick



