#ifndef TECHNICAL_CALCULATOR_HPP
#define TECHNICAL_CALCULATOR_HPP

#include <string>
#include <vector>
#include <mutex>
#include <cmath>

namespace technical_calculator {

//-----------------------------------------------------------------------------------
// 0) Configuration Constants (Top-of-file for Granular Control)
//    Adjust these default values as needed to "dial in" your strategy.
//-----------------------------------------------------------------------------------
namespace config {
    // -- ScalpingFilter defaults --
    static constexpr double SUPERTREND_MULTIPLIER   = 1.5;  // e.g. 1.5
    static constexpr int    SUPERTREND_PERIOD       = 3;    // e.g. 3 bars
    static constexpr double VOLUME_SURGE_THRESHOLD  = 1.5;  // e.g. 1.5 => 150%
    static constexpr double MIN_VWAP_DISTANCE_PCT   = 0.2;  // e.g. 0.2%
    static constexpr double MAX_VWAP_DISTANCE_PCT   = 1.0;  // e.g. 1.0%

    // -- EntrySignal defaults --
    static constexpr double MAX_SPREAD_THRESHOLD    = 0.03;   // e.g. 3 cents
    static constexpr double ORDERBOOK_IMBALANCE_PCT = 130.0;  // e.g. 130% => strong bullish imbalance
    static constexpr int    MICRO_PULLBACK_PERIOD   = 3;      // last 3 micro candles
    static constexpr double MICRO_PULLBACK_PCT      = 0.3;    // up to 30% pullback from local high
    static constexpr int    TAPE_WINDOW             = 10;     // last 10 prints to check
}

//-----------------------------------------------------------------------------------
// 1) Data Structures for Candle, Order Book, Tape, etc.
//-----------------------------------------------------------------------------------

/**
 * Candle struct
 * 
 * A data structure to represent OHLCV (Open, High, Low, Close, Volume) data.
 */
struct Candle {
    double open;
    double high;
    double low;
    double close;
    double volume;
    
    // Constructor for convenience
    Candle(double o = 0.0, double h = 0.0, double l = 0.0, double c = 0.0, double v = 0.0) 
        : open(o), high(h), low(l), close(c), volume(v) {}
};

/**
 * OrderBookSnapshot struct
 * 
 * A snapshot of Level II (order book) data showing current market depth.
 */
struct OrderBookSnapshot {
    double bestBidPrice;
    double bestAskPrice;
    double totalBidSize; // aggregated size across multiple bid levels
    double totalAskSize; // aggregated size across multiple ask levels
    
    // Constructor for convenience
    OrderBookSnapshot(double bid = 0.0, double ask = 0.0, double bidSize = 0.0, double askSize = 0.0)
        : bestBidPrice(bid), bestAskPrice(ask), totalBidSize(bidSize), totalAskSize(askSize) {}
};

/**
 * TapePrint struct
 * 
 * Information about a single trade execution on the time & sales feed.
 */
struct TapePrint {
    double tradePrice;
    double tradeVolume;
    bool isBuy; // True if trade hit the ask, false if it hit the bid
    
    // Constructor for convenience
    TapePrint(double price = 0.0, double volume = 0.0, bool buy = false)
        : tradePrice(price), tradeVolume(volume), isBuy(buy) {}
};

/**
 * MicroCandle struct
 * 
 * A simplified candle for short timeframe analysis.
 */
struct MicroCandle {
    double high;
    double low;
    double close;
    
    // Constructor for convenience
    MicroCandle(double h = 0.0, double l = 0.0, double c = 0.0)
        : high(h), low(l), close(c) {}
};

//-----------------------------------------------------------------------------------
// 2) Forward Declarations (classes):
//-----------------------------------------------------------------------------------

class ScalpingFilter;
class EntrySignal;

/**
 * TechnicalCalculator class
 * 
 * This class provides technical analysis capabilities for market data.
 * It processes raw price and volume data to calculate various technical
 * indicators and trading signals.
 */
class TechnicalCalculator {
private:
    // Mutex for thread safety (use if needed)
    std::mutex m_calculationMutex;
    
