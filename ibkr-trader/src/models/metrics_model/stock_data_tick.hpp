// stock_data_tick.hpp

#ifndef STOCK_DATA_TICK_HPP
#define STOCK_DATA_TICK_HPP

#include <string>
#include <vector>
#include <ctime>
#include <deque>
#include <map>

namespace stock_data_tick {

// Type definitions
using timestamp_t = uint64_t;
using volume_t = uint64_t;

/**
 * Represents a single price level in the order book
 */
struct PriceLevel {
    double price = 0.0;
    volume_t size = 0;
    std::string marketMaker = ""; // Market maker ID (if available)
    bool isSmartDepth = false;    // Whether this is smart depth data
    
    PriceLevel() = default;
    PriceLevel(double p, volume_t s, const std::string& mm = "", bool smart = false)
        : price(p), size(s), marketMaker(mm), isSmartDepth(smart) {}
};

/**
 * StockData - Represents a single data point in time series
 * Contains market data and derived metrics for scalping algorithms
 */
class StockData {
public:
    // Core identification
    std::string symbol;
    timestamp_t timestamp;  // Unix timestamp in milliseconds
    std::string exchange;

    // Core market data
    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
    volume_t bidSize = 0;
    volume_t askSize = 0;
    volume_t lastSize = 0;
    volume_t volume = 0;

    // OHLC data (for bar representation)
    std::string dateTime;  // Formatted date/time string
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;

    // Order book (market depth) data
    std::map<double, PriceLevel> bidLevels;  // Maps price -> level (sorted descending)
    std::map<double, PriceLevel> askLevels;  // Maps price -> level (sorted ascending)
    int depthMktDataPosition = 0;            // Position in market depth update

    // Derived metrics for scalping
    double mid = 0.0;           // Midpoint price
    double spread = 0.0;        // Bid-ask spread
    double spreadPercent = 0.0; // Spread as percentage of mid price
    double vwap = 0.0;          // Volume-weighted average price
    double imbalance = 0.0;     // Order book imbalance (bid vs ask size)
    double momentum = 0.0;      // Short-term price momentum
    double depthImbalance = 0.0; // Imbalance based on full order book (not just top of book)

    // Technical indicators (populated by time-ordered buffer)
    double rsi = 0.0;           // Relative Strength Index
    double ema9 = 0.0;          // 9-period Exponential Moving Average
    double ema26 = 0.0;         // 26-period Exponential Moving Average
    double alma = 0.0;          // Arnaud Legoux Moving Average
    double atr = 0.0;           // Average True Range

    
    // Constructors
    StockData() = default;
    StockData(const std::string& sym, timestamp_t ts, double bidPrice, double askPrice, double lastPrice = 0.0);
    StockData(const std::string& sym, const std::string& dt, double o, double h, double l, double c, volume_t vol);

    // Methods
    void calculateDerivedMetrics();
    void updateTick(double bidPrice, double askPrice, double lastPrice, 
                   volume_t bidVol, volume_t askVol, volume_t lastVol);
                   
    // Market depth methods
    void updateDepth(bool isBid, int position, double price, volume_t size);
    void updateDepthL2(bool isBid, int position, double price, volume_t size, const std::string& marketMaker, bool isSmartDepth);
    void calculateDepthMetrics();
    
    // Analysis methods
    bool hasSignificantImbalance(double threshold = 0.3) const;
    bool hasTightSpread(double maxSpreadPercent = 0.1) const;
    bool hasSufficientLiquidity(volume_t minSize = 1000) const;
    std::string formatTimestamp() const;
};

// /**
//  * TimeSeriesData - Manages a collection of StockData points for time series analysis
//  * Provides methods for calculating moving averages, momentum, and other indicators
//  */
// class TimeSeriesData {
// private:
//     std::string symbol;
//     std::deque<StockData> dataPoints;
//     size_t maxSize = 300;  // Default to storing 5 minutes of 1-second data
    
//     // Private methods
//     void calculateTimeSeriesMetrics();
    
// public:
//     // Constructor
//     TimeSeriesData(const std::string& sym, size_t historySize = 300);
    
//     // Data management
//     void addDataPoint(const StockData& data);
//     StockData getCurrentData() const;
    
//     // Technical indicators
//     double calculateMA(int periods) const;
//     double calculateEMA(int periods, double smoothingFactor = 2.0) const;
//     double calculateRSI(int periods = 14) const;
    // double calculateVWAP() const;
//     double calculateMomentum(int periods = 10) const;
    
//     // Opportunity detection
//     bool hasScalpingOpportunity(double imbalanceThreshold = 0.3, 
//                                double momentumThreshold = 0.5,
//                                double spreadMaxPercent = 0.1) const;
// };

} // namespace stock_data_tick

#endif // STOCK_DATA_TICK_HPP
