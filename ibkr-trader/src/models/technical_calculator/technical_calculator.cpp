// #include "technical_calculator.hpp"
// #include <iostream>
// #include <cmath>
// #include <algorithm>
// #include <numeric>

// namespace technical_calculator {

// //-----------------------------------------------------------------------------------
// // ScalpingFilter Implementations
// //-----------------------------------------------------------------------------------

// ScalpingFilter::ScalpingFilter(double supertrendMultiplier,
//                                int supertrendPeriod,
//                                double volumeSurgeThreshold,
//                                double minVWAPDistancePct,
//                                double maxVWAPDistancePct,
//                                int rsiPeriod,
//                                int rsiSmaPeriod,
//                                double rsiUptrendThreshold,
//                                int fastEmaPeriod,
//                                int slowEmaPeriod)
//     : m_supertrendMultiplier(supertrendMultiplier),
//       m_supertrendPeriod(supertrendPeriod),
//       m_volumeSurgeThreshold(volumeSurgeThreshold),
//       m_minVWAPDistancePct(minVWAPDistancePct),
//       m_maxVWAPDistancePct(maxVWAPDistancePct),
//       m_rsiPeriod(rsiPeriod),
//       m_rsiSmaPeriod(rsiSmaPeriod),
//       m_rsiUptrendThreshold(rsiUptrendThreshold),
//       m_fastEmaPeriod(fastEmaPeriod),
//       m_slowEmaPeriod(slowEmaPeriod)
// {
//     // You can add debug prints or initialization logs here if needed
// }

// // Main filtering method
// bool ScalpingFilter::passesInitialFilters(const std::vector<Candle>& candles, double vwap) {
//     if (candles.size() < static_cast<size_t>(m_supertrendPeriod)) {
//         return false;  // Not enough data to compute short supertrend
//     }

//     // 1) Short-Period Supertrend
//     bool isSupertrendBullish = checkShortPeriodSupertrend(candles);
//     if (!isSupertrendBullish) {
//         return false;
//     }

//     // 2) Volume Surge
//     bool volumeOkay = checkVolumeSurge(candles);
//     if (!volumeOkay) {
//         return false;
//     }

//     // 3) VWAP Distance
//     bool withinVWAPRange = checkVWAPDistance(candles.back().close, vwap);
//     if (!withinVWAPRange) {
//         return false;
//     }
    
//     // 4) RSI with SMA Overlay - check for upward pressure
//     bool hasRsiUptrendPressure = checkRSIwithSMAOverlay(candles);
//     if (!hasRsiUptrendPressure) {
//         return false;
//     }
    
//     // 5) EMA Cross - check if fast EMA (9) is above slow EMA (26)
//     bool isFastEmaAboveSlow = checkEmaCross(candles);
//     if (!isFastEmaAboveSlow) {
//         return false;
//     }
    
//     // 6) ALMA below EMAs - check if ALMA(9, 0.85, 6) is below both EMA9 and EMA26
//     bool isAlmaBelowEmas = checkAlmaBelowEmas(candles);
//     if (!isAlmaBelowEmas) {
//         return false;
//     }

//     // All checks passed => valid candidate
//     return true;
// }

// // (Private) Check methods -----------------------------------------

// bool ScalpingFilter::checkShortPeriodSupertrend(const std::vector<Candle>& candles) {
//     double atr = computeATR(candles, m_supertrendPeriod);

//     const Candle& lastCandle = candles.back();
//     double midPrice = (lastCandle.high + lastCandle.low) / 2.0;
//     double upperBand = midPrice + (m_supertrendMultiplier * atr);
//     double lowerBand = midPrice - (m_supertrendMultiplier * atr);

//     // If the close is above the "lower band," call it bullish
//     bool bullish = (lastCandle.close > lowerBand);
//     return bullish;
// }

// double ScalpingFilter::computeATR(const std::vector<Candle>& candles, int period) {
//     int n = static_cast<int>(candles.size());
//     if (n < period) {
//         return 0.0;
//     }

//     double sumTR = 0.0;
//     for (int i = n - period; i < n; ++i) {
//         if (i == 0) continue;
//         const Candle& curr = candles[i];
//         const Candle& prev = candles[i - 1];
//         double tr1 = curr.high - curr.low;
//         double tr2 = std::fabs(curr.high - prev.close);
//         double tr3 = std::fabs(curr.low  - prev.close);
//         double trueRange = std::max({tr1, tr2, tr3});
//         sumTR += trueRange;
//     }

