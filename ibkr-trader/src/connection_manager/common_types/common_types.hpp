// common_types.hpp

#ifndef COMMON_TYPES_HPP
#define COMMON_TYPES_HPP

#include <map>
#include <string>
#include <Contract.h>
#include <EWrapper.h>
#include <EReaderOSSignal.h>
#include <EReader.h>

// Using TWS API types
using Contract = Contract;
using EWrapper = EWrapper;
using EReaderSignal = EReaderSignal;
using EReader = EReader;
using EReaderOSSignal = EReaderOSSignal;
using TickerId = TickerId;
using TickType = TickType;
using TickAttrib = TickAttrib;
using TickAttribBidAsk = TickAttribBidAsk;
using TickAttribLast = TickAttribLast;
using OrderId = OrderId;
using Bar = Bar;
using Decimal = Decimal;
using Execution = Execution;

// Technical indicators data structure
struct TechnicalIndicators {
    // EMA values for price
    std::map<int, double> emaPrice;  // Key: period, Value: EMA value
    
    // Bollinger Bands
    struct {
        double upper = 0.0;
        double middle = 0.0;
        double lower = 0.0;
        int period = 20;     // default period
        double multiplier = 2.0;  // default standard deviations
    } bollingerBands;
    
    // Stochastic Oscillator
    struct {
        double k = 0.0;
        double d = 0.0;
        int kPeriod = 14;  // default period
        int dPeriod = 3;   // default smoothing
    } stochastic;
    
    // Velocity Vector
    struct {
        double magnitude = 0.0;
        int direction = 0;  // 1: up, -1: down, 0: flat
    } velocity;
    
    // ALMA
    double alma = 0.0;
    
    // Chaikin Oscillator for different EMAs
    std::map<int, double> chaikinOsc;
    
    // Elder-Ray Index
    struct {
        double bullPower = 0.0;
        double bearPower = 0.0;
    } elderRay;
    
    // Liquidity metrics
    struct {
        double bidAskSpread = 0.0;
        double bidAskRatio = 0.0;
        double volumeImbalance = 0.0;  // net buy/sell volume
        double valueImbalance = 0.0;   // net buy/sell value ($)
    } liquidity;
    
    // Volume EMAs
    std::map<int, double> emaVolume;
    
    // Dollar Volume EMAs
    std::map<int, double> emaDollarVolume;
    
    // Order flow metrics
    struct {
        std::map<int, double> emaQuotes;
        std::map<int, double> emaTrades;
    } orderFlow;
    
    // Timestamp of last calculation
    long long lastUpdateTime = 0;
}; // end of struct TechnicalIndicators

#endif // COMMON_TYPES_HPP



