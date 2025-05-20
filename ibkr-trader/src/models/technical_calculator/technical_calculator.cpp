#include "technical_calculator.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace technical_calculator {

//-----------------------------------------------------------------------------------
// ScalpingFilter Implementations
//-----------------------------------------------------------------------------------

ScalpingFilter::ScalpingFilter(double supertrendMultiplier,
                               int supertrendPeriod,
                               double volumeSurgeThreshold,
                               double minVWAPDistancePct,
                               double maxVWAPDistancePct)
    : m_supertrendMultiplier(supertrendMultiplier),
      m_supertrendPeriod(supertrendPeriod),
      m_volumeSurgeThreshold(volumeSurgeThreshold),
      m_minVWAPDistancePct(minVWAPDistancePct),
      m_maxVWAPDistancePct(maxVWAPDistancePct)
{
    // You can add debug prints or initialization logs here if needed
}

// Main filtering method
bool ScalpingFilter::passesInitialFilters(const std::vector<Candle>& candles, double vwap) {
    if (candles.size() < static_cast<size_t>(m_supertrendPeriod)) {
        return false;  // Not enough data to compute short supertrend
    }

    // 1) Short-Period Supertrend
    bool isSupertrendBullish = checkShortPeriodSupertrend(candles);
    if (!isSupertrendBullish) {
        return false;
    }

    // 2) Volume Surge
    bool volumeOkay = checkVolumeSurge(candles);
    if (!volumeOkay) {
        return false;
    }

    // 3) VWAP Distance
    bool withinVWAPRange = checkVWAPDistance(candles.back().close, vwap);
    if (!withinVWAPRange) {
        return false;
    }

    // All checks passed => valid candidate
    return true;
}

// (Private) Check methods -----------------------------------------

bool ScalpingFilter::checkShortPeriodSupertrend(const std::vector<Candle>& candles) {
    double atr = computeATR(candles, m_supertrendPeriod);

    const Candle& lastCandle = candles.back();
    double midPrice = (lastCandle.high + lastCandle.low) / 2.0;
    double upperBand = midPrice + (m_supertrendMultiplier * atr);
    double lowerBand = midPrice - (m_supertrendMultiplier * atr);

    // If the close is above the "lower band," call it bullish
    bool bullish = (lastCandle.close > lowerBand);
    return bullish;
}

double ScalpingFilter::computeATR(const std::vector<Candle>& candles, int period) {
    int n = static_cast<int>(candles.size());
    if (n < period) {
        return 0.0;
    }

    double sumTR = 0.0;
    for (int i = n - period; i < n; ++i) {
        if (i == 0) continue;
        const Candle& curr = candles[i];
        const Candle& prev = candles[i - 1];
        double tr1 = curr.high - curr.low;
        double tr2 = std::fabs(curr.high - prev.close);
        double tr3 = std::fabs(curr.low  - prev.close);
        double trueRange = std::max({tr1, tr2, tr3});
        sumTR += trueRange;
    }

    return sumTR / static_cast<double>(period);
}

bool ScalpingFilter::checkVolumeSurge(const std::vector<Candle>& candles) {
    int n = static_cast<int>(candles.size());
    if (n < 10) {
        // Not enough bars for a 10-bar average
        return false;
    }

    double currentVolume = candles.back().volume;

    // Average volume of last 10 bars (excluding the most recent)
    double sumVolume = 0.0;
    for (int i = n - 11; i < n - 1; ++i) {
        sumVolume += candles[i].volume;
    }
    double avgVolume = sumVolume / 10.0;
    if (avgVolume <= 0.0) {
        return false; 
    }

    double ratio = currentVolume / avgVolume;
    return (ratio > m_volumeSurgeThreshold);
}

bool ScalpingFilter::checkVWAPDistance(double currentPrice, double vwap) {
    if (vwap <= 0.0) {
        return false; 
    }
    double distancePct = ((currentPrice - vwap) / vwap) * 100.0;
    return (distancePct >= m_minVWAPDistancePct &&
            distancePct <= m_maxVWAPDistancePct);
}

//-----------------------------------------------------------------------------------
// EntrySignal Implementations
//-----------------------------------------------------------------------------------

EntrySignal::EntrySignal(double maxSpreadThreshold,
                         double orderBookImbalancePct,
                         int microPullbackPeriod,
                         double microPullbackPct,
                         int tapeWindow)
    : m_maxSpreadThreshold(maxSpreadThreshold),
      m_orderBookImbalancePct(orderBookImbalancePct),
      m_microPullbackPeriod(microPullbackPeriod),
      m_microPullbackPct(microPullbackPct),
      m_tapeWindow(tapeWindow)
{
    // constructor
}