//     return sumTR / static_cast<double>(period);
// }

// bool ScalpingFilter::checkVolumeSurge(const std::vector<Candle>& candles) {
//     int n = static_cast<int>(candles.size());
//     if (n < 10) {
//         // Not enough bars for a 10-bar average
//         return false;
//     }

//     double currentVolume = candles.back().volume;

//     // Average volume of last 10 bars (excluding the most recent)
//     double sumVolume = 0.0;
//     for (int i = n - 11; i < n - 1; ++i) {
//         sumVolume += candles[i].volume;
//     }
//     double avgVolume = sumVolume / 10.0;
//     if (avgVolume <= 0.0) {
//         return false; 
//     }

//     double ratio = currentVolume / avgVolume;
//     return (ratio > m_volumeSurgeThreshold);
// }

// bool ScalpingFilter::checkVWAPDistance(double currentPrice, double vwap) {
//     if (vwap <= 0.0) {
//         return false; 
//     }
//     double distancePct = ((currentPrice - vwap) / vwap) * 100.0;
//     return (distancePct >= m_minVWAPDistancePct &&
//             distancePct <= m_maxVWAPDistancePct);
// }

// bool ScalpingFilter::checkRSIwithSMAOverlay(const std::vector<Candle>& candles) {
//     // Extract close prices from candles
//     std::vector<double> closePrices;
//     for (const auto& candle : candles) {
//         closePrices.push_back(candle.close);
//     }
    
//     // Minimum data required
//     if (closePrices.size() <= (size_t)(m_rsiPeriod + m_rsiSmaPeriod)) {
//         return false; // Not enough data points
//     }
    
//     // Calculate RSI values
//     std::vector<double> rsiValues;
    
//     // Calculate initial RSI values (need m_rsiPeriod+1 prices to get first RSI)
//     for (size_t i = m_rsiPeriod; i < closePrices.size(); ++i) {
//         std::vector<double> subRange(closePrices.begin(), closePrices.begin() + i + 1);
        
//         // Calculate gains and losses
//         double sumGain = 0.0;
//         double sumLoss = 0.0;
        
//         for (size_t j = 1; j <= m_rsiPeriod; ++j) {
//             double change = subRange[j] - subRange[j-1];
//             if (change > 0) {
//                 sumGain += change;
//             } else {
//                 sumLoss += std::abs(change);
//             }
//         }
        
//         double avgGain = sumGain / m_rsiPeriod;
//         double avgLoss = sumLoss / m_rsiPeriod;
        
//         // For the current point, apply the smoothed formula if we have enough data
//         if (i > m_rsiPeriod) {
//             double change = subRange.back() - subRange[subRange.size()-2];
//             if (change > 0) {
//                 avgGain = (avgGain * (m_rsiPeriod - 1) + change) / m_rsiPeriod;
//                 avgLoss = (avgLoss * (m_rsiPeriod - 1)) / m_rsiPeriod;
//             } else {
//                 avgGain = (avgGain * (m_rsiPeriod - 1)) / m_rsiPeriod;
//                 avgLoss = (avgLoss * (m_rsiPeriod - 1) + std::abs(change)) / m_rsiPeriod;
//             }
//         }
        
//         double rs = (avgLoss < 0.000001) ? 100.0 : (avgGain / avgLoss);
//         double rsi = 100.0 - (100.0 / (1.0 + rs));
        
//         rsiValues.push_back(rsi);
//     }
    
//     // Check if we have enough RSI values to calculate SMA
//     if (rsiValues.size() < (size_t)m_rsiSmaPeriod) {
//         return false;
//     }
    
//     // Calculate SMA of RSI
//     double smaOfRSI = 0.0;
//     for (size_t i = rsiValues.size() - m_rsiSmaPeriod; i < rsiValues.size(); ++i) {
//         smaOfRSI += rsiValues[i];
//     }
//     smaOfRSI /= m_rsiSmaPeriod;
    
//     // Get the latest RSI and current price
//     double lastRSI = rsiValues.back();
//     double currentPrice = candles.back().close;
    
//     // Calculate the ratio of RSI to price
//     double rsiToPriceRatio = lastRSI / currentPrice;
    
//     // Check both conditions:
//     // 1. RSI > SMA (trending upward)
//     // 2. RSI is at least m_rsiUptrendThreshold of current price
//     return (lastRSI > smaOfRSI && rsiToPriceRatio >= m_rsiUptrendThreshold);
// }

