#ifndef TECHNICAL_CALCULATOR_HPP
#define TECHNICAL_CALCULATOR_HPP

#include <string>
#include <vector>
#include <mutex>
#include <cmath>
#include <map>

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
    
    // -- RSI with SMA Overlay defaults --
    static constexpr int    RSI_PERIOD              = 14;   // Standard 14-period RSI
    static constexpr int    RSI_SMA_PERIOD          = 14;   // 14-period SMA of RSI
    static constexpr double RSI_UPTREND_THRESHOLD   = 1.25; // 125% ratio threshold
    
    // -- EMA Cross defaults --
    static constexpr int    FAST_EMA_PERIOD         = 9;    // 9-period EMA (faster)
    static constexpr int    SLOW_EMA_PERIOD         = 26;   // 26-period EMA (slower)
    
    // -- Chaikin Oscillator defaults --
    static constexpr int    CHAIKIN_FAST_PERIOD     = 3;    // 3-period EMA of A/D line (3 minutes)
    static constexpr int    CHAIKIN_SLOW_PERIOD     = 10;   // 10-period EMA of A/D line (10 minutes)
    
    // -- ALMA defaults --
    static constexpr int    ALMA_WINDOW_SIZE        = 9;    // 9-period window (9 one-minute bars)
    static constexpr double ALMA_SIGMA              = 0.85; // Responsiveness control (range 0.1-1.0)
    static constexpr double ALMA_OFFSET             = 6.0;  // Phase shift (range 0-10)
    
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
                         double sigma = 0.85,
                         double offset = 6.0) const;
    
    // New RSI-related methods
    double calculateRSI(const std::vector<double>& closePrices, 
                        int period = 14) const;
    
    double calculateSMA(const std::vector<double>& data, 
                        int period) const;
                        
    // RSI with SMA overlay check
    bool checkRSIwithSMAOverlay(const std::vector<double>& closePrices,
                                double currentPrice,
                                int rsiPeriod = 14,
                                int smaPeriod = 14) const;
                                
    // Calculate multiple EMAs and return them in a map
    std::map<int, double> calculateMultipleEMAs(
        const std::vector<double>& prices,
        const std::vector<int>& periods) const;
        
    // General-purpose EMA cross check
    // Verifies if faster EMA is above slower EMA (bullish cross)
    // Can be used for any analysis independent of the filtering pipeline
    bool checkEmaCross(
        const std::vector<double>& prices,
        int fastPeriod = 9,
        int slowPeriod = 26) const;
    
    // Format Chaikin Oscillator value in thousands (K) for display
    std::string formatChaikinForDisplay(double chaikinValue) const;
    
    // Check if Chaikin Oscillator indicates positive momentum
    bool isChaikinPositive(
        const std::vector<double>& highPrices,
        const std::vector<double>& lowPrices,
        const std::vector<double>& closePrices,
        const std::vector<double>& volumes,
        int fastPeriod = 3,
        int slowPeriod = 10) const;
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
                   double maxVWAPDistancePct = 1.0,
                   int rsiPeriod = 14,
                   int rsiSmaPeriod = 14,
                   double rsiUptrendThreshold = 1.25,
                   int fastEmaPeriod = 9,
                   int slowEmaPeriod = 26);
    
    // Main filtering method
    bool passesInitialFilters(const std::vector<Candle>& candles, double vwap);

private:
    // Filter parameters
    double m_supertrendMultiplier;
    int m_supertrendPeriod;
    double m_volumeSurgeThreshold;
    double m_minVWAPDistancePct;
    double m_maxVWAPDistancePct;
    int m_rsiPeriod;
    int m_rsiSmaPeriod;
    double m_rsiUptrendThreshold;
    int m_fastEmaPeriod;
    int m_slowEmaPeriod;
    
    // Filter methods
    bool checkShortPeriodSupertrend(const std::vector<Candle>& candles);
    double computeATR(const std::vector<Candle>& candles, int period);
    bool checkVolumeSurge(const std::vector<Candle>& candles);
    bool checkVWAPDistance(double currentPrice, double vwap);
    bool checkRSIwithSMAOverlay(const std::vector<Candle>& candles);
    
    // Specialized EMA cross check for the filtering pipeline
    // Uses the configured fast/slow EMA periods (typically 9/26)
    bool checkEmaCross(const std::vector<Candle>& candles);
    
    // Check if ALMA is below both EMAs (ALMA < EMA9 & ALMA < EMA26)
    bool checkAlmaBelowEmas(const std::vector<Candle>& candles);
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