bool EntrySignal::confirmEntry(const OrderBookSnapshot& orderBook,
                               const std::vector<TapePrint>& tape,
                               const std::vector<MicroCandle>& microCandles,
                               double currentPrice)
{
    // 1) Spread Check
    if (!checkBidAskSpread(orderBook)) {
        return false;
    }

    // 2) Order Book Imbalance
    if (!checkOrderBookImbalance(orderBook)) {
        return false;
    }

    // 3) Micro Pullback
    if (!checkMicroPullback(microCandles)) {
        return false;
    }

    // 4) Tape Momentum
    if (!checkTapeMomentum(tape)) {
        return false;
    }

    // If all checks pass, we consider it a valid entry signal
    return true;
}

// (Private) Check methods -----------------------------------------

bool EntrySignal::checkBidAskSpread(const OrderBookSnapshot& ob) {
    double spread = ob.bestAskPrice - ob.bestBidPrice;
    return (spread <= m_maxSpreadThreshold);
}

bool EntrySignal::checkOrderBookImbalance(const OrderBookSnapshot& ob) {
    if (ob.totalAskSize <= 0.0) {
        return false; 
    }
    double ratio = (ob.totalBidSize / ob.totalAskSize) * 100.0;
    return (ratio >= m_orderBookImbalancePct);
}

bool EntrySignal::checkMicroPullback(const std::vector<MicroCandle>& microCandles) {
    if (microCandles.size() < static_cast<size_t>(m_microPullbackPeriod)) {
        // Not enough micro-candles; skip or be lenient
        return true;
    }

    int startIndex = static_cast<int>(microCandles.size()) - m_microPullbackPeriod;
    double highestHigh = microCandles[startIndex].high;
    double lowestLow   = microCandles[startIndex].low;

    for (int i = startIndex; i < (int)microCandles.size(); ++i) {
        highestHigh = std::max(highestHigh, microCandles[i].high);
        lowestLow   = std::min(lowestLow,   microCandles[i].low);
    }

    double range = highestHigh - lowestLow;
    if (range <= 0.0) {
        return true; 
    }

    double lastClose = microCandles.back().close;
    double pullbackFromHigh = highestHigh - lastClose;
    double pullbackRatio    = pullbackFromHigh / range; 

    // Example logic: we want a moderate pullback between 10% and m_microPullbackPct
    if (pullbackRatio >= 0.1 && pullbackRatio <= m_microPullbackPct) {
        return true; 
    }
    return false;
}

bool EntrySignal::checkTapeMomentum(const std::vector<TapePrint>& tape) {
    if (tape.empty()) {
        return true; 
    }

    int startIndex = std::max(0, (int)tape.size() - m_tapeWindow);
    int buyCount = 0;
    int totalCount = 0;

    for (int i = startIndex; i < (int)tape.size(); ++i) {
        if (tape[i].isBuy) {
            buyCount++;
        }
        totalCount++;
    }

    if (totalCount == 0) {
        return false;
    }
    double buyPct = (static_cast<double>(buyCount) / totalCount) * 100.0;
    // Example threshold: at least 60% buys => bullish momentum
    return (buyPct >= 60.0);
}

//-----------------------------------------------------------------------------------
// TechnicalCalculator Implementations
//-----------------------------------------------------------------------------------

TechnicalCalculator::TechnicalCalculator() {
    // By default, use the config parameters for filter & entry signals:
    m_scalpingFilter = new ScalpingFilter();
    m_entrySignal    = new EntrySignal();
}

TechnicalCalculator::~TechnicalCalculator() {
    if (m_scalpingFilter) {
        delete m_scalpingFilter;
        m_scalpingFilter = nullptr;
    }
    if (m_entrySignal) {
        delete m_entrySignal;
        m_entrySignal = nullptr;
    }
}

// Convert price and volume data to Candle objects
std::vector<Candle> TechnicalCalculator::convertToCandles(
    const std::vector<double>& prices, 
    const std::vector<double>& volumes) const 
{
    std::vector<Candle> candles;
    
    if (prices.empty() || volumes.empty() || prices.size() != volumes.size()) {
        return candles; 
    }
    
    // For simplicity, use the same price for OHLC
    for (size_t i = 0; i < prices.size(); ++i) {
        Candle candle;
        candle.open = candle.high = candle.low = candle.close = prices[i];
        candle.volume = volumes[i];
        candles.push_back(candle);
    }
    return candles;
}

