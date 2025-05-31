#ifndef RING_BUFFER_TRADE_HANDLER_HPP
#define RING_BUFFER_TRADE_HANDLER_HPP

#include "time_ordered_tick_buffer.hpp"
#include "../../models/metrics_model/stock_data_tick.hpp"
#include <vector>

namespace ring_buffer_trade_handler {

// Data structures for entry signal confirmation
struct OrderBookSnapshot {
    double bestBidPrice;
    double bestAskPrice;
    double totalBidSize;
    double totalAskSize;
};

struct TapePrint {
    bool isBuy;
    double price;
    double size;
    int64_t timestamp;
};

struct MicroCandle {
    double open;
    double high;
    double low;
    double close;
    double volume;
    int64_t timestamp;
};

class RingBufferTradeHandler {
public:
    explicit RingBufferTradeHandler(time_ordered_tick_buffer::TimeOrderedTickBuffer* tickBuffer) 
        : m_tickBuffer(tickBuffer) {}
    
    bool checkForTradeOpportunity(const stock_data_tick::StockData& newTick);
    
    // Entry signal confirmation - call this after initial filters pass
    // Reads all data from ring buffer
    bool confirmEntry(double currentPrice);

private:
    time_ordered_tick_buffer::TimeOrderedTickBuffer* m_tickBuffer;
    
    // Constants
    static constexpr int64_t MS_PER_MINUTE = 60 * 1000;
    
    // Scalping filter parameters
    static constexpr int m_supertrendPeriod = 7;        // Short period for quick signals
    static constexpr double m_supertrendMultiplier = 2.0; // ATR multiplier
    static constexpr double m_maxVwapDistancePercent = 0.5; // 0.5% max distance from VWAP
    static constexpr int m_rsiPeriod = 14;              // RSI period
    static constexpr int m_rsiSmaPeriod = 5;            // SMA overlay on RSI
    static constexpr double m_volumeSurgeMultiplier = 1.5; // Volume must be 1.5x average
    static constexpr int m_volumeAvgPeriod = 10;        // Period for volume average
    
    // Entry signal confirmation parameters
    static constexpr double m_maxSpreadThreshold = 0.05;     // Maximum bid-ask spread
    static constexpr double m_orderBookImbalancePct = 60.0;  // Minimum bid/ask imbalance percentage
    static constexpr int m_microPullbackPeriod = 5;         // Number of micro-candles to analyze
    static constexpr double m_microPullbackPct = 0.4;       // Maximum pullback percentage (40%)
    static constexpr int m_tapeWindow = 20;                  // Number of recent tape prints to analyze
    
    // Main filtering method
    bool passesInitialFilters(const std::vector<time_ordered_tick_buffer::Candle>& candles, 
                             double vwap, double ema9, double ema26, double alma, double rsi);
    
    // Individual filter methods
    bool checkShortPeriodSupertrend(const std::vector<time_ordered_tick_buffer::Candle>& candles);
    bool checkVolumeSurge(const std::vector<time_ordered_tick_buffer::Candle>& candles);
    bool checkVWAPDistance(double currentPrice, double vwap);
    bool checkRSI(const std::vector<time_ordered_tick_buffer::Candle>& candles);
    bool checkEmaCross(double ema9, double ema26);
    bool checkAlmaBelowEmas(double alma, double ema9, double ema26);
    
    // Entry signal confirmation methods (read from ring buffer)
    bool checkBidAskSpread();
    bool checkOrderBookImbalance();
    bool checkMicroPullback();
    bool checkTapeMomentum();
    
    // Helper methods to extract data from ring buffer
    OrderBookSnapshot getCurrentOrderBook();
    std::vector<TapePrint> getRecentTapeData();
    std::vector<MicroCandle> getRecentMicroCandles();
    std::vector<time_ordered_tick_buffer::Candle> getCandlesFromBuffer();
};

} // namespace ring_buffer_trade_handler

#endif // RING_BUFFER_TRADE_HANDLER_HPP