// // Check if fast EMA (9) is above slow EMA (26)
// // Note: This specialized version is for use within the ScalpingFilter pipeline
// // and uses the filter's configured EMA periods. For more general use cases,
// // see the TechnicalCalculator::checkEmaCross method.
// bool ScalpingFilter::checkEmaCross(const std::vector<Candle>& candles) {
//     // Extract close prices from candles
//     std::vector<double> closePrices;
//     for (const auto& candle : candles) {
//         closePrices.push_back(candle.close);
//     }
    
//     // Need at least as many candles as the slow EMA period
//     if (closePrices.size() < static_cast<size_t>(m_slowEmaPeriod)) {
//         return false; // Not enough data
//     }
    
//     // Create a temporary TechnicalCalculator to use its EMA calculation
//     TechnicalCalculator calculator;
    
//     // Calculate fast EMA (typically 9-period) and slow EMA (typically 26-period)
//     double fastEma = calculator.calculateEMA(closePrices, m_fastEmaPeriod);
//     double slowEma = calculator.calculateEMA(closePrices, m_slowEmaPeriod);
    
//     // Check if fast EMA is above slow EMA (bullish)
//     return (fastEma > slowEma);
// }

// // Check if ALMA is below both EMAs (ALMA < EMA9 & ALMA < EMA26)
// bool ScalpingFilter::checkAlmaBelowEmas(const std::vector<Candle>& candles) {
//     // Extract close prices from candles
//     std::vector<double> closePrices;
//     for (const auto& candle : candles) {
//         closePrices.push_back(candle.close);
//     }
    
//     // Need at least as many candles as the slow EMA period
//     if (closePrices.size() < static_cast<size_t>(m_slowEmaPeriod)) {
//         return false; // Not enough data
//     }
    
//     // Create a temporary TechnicalCalculator to use its calculations
//     TechnicalCalculator calculator;
    
//     // Calculate EMAs (9 and 26)
//     double fastEma = calculator.calculateEMA(closePrices, m_fastEmaPeriod);
//     double slowEma = calculator.calculateEMA(closePrices, m_slowEmaPeriod);
    
//     // Calculate ALMA(9, 0.85, 6)
//     double alma = calculator.calculateALMA(
//         closePrices, 
//         config::ALMA_WINDOW_SIZE, 
//         config::ALMA_SIGMA, 
//         config::ALMA_OFFSET
//     );
    
//     // Check if ALMA is below both EMAs
//     return (alma < fastEma && alma < slowEma);
// }

// //-----------------------------------------------------------------------------------
// // EntrySignal Implementations
// //-----------------------------------------------------------------------------------

// EntrySignal::EntrySignal(double maxSpreadThreshold,
//                          double orderBookImbalancePct,
//                          int microPullbackPeriod,
//                          double microPullbackPct,
//                          int tapeWindow)
//     : m_maxSpreadThreshold(maxSpreadThreshold),
//       m_orderBookImbalancePct(orderBookImbalancePct),
//       m_microPullbackPeriod(microPullbackPeriod),
//       m_microPullbackPct(microPullbackPct),
//       m_tapeWindow(tapeWindow)
// {
//     // constructor
// }

// bool EntrySignal::confirmEntry(const OrderBookSnapshot& orderBook,
//                                const std::vector<TapePrint>& tape,
//                                const std::vector<MicroCandle>& microCandles,
//                                double currentPrice)
// {
//     // 1) Spread Check
//     if (!checkBidAskSpread(orderBook)) {
//         return false;
//     }

//     // 2) Order Book Imbalance
//     if (!checkOrderBookImbalance(orderBook)) {
//         return false;
//     }

//     // 3) Micro Pullback
//     if (!checkMicroPullback(microCandles)) {
//         return false;
//     }

//     // 4) Tape Momentum
//     if (!checkTapeMomentum(tape)) {
//         return false;
//     }

//     // If all checks pass, we consider it a valid entry signal
//     return true;
// }

// // // (Private) Check methods -----------------------------------------

// bool EntrySignal::checkBidAskSpread(const OrderBookSnapshot& ob) {
//     double spread = ob.bestAskPrice - ob.bestBidPrice;
//     return (spread <= m_maxSpreadThreshold);
// }

