// ───────────────────────────────────────────────────────────────
//  connection.cpp – implementation for the thin IBKRTrader layer
// ───────────────────────────────────────────────────────────────
#include "connection.hpp"
#include "../../models/stock_data_tick/stock_data_tick.hpp"   // full struct
#include "../../models/model_manager.hpp"                   // only for pointer use
#include <chrono>
#include <iomanip>
#include <iostream>
#include <cstdio>          // std::sscanf
#include <ctime>
#include <sstream>


using namespace ibkr_frame_analyzer;
using namespace ibkr_decoder;
using namespace std::chrono_literals;
namespace connection {

// ───────────────────────────────────────────────────────────────
//  ctor / dtor
// ───────────────────────────────────────────────────────────────
IBKRTrader::IBKRTrader()
    : m_client(std::make_unique<EClientSocket>(this, &m_osSignal))
{
    // build analyzer/decoder pair
    m_an  = std::make_unique<FrameAnalyzer>();           // default ctor
    m_dec = std::make_unique<IBKRDecoder>(*m_an);  
    m_cache = std::make_unique<ConnectionCache>(*m_dec);
    logCb("CTOR", "-", "IBKRTrader constructed");
}

IBKRTrader::~IBKRTrader()
{
    disconnect();   // idempotent
    logCb("DTOR", "-", "IBKRTrader destroyed");
}

// ───────────────────────────────────────────────────────────────
//  connect / disconnect
// ───────────────────────────────────────────────────────────────
bool IBKRTrader::connect(int clientId,
                         const std::string& symbol,
                         const Contract&    c)
{
    if (isConnected()) return true;

    logCb("connect", symbol, "eConnect → " + std::to_string(clientId));
    m_client->setConnectOptions("+PACEAPI");

    if (!m_client->eConnect(HOST, PORT, clientId, /*extraAuth*/false)) {
        logCb("connect", symbol, "eConnect failed");
        return false;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));        // let settle
    m_sym      = symbol;
    m_contract = c;

    logCb("connect", symbol, "connected & stabilised");
    return true;
}

void IBKRTrader::disconnect()
{
    if (m_client && m_client->isConnected()) {
        m_client->eDisconnect();
        logCb("disconnect", m_sym, "disconnected");
    }
}

// ───────────────────────────────────────────────────────────────
//  helper & plumbing
// ───────────────────────────────────────────────────────────────
std::unique_ptr<EReader> IBKRTrader::createReader()
{
    auto rdr = std::make_unique<EReader>(m_client.get(), &m_osSignal);
    rdr->start();                     // non-blocking
    return rdr;
}

void IBKRTrader::setModelManager(model_manager::ModelManager* mgr,
                                 const std::string& sym)
{
    m_mgr = mgr;
    m_sym = sym;
}

void IBKRTrader::startDataStream(const std::string& symbol)
{
    if (!isConnected()) return;
    //////////////////////////////// {performed inside ModelManager} - line 100
    //cli->reqMktData(m_reqId, c, "233,232,221", false, false, {});
    ////////////////////////////////
    logCb("startDataStream", symbol, "initialising supplementary feeds");

    // tick-by-tick last & bid/ask streams (supplementary to main market data)
    static int TBT = 7'000;
    m_client->reqTickByTickData(TBT++, m_contract, "AllLast", 0, false);
    m_client->reqTickByTickData(TBT++, m_contract, "BidAsk",  0, false);

    // 5-second realtime bars (supplementary to main market data)
    static int BAR = 6'000;
    m_client->reqRealTimeBars(BAR++, m_contract, 5, "TRADES", false, {});




    logCb("startDataStream", symbol, "feeds running");
}

void IBKRTrader::routeViaCache(double last,double bid,double ask,int bidSz,int askSz,
                               double vol,uint64_t ts, const std::string& exchange,
                               double open, double high, double low, double close, double vwap)
{
    auto res = m_cache->merge(m_sym, ts, last, vol, bid, ask, bidSz, askSz, exchange, open, high, low, close, vwap);
    if (res.isComplete && res.tickChanged && m_mgr){
        // route back to model manager here
        m_mgr->addTick(res.data);
    }
}