// Main method to determine if a stock is a valid trading candidate (Initial Filter)
bool TechnicalCalculator::isValidTradingCandidate(
    const std::vector<double>& prices, 
    const std::vector<double>& volumes, 
    double vwap) const 
{
    // Convert raw data to candles
    std::vector<Candle> candles = convertToCandles(prices, volumes);
    // Apply the ScalpingFilter
    return m_scalpingFilter->passesInitialFilters(candles, vwap);
}

// Secondary check: confirm we should actually enter (EntrySignal)
bool TechnicalCalculator::confirmEntryOpportunity(
    const OrderBookSnapshot& orderBook,
    const std::vector<TapePrint>& tapeData,
    const std::vector<MicroCandle>& microCandles,
    double currentPrice) const
{
    return m_entrySignal->confirmEntry(orderBook, tapeData, microCandles, currentPrice);
}

// Calculate ATR (Average True Range)
double TechnicalCalculator::calculateATR(
    const std::vector<double>& highPrices,
    const std::vector<double>& lowPrices, 
    const std::vector<double>& closePrices, 
    int period) const 
{
    if (highPrices.size() < 2 || lowPrices.size() < 2 || closePrices.size() < 2 ||
        highPrices.size() != lowPrices.size() || highPrices.size() != closePrices.size()) {
        return 0.0;
    }
    
    double sumTR = 0.0;
    for (size_t i = 1; i < highPrices.size(); ++i) {
        double tr1 = highPrices[i] - lowPrices[i];
        double tr2 = std::fabs(highPrices[i] - closePrices[i-1]);
        double tr3 = std::fabs(lowPrices[i] - closePrices[i-1]);
        double trueRange = std::max({tr1, tr2, tr3});
        sumTR += trueRange;
    }
    
    int actualPeriod = std::min(period, (int)(highPrices.size() - 1));
    return sumTR / static_cast<double>(actualPeriod);
}

// Calculate VWAP (Volume Weighted Average Price)
double TechnicalCalculator::calculateVWAP(
    const std::vector<double>& prices, 
    const std::vector<double>& volumes) const 
{
    if (prices.empty() || volumes.empty() || prices.size() != volumes.size()) {
        return 0.0;
    }
    
    double sumPV = 0.0;
    double sumV = 0.0;
    for (size_t i = 0; i < prices.size(); ++i) {
        sumPV += prices[i] * volumes[i];
        sumV  += volumes[i];
    }
    
    return (sumV > 0.0) ? (sumPV / sumV) : 0.0;
}

// Calculate EMA (Exponential Moving Average)
double TechnicalCalculator::calculateEMA(
    const std::vector<double>& prices, 
    int period) const 
{
    if (prices.size() < (size_t)period) {
        return 0.0;
    }
    
    double multiplier = 2.0 / (period + 1.0);
    
    // Start with an SMA of the first 'period' elements
    double ema = std::accumulate(prices.begin(), prices.begin() + period, 0.0) / period;
    
    // Then apply EMA for the remaining data
    for (size_t i = period; i < prices.size(); ++i) {
        ema = (prices[i] - ema) * multiplier + ema;
    }
    
    return ema;
}

//------------------------------------------------------------------------------
// (New) Calculate Chaikin Oscillator
//------------------------------------------------------------------------------
// * The Chaikin Oscillator is typically the difference between two EMAs
//   (fast and slow) of the Accumulation/Distribution (A/D) line.
// * The A/D line (per bar) = ( (Close - Low) - (High - Close) ) / (High - Low ) * Volume
//   (Sometimes known as the Chaikin Money Flow multiplier * Volume)
// * Then we compute two EMAs of that A/D line: e.g., 3-period EMA and 10-period EMA
//   The difference = Chaikin Oscillator.
double TechnicalCalculator::calculateChaikinOscillator(
    const std::vector<double>& highPrices,
    const std::vector<double>& lowPrices,
    const std::vector<double>& closePrices,
    const std::vector<double>& volumes,
    int fastPeriod,
    int slowPeriod
) const
{
    // Safety checks
    size_t n = highPrices.size();
    if (n < (size_t)std::max(fastPeriod, slowPeriod) || 
        n != lowPrices.size() || n != closePrices.size() || n != volumes.size()) {
        return 0.0; // Not enough data or mismatch
    }

    // 1) Compute the A/D line for each bar
    std::vector<double> adLine(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        double range = (highPrices[i] - lowPrices[i]);
        if (range == 0.0) {
            // Avoid division by zero; treat as 0 or skip
            adLine[i] = 0.0;
        } else {
            double moneyFlowMultiplier = ((closePrices[i] - lowPrices[i]) 
                                          - (highPrices[i] - closePrices[i])) / range;
            adLine[i] = moneyFlowMultiplier * volumes[i];
        }
    }

    // 2) Compute fast & slow EMAs of A/D line
    double fastEMA = calculateEMA(adLine, fastPeriod);
    double slowEMA = calculateEMA(adLine, slowPeriod);

    // 3) The Chaikin Oscillator = fastEMA - slowEMA
    return (fastEMA - slowEMA);
}