// bool EntrySignal::checkOrderBookImbalance(const OrderBookSnapshot& ob) {
//     if (ob.totalAskSize <= 0.0) {
//         return false; 
//     }
//     double ratio = (ob.totalBidSize / ob.totalAskSize) * 100.0;
//     return (ratio >= m_orderBookImbalancePct);
// }

// bool EntrySignal::checkMicroPullback(const std::vector<MicroCandle>& microCandles) {
//     if (microCandles.size() < static_cast<size_t>(m_microPullbackPeriod)) {
//         // Not enough micro-candles; skip or be lenient
//         return true;
//     }

//     int startIndex = static_cast<int>(microCandles.size()) - m_microPullbackPeriod;
//     double highestHigh = microCandles[startIndex].high;
//     double lowestLow   = microCandles[startIndex].low;

//     for (int i = startIndex; i < (int)microCandles.size(); ++i) {
//         highestHigh = std::max(highestHigh, microCandles[i].high);
//         lowestLow   = std::min(lowestLow,   microCandles[i].low);
//     }

//     double range = highestHigh - lowestLow;
//     if (range <= 0.0) {
//         return true; 
//     }

//     double lastClose = microCandles.back().close;
//     double pullbackFromHigh = highestHigh - lastClose;
//     double pullbackRatio    = pullbackFromHigh / range; 

//     // Example logic: we want a moderate pullback between 10% and m_microPullbackPct
//     if (pullbackRatio >= 0.1 && pullbackRatio <= m_microPullbackPct) {
//         return true; 
//     }
//     return false;
// }

// bool EntrySignal::checkTapeMomentum(const std::vector<TapePrint>& tape) {
//     if (tape.empty()) {
//         return true; 
//     }

//     int startIndex = std::max(0, (int)tape.size() - m_tapeWindow);
//     int buyCount = 0;
//     int totalCount = 0;

//     for (int i = startIndex; i < (int)tape.size(); ++i) {
//         if (tape[i].isBuy) {
//             buyCount++;
//         }
//         totalCount++;
//     }

//     if (totalCount == 0) {
//         return false;
//     }
//     double buyPct = (static_cast<double>(buyCount) / totalCount) * 100.0;
//     // Example threshold: at least 60% buys => bullish momentum
//     return (buyPct >= 60.0);
// }

// //-----------------------------------------------------------------------------------
// // TechnicalCalculator Implementations
// //-----------------------------------------------------------------------------------

// TechnicalCalculator::TechnicalCalculator() {
//     // By default, use the config parameters for filter & entry signals:
//     m_scalpingFilter = new ScalpingFilter(
//         config::SUPERTREND_MULTIPLIER,
//         config::SUPERTREND_PERIOD,
//         config::VOLUME_SURGE_THRESHOLD,
//         config::MIN_VWAP_DISTANCE_PCT,
//         config::MAX_VWAP_DISTANCE_PCT,
//         config::RSI_PERIOD,
//         config::RSI_SMA_PERIOD,
//         config::RSI_UPTREND_THRESHOLD,
//         config::FAST_EMA_PERIOD,
//         config::SLOW_EMA_PERIOD
//     );
//     m_entrySignal = new EntrySignal();
// }

// TechnicalCalculator::~TechnicalCalculator() {
//     if (m_scalpingFilter) {
//         delete m_scalpingFilter;
//         m_scalpingFilter = nullptr;
//     }
//     if (m_entrySignal) {
//         delete m_entrySignal;
//         m_entrySignal = nullptr;
//     }
// }

// // Convert price and volume data to Candle objects
// std::vector<Candle> TechnicalCalculator::convertToCandles(
//     const std::vector<double>& prices, 
//     const std::vector<double>& volumes) const 
// {
//     std::vector<Candle> candles;
    
//     if (prices.empty() || volumes.empty() || prices.size() != volumes.size()) {
//         return candles; 
//     }
    
//     // For simplicity, use the same price for OHLC
//     for (size_t i = 0; i < prices.size(); ++i) {
//         Candle candle;
//         candle.open = candle.high = candle.low = candle.close = prices[i];
//         candle.volume = volumes[i];
//         candles.push_back(candle);
//     }
//     return candles;
// }

