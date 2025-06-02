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

// convert + forward one logical tick to ModelManager
void IBKRTrader::routeToModel(double last, double bid, double ask,
                              int bidSz, int askSz,
                              double vol, std::uint64_t ts_ms)
{
    if (!m_mgr) return;

    stock_data_tick::StockData sd;
    sd.symbol    = m_sym;
    sd.timestamp = ts_ms;
    sd.last      = last;
    sd.bid       = bid;
    sd.ask       = ask;
    sd.bidSize   = bidSz;
    sd.askSize   = askSz;
    sd.volume    = static_cast<int>(vol);

    // TODO(CONNECTION_CACHE): merge/route via cache here

    m_mgr->addTick(sd);
}

// ─────────────────── EWrapper overrides ───────────────────────
void IBKRTrader::tickString(TickerId id, TickType field,
                            const std::string& val)
{
    if (field != 45 && field != 48 && field != 49 && field != 88) return;

    logCb("tickString", m_sym,
          "raw id=" + std::to_string(id) +
          " fld=" + std::to_string(field) +
          " '" + val + '\'');

    auto res = m_an->analyzeTickStringData(id, field, val);
    if (res.hasDecodedData) {
        logCb("tickString", m_sym,
              "decoded [" + res.dataType + "] " + res.decodedValue);
    }

    // minimalist routing (only RTVolume gives us price/size)
    if (field == 49) {
        double price = 0.0, size = 0.0;
        std::sscanf(val.c_str(), "%lf;%lf", &price, &size);
        routeToModel(price, 0, 0, 0, 0, size,
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count());
    }
}

void IBKRTrader::realtimeBar(TickerId reqId, long t,
                             double o, double h, double l,
                             double c, Decimal vol,
                             Decimal wap, int cnt)
{
    logCb("realtimeBar", m_sym,
          "raw req=" + std::to_string(reqId) +
          " close=" + std::to_string(c) +
          " vol=" + std::to_string(DecimalFunctions::decimalToDouble(vol)));

    auto bar = m_an->analyzeRealtimeBarData(reqId, static_cast<int>(t),
                                            o, h, l, c,
                                            DecimalFunctions::decimalToDouble(vol),
                                            DecimalFunctions::decimalToDouble(wap),
                                            cnt);
    logCb("realtimeBar", m_sym,
          "Δ=" + std::to_string(bar.priceChange) +
          " range=" + std::to_string(bar.barRange));

    routeToModel(c, 0, 0, 0, 0,
                 DecimalFunctions::decimalToDouble(vol),
                 static_cast<std::uint64_t>(t) * 1'000);
}

void IBKRTrader::tickByTickAllLast(int reqId, int type, time_t t,
                                   double price, Decimal size,
                                   const TickAttribLast& attr,
                                   const std::string& ex,
                                   const std::string& cond)
{
    logCb("tickByTickAllLast", m_sym,
          "raw req=" + std::to_string(reqId) +
          " type=" + std::to_string(type) +
          " p=" + std::to_string(price) +
          " sz=" + std::to_string(DecimalFunctions::decimalToDouble(size)));

    auto ana = m_an->analyzeTickByTickData(
        reqId, type, t, price,
        DecimalFunctions::decimalToDouble(size),
        ex, cond, attr.pastLimit, attr.unreported);

    logCb("tickByTickAllLast", m_sym,
          "trade $" + std::to_string(ana.dollarsTraded));

    routeToModel(price, 0, 0, 0, 0,
                 DecimalFunctions::decimalToDouble(size),
                 static_cast<std::uint64_t>(t) * 1'000);
}

void IBKRTrader::tickByTickBidAsk(int reqId, time_t t,
                                  double bp, double ap,
                                  Decimal bs, Decimal as,
                                  const TickAttribBidAsk& attr)
{
    logCb("tickByTickBidAsk", m_sym,
          "raw req=" + std::to_string(reqId) +
          " bid=" + std::to_string(bp) +
          "@"   + std::to_string(DecimalFunctions::decimalToDouble(bs)) +
          " ask=" + std::to_string(ap) +
          "@"   + std::to_string(DecimalFunctions::decimalToDouble(as)));

    auto ana = m_an->analyzeTickByTickBidAskData(
        reqId, t, bp, ap,
        DecimalFunctions::decimalToDouble(bs),
        DecimalFunctions::decimalToDouble(as),
        attr.bidPastLow, attr.askPastHigh);

    logCb("tickByTickBidAsk", m_sym,
          "spread=" + std::to_string(ana.spread) +
          " (" + std::to_string(ana.spreadPercent) + "%)");

    routeToModel(0, bp, ap,
                 static_cast<int>(DecimalFunctions::decimalToDouble(bs)),
                 static_cast<int>(DecimalFunctions::decimalToDouble(as)),
                 0,
                 static_cast<std::uint64_t>(t) * 1'000);
}

void IBKRTrader::error(int id, long /*time*/, int code,
                       const std::string& msg,
                       const std::string&)
{
    if (code == 2104 || code == 2106) return;   // suppress chatter
    
    logCb("ERROR", m_sym,
          "id=" + std::to_string(id) +
          " code=" + std::to_string(code) +
          " '" + msg + '\'');
}

} // namespace connection