// ─────────────────── EWrapper overrides ───────────────────────
void IBKRTrader::tickString(TickerId id, TickType field,
                            const std::string& val)
{
    if (field != 45 && field != 48 && field != 49 && field != 88) return;

    // Get thread ID for logging
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();

    auto res = m_an->analyzeTickStringData(id, field, val);
    
    if (res.hasDecodedData) {
        std::cout << "\n========== TICK STRING ANALYSIS ==========" << std::endl;
        std::cout << "Symbol: " << m_sym << " | Request ID: " << id << std::endl;
        std::cout << "Thread ID: " << threadIdStr.str() << std::endl;
        std::cout << "Field: " << field << " | Type: " << res.dataType << std::endl;
        std::cout << "Raw Value: '" << val << "'" << std::endl;
        
        if (res.volume > 0.0 || res.vwap > 0.0) {
            std::cout << "Volume (Total Market): " << std::fixed << std::setprecision(2) << res.volume << "M" << std::endl;
            std::cout << "VWAP: $" << std::fixed << std::setprecision(5) << res.vwap << std::endl;
        }
        
        if (res.timestamp > 0) {
            std::cout << "Timestamp: " << res.timestamp << std::endl;
        }
        
        std::cout << "Decoded: " << res.decodedValue << std::endl;
        std::cout << "===========================================" << std::endl;
    } else {
        std::cout << "[TickString][" << m_sym << "] Raw field=" << field << " value='" << val << "'" << std::endl;
    }

    // minimalist routing (only RTVolume gives us price/size)
    if (field == 49) {
        double price = 0.0, size = 0.0;
        std::sscanf(val.c_str(), "%lf;%lf", &price, &size);
        routeViaCache(price, 0, 0, 0, 0,
                     size,
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count(),
                     "", 0, 0, 0, 0, res.vwap);
    }
}

void IBKRTrader::realtimeBar(TickerId reqId, long t,
                             double o, double h, double l,
                             double c, Decimal vol,
                             Decimal wap, int cnt)
{


    auto bar = m_an->analyzeRealtimeBarData(reqId, static_cast<int>(t),
                                            o, h, l, c,
                                            DecimalFunctions::decimalToDouble(vol),
                                            DecimalFunctions::decimalToDouble(wap),
                                            cnt);
    // logCb("realtimeBar", m_sym,
    //       "Δ=" + std::to_string(bar.priceChange) +
    //       " range=" + std::to_string(bar.barRange));
    // Step 1: Get current time for time difference calculation
    time_t currentTime = std::time(nullptr);
    
    // Step 2: Analyze the bar data using frame analyzer
    auto analyzedData = m_an->analyzeRealtimeBarData(reqId, static_cast<int>(t),
                                                     o, h, l, c,
                                                     DecimalFunctions::decimalToDouble(vol),
                                                     DecimalFunctions::decimalToDouble(wap),
                                                     cnt);
    
    // Step 3: Print comprehensive bar analysis (legacy format)
    std::cout << "\n========== REALTIME BAR ANALYSIS ==========" << std::endl;
    std::cout << "Symbol: " << m_sym << " | Request ID: " << analyzedData.reqId << std::endl;
    std::cout << "Bar Time: " << analyzedData.formattedTime << " (epoch: " << analyzedData.epochTime << ")" << std::endl;
    std::cout << "Time Difference: " << (currentTime - analyzedData.epochTime) << " seconds" << std::endl;
    std::cout << "Open: $" << std::fixed << std::setprecision(4) << analyzedData.open << std::endl;
    std::cout << "High: $" << std::fixed << std::setprecision(4) << analyzedData.high << std::endl;
    std::cout << "Low: $" << std::fixed << std::setprecision(4) << analyzedData.low << std::endl;
    std::cout << "Close: $" << std::fixed << std::setprecision(4) << analyzedData.close << std::endl;
    std::cout << "Volume: " << std::fixed << std::setprecision(0) << analyzedData.volume << " shares" << std::endl;
    std::cout << "WAP: $" << std::fixed << std::setprecision(6) << analyzedData.wap << std::endl;
    std::cout << "range: " << std::fixed << std::setprecision(4) << analyzedData.barRange << std::endl;
    std::cout << "Price Change: $" << std::fixed << std::setprecision(4) << analyzedData.priceChange; 
    std::cout << "Trade Count: " << analyzedData.count << std::endl;
    
    // Print calculated metrics if valid
    if (analyzedData.hasValidPriceChange) {
        std::cout << "Price Change: $" << std::fixed << std::setprecision(4) << analyzedData.priceChange 
                  << " (" << std::showpos << std::fixed << std::setprecision(2) << analyzedData.percentChange << "%)" << std::noshowpos << std::endl;
    }
    
    if (analyzedData.hasValidRange) {
        std::cout << "Bar Range: $" << std::fixed << std::setprecision(4) << analyzedData.barRange << std::endl;
    }
    
    std::cout << "===========================================" << std::endl;

    // Step 4: Route to model manager
    routeViaCache(c, 0, 0, 0, 0,
                 DecimalFunctions::decimalToDouble(vol),
                 static_cast<std::uint64_t>(t) * 1'000,
                 "", analyzedData.open, analyzedData.high, analyzedData.low, analyzedData.close, 0);
}