// // Main method to determine if a stock is a valid trading candidate (Initial Filter)
// bool TechnicalCalculator::isValidTradingCandidate(
//     const std::vector<double>& prices, 
//     const std::vector<double>& volumes, 
//     double vwap) const 
// {
//     // Convert raw data to candles
//     std::vector<Candle> candles = convertToCandles(prices, volumes);
//     // Apply the ScalpingFilter
//     return m_scalpingFilter->passesInitialFilters(candles, vwap);
// }

// // Secondary check: confirm we should actually enter (EntrySignal)
// bool TechnicalCalculator::confirmEntryOpportunity(
//     const OrderBookSnapshot& orderBook,
//     const std::vector<TapePrint>& tapeData,
//     const std::vector<MicroCandle>& microCandles,
//     double currentPrice) const
// {
//     return m_entrySignal->confirmEntry(orderBook, tapeData, microCandles, currentPrice);
// }

// // Calculate ATR (Average True Range)
// double TechnicalCalculator::calculateATR(
//     const std::vector<double>& highPrices,
//     const std::vector<double>& lowPrices, 
//     const std::vector<double>& closePrices, 
//     int period) const 
// {
//     if (highPrices.size() < 2 || lowPrices.size() < 2 || closePrices.size() < 2 ||
//         highPrices.size() != lowPrices.size() || highPrices.size() != closePrices.size()) {
//         return 0.0;
//     }
    
//     double sumTR = 0.0;
//     for (size_t i = 1; i < highPrices.size(); ++i) {
//         double tr1 = highPrices[i] - lowPrices[i];
//         double tr2 = std::fabs(highPrices[i] - closePrices[i-1]);
//         double tr3 = std::fabs(lowPrices[i] - closePrices[i-1]);
//         double trueRange = std::max({tr1, tr2, tr3});
//         sumTR += trueRange;
//     }
    
//     int actualPeriod = std::min(period, (int)(highPrices.size() - 1));
//     return sumTR / static_cast<double>(actualPeriod);
// }

// // Calculate VWAP (Volume Weighted Average Price)
// double TechnicalCalculator::calculateVWAP(
//     const std::vector<double>& prices, 
//     const std::vector<double>& volumes) const 
// {
//     if (prices.empty() || volumes.empty() || prices.size() != volumes.size()) {
//         return 0.0;
//     }
    
//     double sumPV = 0.0;
//     double sumV = 0.0;
//     for (size_t i = 0; i < prices.size(); ++i) {
//         sumPV += prices[i] * volumes[i];
//         // 14,  // Default RSI period
//         // 14,  // Default SMA of RSI period
//         // 1.25 // Default RSI uptrend threshold (125%)
//         sumV += volumes[i];
//     }
//     return (sumV > 0.0) ? (sumPV / sumV) : 0.0;
// }

// // Calculate EMA (Exponential Moving Average)
// double TechnicalCalculator::calculateEMA(
//     const std::vector<double>& prices, 
//     int period) const 
// {
//     if (prices.size() < (size_t)period) {
//         return 0.0;
//     }
    
//     double multiplier = 2.0 / (period + 1.0);
    
//     // Start with an SMA of the first 'period' elements
//     double sum = 0.0;
//     for (int i = 0; i < period; ++i) {
//         sum += prices[i];
//     }
//     double ema = sum / period;
    
//     // Then apply EMA for the remaining data
//     for (size_t i = period; i < prices.size(); ++i) {
//         ema = (prices[i] - ema) * multiplier + ema;
//     }
    
//     return ema;
// }

// //------------------------------------------------------------------------------
// // Calculate Chaikin Oscillator
// //------------------------------------------------------------------------------
// // * The Chaikin Oscillator is typically the difference between two EMAs
// //   (fast and slow) of the Accumulation/Distribution (A/D) line.
// // * The A/D line (per bar) = ( (Close - Low) - (High - Close) ) / (High - Low ) * Volume
// //   (Sometimes known as the Chaikin Money Flow multiplier * Volume)
// // * Then we compute two EMAs of that A/D line: e.g., 3-period EMA and 10-period EMA
// //   The difference = Chaikin Oscillator.

