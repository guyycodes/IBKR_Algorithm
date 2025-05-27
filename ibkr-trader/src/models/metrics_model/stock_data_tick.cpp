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

StockData::StockData(const std::string& sym, const std::string& dt, double o, double h, double l, double c, volume_t vol, int cnt)
    : symbol(sym), dateTime(dt), open(o), high(h), low(l), close(c), volume(vol), count(cnt) {
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
    
    // WAP calculation
    if (bidSize + askSize > 0) {
        wap = (bid * askSize + ask * bidSize) / (bidSize + askSize);
    } else {
        wap = mid;
    }

    // Calculate signal metrics if we have order book data
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
        count++;
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

// // TimeSeriesData class implementations
// TimeSeriesData::TimeSeriesData(const std::string& sym, size_t historySize)
//     : symbol(sym), maxSize(historySize) {
// }

// void TimeSeriesData::addDataPoint(const StockData& data) {
//     dataPoints.push_back(data);
//     if (dataPoints.size() > maxSize) {
//         dataPoints.pop_front();
//     }
    
//     // Calculate time-series based metrics like VWAP, momentum
//     calculateTimeSeriesMetrics();
// }

// StockData TimeSeriesData::getCurrentData() const {
//     if (dataPoints.empty()) {
//         return StockData();
//     }
//     return dataPoints.back();
// }

// double TimeSeriesData::calculateMA(int periods) const {
//     if (dataPoints.size() < periods) return 0.0;
    
//     double sum = 0.0;
//     auto it = dataPoints.end() - periods;
//     for (int i = 0; i < periods; ++i) {
//         sum += (it + i)->last;
//     }
//     return sum / periods;
// }

// double TimeSeriesData::calculateEMA(int periods, double smoothingFactor) const {
//     if (dataPoints.empty()) return 0.0;
//     if (dataPoints.size() < periods) return dataPoints.back().last;
    
//     double alpha = smoothingFactor / (periods + 1.0);
//     double ema = dataPoints[dataPoints.size() - periods].last;
    
//     for (size_t i = dataPoints.size() - periods + 1; i < dataPoints.size(); ++i) {
//         ema = alpha * dataPoints[i].last + (1.0 - alpha) * ema;
//     }
    
//     return ema;
// }

// double TimeSeriesData::calculateRSI(int periods) const {
//     if (dataPoints.size() < periods + 1) return 50.0; // Neutral if not enough data
    
//     double avgGain = 0.0, avgLoss = 0.0;
    
//     // Calculate first average gain/loss
//     for (size_t i = dataPoints.size() - periods; i < dataPoints.size(); ++i) {
//         double change = dataPoints[i].last - dataPoints[i-1].last;
//         if (change > 0) {
//             avgGain += change;
//         } else {
//             avgLoss += std::abs(change);
//         }
//     }
    
//     avgGain /= periods;
//     avgLoss /= periods;
    
//     if (avgLoss == 0) return 100.0;
//     double rs = avgGain / avgLoss;
//     return 100.0 - (100.0 / (1.0 + rs));
// }

// double TimeSeriesData::calculateVWAP() const {
//     if (dataPoints.empty()) return 0.0;
    
//     double sumPriceVolume = 0.0;
//     volume_t sumVolume = 0;
    
//     for (const auto& data : dataPoints) {
//         sumPriceVolume += data.last * data.lastSize;
//         sumVolume += data.lastSize;
//     }
    
//     return (sumVolume > 0) ? sumPriceVolume / sumVolume : 0.0;
// }

// double TimeSeriesData::calculateMomentum(int periods) const {
//     if (dataPoints.size() < periods) return 0.0;
    
//     double currentPrice = dataPoints.back().last;
//     double pastPrice = dataPoints[dataPoints.size() - periods].last;
    
//     return (pastPrice > 0) ? ((currentPrice - pastPrice) / pastPrice) * 100.0 : 0.0;
// }

// bool TimeSeriesData::hasScalpingOpportunity(double imbalanceThreshold, 
//                                           double momentumThreshold,
//                                           double spreadMaxPercent) const {
//     if (dataPoints.empty()) return false;
    
//     const StockData& current = dataPoints.back();
    
//     // For scalping we want: significant imbalance, tight spread, and momentum in our favor
//     bool hasImbalance = current.hasSignificantImbalance(imbalanceThreshold);
//     bool hasTightSpread = current.hasTightSpread(spreadMaxPercent);
//     bool hasMomentum = std::abs(calculateMomentum()) > momentumThreshold;
    
//     return hasImbalance && hasTightSpread && hasMomentum;
// }

// void TimeSeriesData::calculateTimeSeriesMetrics() {
//     if (dataPoints.empty()) return;
    
//     // Update the last data point with time series metrics
//     dataPoints.back().vwap = calculateVWAP();
//     dataPoints.back().momentum = calculateMomentum();
// }

} // namespace stock_data_tick