void IBKRTrader::tickByTickAllLast(int reqId, int type, time_t t,
                                   double price, Decimal size,
                                   const TickAttribLast& attr,
                                   const std::string& ex,
                                   const std::string& cond)
{
    // Get thread ID for logging
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();

    auto ana = m_an->analyzeTickByTickData(
        reqId, type, t, price,
        DecimalFunctions::decimalToDouble(size),
        ex, cond, attr.pastLimit, attr.unreported);

    // Print comprehensive trade analysis
    std::cout << "\n========== TICK-BY-TICK TRADE ANALYSIS ==========" << std::endl;
    std::cout << "Symbol: " << m_sym << " | Request ID: " << ana.reqId << std::endl;
    std::cout << "Thread ID: " << threadIdStr.str() << std::endl;
    std::cout << "Trade Time: " << ana.formattedTime << " (epoch: " << ana.epochTime << ")" << std::endl;
    std::cout << "Tick Type: " << ana.tickType << " (" << (ana.tickType == 1 ? "Last" : "AllLast") << ")" << std::endl;
    std::cout << "Price: $" << std::fixed << std::setprecision(4) << ana.price << std::endl;
    std::cout << "Volume: " << std::fixed << std::setprecision(0) << ana.volume << " shares" << std::endl;
    std::cout << "Exchange: " << (ana.exchange.empty() ? "N/A" : ana.exchange) << std::endl;
    
    // Print special conditions if present
    if (!ana.specialConditions.empty()) {
        std::cout << "Special Conditions: " << ana.specialConditions << std::endl;
    }
    
    // Print flags if set
    if (ana.pastLimit) {
        std::cout << "⚠️  Trade Past Limit" << std::endl;
    }
    if (ana.unreported) {
        std::cout << "⚠️  Unreported Trade" << std::endl;
    }
    
    // Print calculated metrics if valid
    if (ana.hasValidTrade) {
        std::cout << "Dollars Traded: $" << std::fixed << std::setprecision(2) << ana.dollarsTraded << std::endl;
    }
    
    std::cout << "===============================================" << std::endl;

    routeViaCache(price, 0, 0, 0, 0,
                 DecimalFunctions::decimalToDouble(size),
                 static_cast<std::uint64_t>(t) * 1'000,
                 ex, 0, 0, 0, 0, 0);
}