// // * The Chaikin Oscillator is the difference between two EMAs (fast and slow)
// //   of the Accumulation/Distribution (A/D) line.
// // * Fast EMA Period: 3 (3 one-minute bars in a 1-minute chart)
// // * Slow EMA Period: 10 (10 one-minute bars in a 1-minute chart)
// // * The A/D line (per bar) = ((Close - Low) - (High - Close)) / (High - Low) * Volume
// //   (Sometimes known as the Money Flow Volume)
// // * Output is often displayed in thousands (K) for readability
// double TechnicalCalculator::calculateChaikinOscillator(
//     const std::vector<double>& highPrices,
//     const std::vector<double>& lowPrices,
//     const std::vector<double>& closePrices,
//     const std::vector<double>& volumes,
//     int fastPeriod,
//     int slowPeriod
// ) const
// {
//     // Using default values if not specified
//     if (fastPeriod <= 0) fastPeriod = config::CHAIKIN_FAST_PERIOD; // Default: 3 periods
//     if (slowPeriod <= 0) slowPeriod = config::CHAIKIN_SLOW_PERIOD; // Default: 10 periods
    
//     // Safety checks
//     size_t n = highPrices.size();
//     if (n < (size_t)std::max(fastPeriod, slowPeriod) || 
//         n != lowPrices.size() || n != closePrices.size() || n != volumes.size()) {
//         return 0.0; // Not enough data or mismatch
//     }

//     // 1) Compute the A/D line for each bar
//     std::vector<double> adLine(n, 0.0);
//     for (size_t i = 0; i < n; ++i) {
//         double range = (highPrices[i] - lowPrices[i]);
//         if (range == 0.0) {
//             // Avoid division by zero; treat as 0 or skip
//             adLine[i] = 0.0;
//         } else {
//             double moneyFlowMultiplier = ((closePrices[i] - lowPrices[i]) 
//                                           - (highPrices[i] - closePrices[i])) / range;
//             adLine[i] = moneyFlowMultiplier * volumes[i];
//         }
//     }

//     // 2) Compute fast & slow EMAs of A/D line
//     double fastEMA = calculateEMA(adLine, fastPeriod);
//     double slowEMA = calculateEMA(adLine, slowPeriod);

//     // 3) The Chaikin Oscillator = fastEMA - slowEMA
//     // Note: Returns raw value - display can format in thousands (K) as needed
//     return (fastEMA - slowEMA);
// }

// //------------------------------------------------------------------------------
// // Calculate ALMA (Arnaud Legoux Moving Average)
// //------------------------------------------------------------------------------
// // * ALMA aims to reduce lag while giving a smooth average.
// // * Parameters:
// //   - windowSize: number of bars to consider (e.g., 9)
// //   - sigma: controls responsiveness (range 0.1-1.0)
// //   - offset: controls phase shift (range 0-10)
// //
// // * The formula creates Gaussian-distributed weights centered with offset
// //   applied to the most recent prices.
// double TechnicalCalculator::calculateALMA(
//     const std::vector<double>& prices,
//     int windowSize,
//     double sigma,
//     double offset
// ) const
// {
//     // Need at least windowSize data points
//     if ((int)prices.size() < windowSize || windowSize <= 0) {
//         return 0.0;
//     }

//     // Constrain parameters to valid ranges
//     sigma = std::max(0.1, std::min(sigma, 1.0));
//     offset = std::max(0.0, std::min(offset, 10.0));

//     // Calculate distribution center point
//     double m = offset;
    
//     // Calculate standard deviation factor
//     double s = windowSize / (sigma * 10.0);

//     // Build weights - optimized to avoid unnecessary memory allocations
//     double sumW = 0.0;
//     double weightedSum = 0.0;
//     int startIdx = static_cast<int>(prices.size()) - windowSize;

//     // Calculate weighted sum in a single pass
//     for (int i = 0; i < windowSize; ++i) {
//         double x = (double)i - m;
//         double weight = std::exp(-(x * x) / (2.0 * s * s));
//         sumW += weight;
//         weightedSum += prices[startIdx + i] * weight;
//     }

//     // Normalize and return
//     return (sumW > 0.0) ? (weightedSum / sumW) : 0.0;
// }

// //------------------------------------------------------------------------------
// // (New) Calculate RSI (Relative Strength Index)
// //------------------------------------------------------------------------------
// double TechnicalCalculator::calculateRSI(const std::vector<double>& closePrices, int period) const {
//     if (closePrices.size() <= period + 1) {
//         return 50.0; // Not enough data, return neutral value
//     }
    
//     double sumGain = 0.0;
//     double sumLoss = 0.0;
    
//     // Calculate initial average gain and loss
//     for (size_t i = 1; i <= period; ++i) {
//         double change = closePrices[i] - closePrices[i-1];
//         if (change > 0) {
//             sumGain += change;
//         } else {
//             sumLoss += std::abs(change);
//         }
//     }
    