//------------------------------------------------------------------------------
// (New) Calculate ALMA (Arnaud Legoux Moving Average)
//------------------------------------------------------------------------------
// * ALMA aims to reduce lag while giving a smooth average.
// * Typically has parameters: windowSize, offset, sigma
//   - offset is usually between 0 and 1, controlling the "center" of the weights
//   - sigma controls the spread of the weights (like standard deviation).
//   - windowSize is the number of bars to consider.
//
// * The formula involves creating a set of weights based on a Gaussian distribution
//   centered around "offset * (windowSize - 1)" with "sigma" controlling width.
// * Then we multiply each price by the corresponding weight and sum.
//
// For typical usage in short-term trading, you might do something like:
//   ALMA(9, offset=0.85, sigma=6) 
//   or similar, depending on backtesting.
double TechnicalCalculator::calculateALMA(
    const std::vector<double>& prices,
    int windowSize,
    double offset,
    double sigma
) const
{
    // Need at least windowSize data points
    if ((int)prices.size() < windowSize || windowSize <= 0) {
        return 0.0;
    }

    // We'll calculate ALMA on the last 'windowSize' bars
    double m = offset * (windowSize - 1);
    double s = (double)windowSize / sigma;  // standard deviation factor

    // Build weights
    std::vector<double> weights(windowSize, 0.0);
    double sumW = 0.0;
    for (int i = 0; i < windowSize; ++i) {
        double x = (double)i - m;
        double w = std::exp(- (x * x) / (2.0 * s * s));
        weights[i] = w;
        sumW += w;
    }

    // Weighted sum of the last 'windowSize' prices
    double weightedSum = 0.0;
    int startIdx = static_cast<int>(prices.size()) - windowSize;
    for (int i = 0; i < windowSize; ++i) {
        weightedSum += prices[startIdx + i] * weights[i];
    }

    // Normalize
    return (weightedSum / sumW);
}

//-----------------------------------------------------------------------------------
// Notes on Using Chaikin Oscillator & ALMA in Real-Time Trading
//-----------------------------------------------------------------------------------
/*
   1) Chaikin Oscillator:
      - This helps detect if *money flow* is supporting a price move.
      - If you see a sudden iceberg order and big volume push, 
        the A/D line (and thus the Chaikin Osc) should spike positive 
        if it's real buying interest. 
      - If you're in a position that's "flatlining" but Chaikin remains above 0,
        it's often safe to keep holding (the money flow hasn't reversed).
      - A sudden turn negative might indicate distribution 
        => consider tightening stops or exiting.

   2) ALMA:
      - Use as a *smoother moving average* to track short-term trend.
      - If you're forced to hold longer than 5 minutes, 
        you can watch if the price stays above ALMA(9, 0.85, 6) 
        (for example) to confirm ongoing bullishness.
      - Once price closes below ALMA repeatedly, 
        it could be a warning sign to exit.

   The big picture: 
   - If you see an iceberg pushing price up *and* the Chaikin Osc is surging, 
     it's a sign "big money" is stepping in, so you may want to hold longer 
     or even add if your scalping risk parameters allow.
   - If an iceberg is suspected (huge hidden liquidity on the bid/ask) but 
     Chaikin doesn't respond (stays low or negative), 
     it might be a "fake" or short-lived bounce.

   Example usage in code:
     ...
     // 1) If your filters say "enter," you do so.
     // 2) If you hold for >5 mins, you call:
     double chaikinVal = myTechCalc.calculateChaikinOscillator(highs, lows, closes, volumes, 3, 10);
     double almaVal    = myTechCalc.calculateALMA(closes, 9, 0.85, 6);

     // Then interpret:
     if (chaikinVal > 0.0 && closes.back() > almaVal) {
        // bullish money flow + price above ALMA => safe to hold
     } else {
        // losing money flow or price below ALMA => consider exit
     }
     ...
*/
} // namespace technical_calculator