void IBKRTrader::tickByTickBidAsk(int reqId, time_t t,
                                  double bp, double ap,
                                  Decimal bs, Decimal as,
                                  const TickAttribBidAsk& attr)
{
    // Get thread ID for logging
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();

    auto ana = m_an->analyzeTickByTickBidAskData(
        reqId, t, bp, ap,
        DecimalFunctions::decimalToDouble(bs),
        DecimalFunctions::decimalToDouble(as),
        attr.bidPastLow, attr.askPastHigh);

    // Print comprehensive bid-ask analysis
    std::cout << "\n========== TICK-BY-TICK BID-ASK ANALYSIS ==========" << std::endl;
    std::cout << "Symbol: " << m_sym << " | Request ID: " << ana.reqId << std::endl;
    std::cout << "Thread ID: " << threadIdStr.str() << std::endl;
    std::cout << "Quote Time: " << ana.formattedTime << " (epoch: " << ana.epochTime << ")" << std::endl;
    std::cout << "Bid: $" << std::fixed << std::setprecision(4) << ana.bidPrice 
              << " x " << std::fixed << std::setprecision(0) << ana.bidSize << std::endl;
    std::cout << "Ask: $" << std::fixed << std::setprecision(4) << ana.askPrice 
              << " x " << std::fixed << std::setprecision(0) << ana.askSize << std::endl;
    
    // Print flags if set
    if (ana.bidPastLow) {
        std::cout << "⚠️  Bid Past Low" << std::endl;
    }
    if (ana.askPastHigh) {
        std::cout << "⚠️  Ask Past High" << std::endl;
    }
    
    // Print calculated metrics if valid
    if (ana.hasValidSpread) {
        std::cout << "Spread: $" << std::fixed << std::setprecision(4) << ana.spread 
                  << " (" << std::fixed << std::setprecision(2) << ana.spreadPercent << "%)" << std::endl;
    }
    
    if (ana.hasValidMidPoint) {
        std::cout << "Mid Point: $" << std::fixed << std::setprecision(4) << ana.midPoint << std::endl;
    }
    
    std::cout << "===============================================" << std::endl;

    routeViaCache(0, ana.bidPrice, ana.askPrice,
                 static_cast<int>(ana.bidSize),
                 static_cast<int>(ana.askSize),
                 0,
                 static_cast<std::uint64_t>(t) * 1'000,
                 "", 0, 0, 0, 0, 0);
}

void IBKRTrader::error(int id, long /*time*/, int code,
                       const std::string& msg,
                       const std::string&)
{
    if (code == 2104 || code == 2106) return;   // suppress chatter
    
    // Get thread ID for logging
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    
    std::cout << "\n========== ERROR ANALYSIS ==========" << std::endl;
    std::cout << "Symbol: " << m_sym << " | Request ID: " << id << std::endl;
    std::cout << "Thread ID: " << threadIdStr.str() << std::endl;
    std::cout << "Error Code: " << code << std::endl;
    std::cout << "Error Message: " << msg << std::endl;
    
    // Provide detailed error context based on error codes
    if (code == 162) {
        std::cout << "Context: Historical data request error" << std::endl;
    } else if (code == 200) {
        std::cout << "Context: No security definition found - check contract details and symbol" << std::endl;
    } else if (code == 10092) {
        std::cout << "Context: Deep market data not supported - using top-of-book data" << std::endl;
        if (id >= 8000 && id < 9000) {
            std::cout << "Fallback: Switching from market depth to regular market data" << std::endl;
        }
    } else if (code == 10189) {
        std::cout << "Context: Tick-by-tick data request failed - verify market data subscription" << std::endl;
        if (id >= 7000 && id < 8000) {
            std::cout << "Fallback: Switching from tick-by-tick to regular market data" << std::endl;
        }
    } else if (code == 321) {
        std::cout << "Context: Historical data format error - check duration format" << std::endl;
        if (id >= 4000 && id < 5000) {
            std::cout << "Fallback: Continuing with available real-time data" << std::endl;
        }
    } else {
        std::cout << "Context: General error - see message for details" << std::endl;
    }
    
    std::cout << "=======================================" << std::endl;
}

} // namespace connection