//     double avgGain = sumGain / period;
//     double avgLoss = sumLoss / period;
    
//     // Smoothed RSI calculation
//     for (size_t i = period + 1; i < closePrices.size(); ++i) {
//         double change = closePrices[i] - closePrices[i-1];
        
//         if (change > 0) {
//             avgGain = (avgGain * (period - 1) + change) / period;
//             avgLoss = (avgLoss * (period - 1)) / period;
//         } else {
//             avgGain = (avgGain * (period - 1)) / period;
//             avgLoss = (avgLoss * (period - 1) + std::abs(change)) / period;
//         }
//     }
    
//     if (avgLoss < 0.000001) { // Avoid division by zero
//         return 100.0;
//     }
    
//     double rs = avgGain / avgLoss;
//     double rsi = 100.0 - (100.0 / (1.0 + rs));
    
//     return rsi;
// }

// //------------------------------------------------------------------------------
// // (New) Calculate SMA (Simple Moving Average)
// //------------------------------------------------------------------------------
// double TechnicalCalculator::calculateSMA(const std::vector<double>& data, int period) const {
//     if (data.size() < (size_t)period) {
//         return 0.0; // Not enough data
//     }
    
//     double sum = 0.0;
//     for (size_t i = data.size() - period; i < data.size(); ++i) {
//         sum += data[i];
//     }
    
//     return sum / period;
// }

// //------------------------------------------------------------------------------
// // (New) Check RSI with SMA Overlay
// //------------------------------------------------------------------------------
// bool TechnicalCalculator::checkRSIwithSMAOverlay(
//     const std::vector<double>& closePrices,
//     double currentPrice,
//     int rsiPeriod,
//     int smaPeriod) const {
    
//     // Calculate RSI values for the entire period
//     std::vector<double> rsiValues;
    
//     // Need at least rsiPeriod+1 data points to calculate first RSI value
//     if (closePrices.size() <= (size_t)(rsiPeriod + 1)) {
//         return false; // Not enough data
//     }
    
//     // Calculate RSI for each point after the initial period
//     for (size_t i = rsiPeriod; i < closePrices.size(); ++i) {
//         std::vector<double> subRange(closePrices.begin(), closePrices.begin() + i + 1);
//         double rsi = calculateRSI(subRange, rsiPeriod);
//         rsiValues.push_back(rsi);
//     }
    
//     // Need enough RSI values to calculate SMA
//     if (rsiValues.size() < (size_t)smaPeriod) {
//         return false; // Not enough RSI data points
//     }
    
//     // Calculate SMA of RSI
//     double smaOfRSI = calculateSMA(rsiValues, smaPeriod);
    
//     // Check if RSI is trending upward (above SMA)
//     double lastRSI = rsiValues.back();
    
//     // Check if RSI is at least 125% of current price (indicating upward pressure)
//     double rsiToPriceRatio = lastRSI / currentPrice;
    
//     // The condition we're looking for:
//     // 1. RSI > SMA (trending upward)
//     // 2. RSI is at least 125% of current price
//     return (lastRSI > smaOfRSI && rsiToPriceRatio >= 1.25);
// }

// //------------------------------------------------------------------------------
// // Calculate Multiple EMAs for different periods
// //------------------------------------------------------------------------------
// std::map<int, double> TechnicalCalculator::calculateMultipleEMAs(
//     const std::vector<double>& prices,
//     const std::vector<int>& periods) const
// {
//     std::map<int, double> results;
    
//     // Find the maximum period to ensure we have enough data
//     int maxPeriod = 0;
//     for (const auto& period : periods) {
//         maxPeriod = std::max(maxPeriod, period);
//     }
    
//     // Check if we have enough data for the largest period
//     if (prices.size() < (size_t)maxPeriod) {
//         // Return empty map or with zeros if not enough data
//         for (const auto& period : periods) {
//             results[period] = 0.0;
//         }
//         return results;
//     }
    
//     // Calculate EMA for each requested period
//     for (const auto& period : periods) {
//         results[period] = calculateEMA(prices, period);
//     }
    
//     return results;
// }