    // Filter and entry signal objects
    ScalpingFilter* m_scalpingFilter;
    EntrySignal* m_entrySignal;
    
    // Private helper method
    std::vector<Candle> convertToCandles(const std::vector<double>& prices, 
                                         const std::vector<double>& volumes) const;
    
public:
    // Constructor and destructor
    TechnicalCalculator();
    ~TechnicalCalculator();
    
    // Main calculation methods
    bool isValidTradingCandidate(const std::vector<double>& prices, 
                                 const std::vector<double>& volumes, 
                                 double vwap) const;
    
    // Secondary confirmation for entry opportunities
    bool confirmEntryOpportunity(const OrderBookSnapshot& orderBook,
                                 const std::vector<TapePrint>& tapeData,
                                 const std::vector<MicroCandle>& microCandles,
                                 double currentPrice) const;
    
    // Technical indicators
    double calculateATR(const std::vector<double>& highPrices,
                        const std::vector<double>& lowPrices, 
                        const std::vector<double>& closePrices, 
                        int period) const;
    
    double calculateVWAP(const std::vector<double>& prices, 
                         const std::vector<double>& volumes) const;
    
    double calculateEMA(const std::vector<double>& prices, 
                        int period) const;
    
    // New advanced indicators
    double calculateChaikinOscillator(const std::vector<double>& highPrices,
                                      const std::vector<double>& lowPrices,
                                      const std::vector<double>& closePrices,
                                      const std::vector<double>& volumes,
                                      int fastPeriod = 3,
                                      int slowPeriod = 10) const;
    
    double calculateALMA(const std::vector<double>& prices,
                         int windowSize = 9,
                         double offset = 0.85,
                         double sigma = 6.0) const;
};

/**
 * ScalpingFilter class
 * 
 * This class performs initial filtering of stocks using technical indicators
 * to identify potential scalping candidates.
 */
class ScalpingFilter {
public:
    // Constructor
    ScalpingFilter(double supertrendMultiplier = 1.5,
                   int supertrendPeriod = 3,
                   double volumeSurgeThreshold = 1.5,
                   double minVWAPDistancePct = 0.2,
                   double maxVWAPDistancePct = 1.0);
    
    // Main filtering method
    bool passesInitialFilters(const std::vector<Candle>& candles, double vwap);

private:
    // Filter parameters
    double m_supertrendMultiplier;
    int m_supertrendPeriod;
    double m_volumeSurgeThreshold;
    double m_minVWAPDistancePct;
    double m_maxVWAPDistancePct;
    
    // Filter methods
    bool checkShortPeriodSupertrend(const std::vector<Candle>& candles);
    double computeATR(const std::vector<Candle>& candles, int period);
    bool checkVolumeSurge(const std::vector<Candle>& candles);
    bool checkVWAPDistance(double currentPrice, double vwap);
};

/**
 * EntrySignal class
 * 
 * This class performs secondary checks on stocks that have passed the initial
 * ScalpingFilter to confirm optimal entry points.
 */
class EntrySignal {
public:
    // Constructor
    EntrySignal(double maxSpreadThreshold = 0.03,
                double orderBookImbalancePct = 130.0,
                int microPullbackPeriod = 3,
                double microPullbackPct = 0.3,
                int tapeWindow = 10);
    
    // Main confirmation method
    bool confirmEntry(const OrderBookSnapshot& orderBook,
                      const std::vector<TapePrint>& tape,
                      const std::vector<MicroCandle>& microCandles,
                      double currentPrice);
    
private:
    // Parameters
    double m_maxSpreadThreshold;
    double m_orderBookImbalancePct;
    int m_microPullbackPeriod;
    double m_microPullbackPct;
    int m_tapeWindow;
    
    // Check methods
    bool checkBidAskSpread(const OrderBookSnapshot& ob);
    bool checkOrderBookImbalance(const OrderBookSnapshot& ob);
    bool checkMicroPullback(const std::vector<MicroCandle>& microCandles);
    bool checkTapeMomentum(const std::vector<TapePrint>& tape);
};

} // namespace technical_calculator

#endif // TECHNICAL_CALCULATOR_HPP