// //------------------------------------------------------------------------------
// // Check if faster EMA is above slower EMA (bullish cross)
// //------------------------------------------------------------------------------
// // Note: This is a general-purpose utility method available to any code with access
// // to a TechnicalCalculator. It provides the same functionality as ScalpingFilter::checkEmaCross
// // but with more flexibility (custom periods can be specified). This version can be used
// // for monitoring, backtesting, or other parts of the trading system independently
// // from the filtering process.
// bool TechnicalCalculator::checkEmaCross(
//     const std::vector<double>& prices,
//     int fastPeriod,
//     int slowPeriod) const
// {
//     // Need enough data points for the longer period
//     if (prices.size() < (size_t)std::max(fastPeriod, slowPeriod)) {
//         return false;
//     }
    
//     // Calculate both EMAs
//     double fastEma = calculateEMA(prices, fastPeriod);
//     double slowEma = calculateEMA(prices, slowPeriod);
    
//     // Return true if fast EMA is above slow EMA (bullish)
//     return (fastEma > slowEma);
// }

// //------------------------------------------------------------------------------
// // Format Chaikin Oscillator for display (in thousands - K)
// //------------------------------------------------------------------------------
// std::string TechnicalCalculator::formatChaikinForDisplay(double chaikinValue) const {
//     // Convert to thousands and format with one decimal place
//     double valueInK = chaikinValue / 1000.0;
    
//     // Format with sign and K suffix
//     char buffer[32];
//     std::snprintf(buffer, sizeof(buffer), "%.1fK", valueInK);
    
//     // Add + sign for positive values (negative values already have -)
//     if (valueInK > 0) {
//         return "+" + std::string(buffer);
//     } else {
//         return std::string(buffer);
//     }
// }

// //------------------------------------------------------------------------------
// // Check if Chaikin Oscillator indicates positive momentum
// //------------------------------------------------------------------------------
// bool TechnicalCalculator::isChaikinPositive(
//     const std::vector<double>& highPrices,
//     const std::vector<double>& lowPrices,
//     const std::vector<double>& closePrices,
//     const std::vector<double>& volumes,
//     int fastPeriod,
//     int slowPeriod) const 
// {
//     // Calculate the Chaikin Oscillator value
//     double chaikinValue = calculateChaikinOscillator(
//         highPrices, lowPrices, closePrices, volumes, 
//         fastPeriod, slowPeriod);
    
//     // Positive value indicates buying pressure/momentum
//     return (chaikinValue > 0.0);
// }
// } // namespace technical_calculator
// //-----------------------------------------------------------------------------------
// // Notes on Using Chaikin Oscillator & ALMA in Real-Time Trading
// //-----------------------------------------------------------------------------------
// /*
//    1) Chaikin Oscillator:
//       - This helps detect if *money flow* is supporting a price move.
//       - If you see a sudden iceberg order and big volume push, 
//         the A/D line (and thus the Chaikin Osc) should spike positive 
//         if it's real buying interest. 
//       - If you're in a position that's "flatlining" but Chaikin remains above 0,
//         it's often safe to keep holding (the money flow hasn't reversed).
//       - A sudden turn negative might indicate distribution 
//         => consider tightening stops or exiting.

//    2) ALMA:
//       - Use as a *smoother moving average* to track short-term trend.
//       - If you're forced to hold longer than 5 minutes, 
//         you can watch if the price stays above ALMA(9, 0.85, 6) 
//         (for example) to confirm ongoing bullishness.
//       - Once price closes below ALMA repeatedly, 
//         it could be a warning sign to exit.

//    The big picture: EntrySignal
//    - If you see an iceberg pushing price up *and* the Chaikin Osc is surging, 
//      it's a sign "big money" is stepping in, so you may want to hold longer 
//      or even add if your scalping risk parameters allow.
//    - If an iceberg is suspected (huge hidden liquidity on the bid/ask) but 
//      Chaikin doesn't respond (stays low or negative), 
//      it might be a "fake" or short-lived bounce.

//    Example usage in code:
//      ...
//      // 1) If your filters say "enter," you do so.
//      // 2) If you hold for >5 mins, you call:
//      double chaikinVal = myTechCalc.calculateChaikinOscillator(highs, lows, closes, volumes, 3, 10);
//      double almaVal    = myTechCalc.calculateALMA(closes, 9, 0.85, 6);

//      // Then interpret:
//      if (chaikinVal > 0.0 && closes.back() > almaVal) {
//         // bullish money flow + price above ALMA => safe to hold
//      } else {
//         // losing money flow or price below ALMA => consider exit
//      }
//      ...
// */
// //   - windowSize is the number of bars to consider.
