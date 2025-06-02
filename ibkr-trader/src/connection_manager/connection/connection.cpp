// ───────────────────────────────────────────────────────────────
//  connection.cpp – implementation for the thin IBKRTrader layer
// ───────────────────────────────────────────────────────────────
#include "connection.hpp"
#include "../../models/stock_data_tick/stock_data_tick.hpp"   // full struct
#include "../../models/model_manager.hpp"                   // only for pointer use
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;
namespace connection {

// ───────────────────────────────────────────────────────────────
//  ctor / dtor
// ───────────────────────────────────────────────────────────────
IBKRTrader::IBKRTrader()
    : m_client(std::make_unique<EClientSocket>(this, &m_osSignal))
{
    std::ostringstream ss; ss << std::this_thread::get_id();
    std::cout << "[IBKRTrader:" << ss.str() << "] constructed\n";
}

IBKRTrader::~IBKRTrader()
{
    disconnect();   // idempotent
    std::ostringstream ss; ss << std::this_thread::get_id();
    std::cout << "[IBKRTrader:" << ss.str() << "] destroyed\n";
}

// ───────────────────────────────────────────────────────────────
//  connect / disconnect
// ───────────────────────────────────────────────────────────────
bool IBKRTrader::connect(int clientId,
                         const std::string& symbol,
                         const Contract&    c)
{
    if (isConnected()) return true;

    std::cout << "[IBKRTrader] Attempting connection to " << HOST << ":" << PORT << " with clientId=" << clientId << "\n";
    
    m_client->setConnectOptions("+PACEAPI");
    bool connectResult = m_client->eConnect(HOST, PORT, clientId, /*extraAuth*/false);
    
    std::cout << "[IBKRTrader] eConnect returned: " << (connectResult ? "true" : "false") << "\n";
    std::cout << "[IBKRTrader] isConnected() immediately after eConnect: " << (isConnected() ? "true" : "false") << "\n";
    
    // Wait for connection to stabilize (like legacy code)
    std::cout << "[IBKRTrader] Waiting for connection to stabilize...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (!connectResult) {
        std::cerr << "[IBKRTrader] eConnect failed\n";
        return false;
    }

    m_sym      = symbol;
    m_contract = c;

    std::cout << "[IBKRTrader] connected → " << HOST << ":" << PORT
              << " (cid=" << clientId << "), connection stabilized\n";
    return true;
}

void IBKRTrader::disconnect()
{
    if (m_client && m_client->isConnected())
        m_client->eDisconnect();
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
                                 const std::string&            sym)
{
    m_mgr = mgr;
    m_sym = sym;
}

void IBKRTrader::startDataStream(const std::string& symbol)
{
    if (!isConnected()) return;

    std::cout << "[IBKRTrader] Starting supplementary data streams for " << symbol << '\n';

    // tick-by-tick last & bid/ask streams (supplementary to main market data)
    static int TBT = 7'000;
    m_client->reqTickByTickData(TBT++, m_contract, "AllLast", 0, false);
    m_client->reqTickByTickData(TBT++, m_contract, "BidAsk",  0, false);

    // 5-second realtime bars (supplementary to main market data)
    static int BAR = 6'000;
    m_client->reqRealTimeBars(BAR++, m_contract, 5, "TRADES", false, {});

    std::cout << "[IBKRTrader] supplementary data streams started for " << symbol << '\n';
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
    sd.volume    = static_cast<int>(vol);

    m_mgr->addTick(sd);
}

// ───────────────────────────────────────────────────────────────
//  EWrapper overrides – minimal implementations
// ───────────────────────────────────────────────────────────────
void IBKRTrader::tickString(TickerId id, TickType field,
                            const std::string& v)
{
    if (field != 45 /*Last Timestamp*/ && field != 49 /*RTVolume*/)
        return;

    // For demo: parse last price & volume from RTVolume field
    // "price;size;…"
    double price = 0.0, size = 0.0;
    if (field == 49) {
        std::sscanf(v.c_str(), "%lf;%lf", &price, &size);
        routeToModel(price, 0, 0, 0, 0, size,
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count());
    }
}

void IBKRTrader::realtimeBar(TickerId /*reqId*/, long time,
                             double /*open*/, double /*high*/,
                             double /*low*/,  double close,
                             Decimal volume, Decimal /*wap*/, int /*count*/)
{
    routeToModel(close, 0, 0, 0, 0,
                 static_cast<double>(DecimalFunctions::decimalToDouble(volume)),
                 static_cast<std::uint64_t>(time) * 1'000);
}

void IBKRTrader::tickByTickAllLast(int /*reqId*/, int /*tickType*/, time_t t,
                                   double price, Decimal size,
                                   const TickAttribLast&,
                                   const std::string&, const std::string&)
{
    routeToModel(price, 0, 0, 0, 0,
                 DecimalFunctions::decimalToDouble(size),
                 static_cast<std::uint64_t>(t) * 1'000);
}

void IBKRTrader::tickByTickBidAsk(int /*reqId*/, time_t t,
                                  double bidPrice, double askPrice,
                                  Decimal bidSize, Decimal askSize,
                                  const TickAttribBidAsk&)
{
    routeToModel(0, bidPrice, askPrice,
                 static_cast<int>(DecimalFunctions::decimalToDouble(bidSize)),
                 static_cast<int>(DecimalFunctions::decimalToDouble(askSize)),
                 0,
                 static_cast<std::uint64_t>(t) * 1'000);
}

void IBKRTrader::error(int id, long /*t*/, int code,
                       const std::string& msg,
                       const std::string&)
{
    // suppress noisy heartbeat codes
    if (code == 2104 || code == 2106) return;

    std::cerr << "[IBKRTrader] ERROR id=" << id
              << " code=" << code << " : "
              << msg << '\n';
}

} // namespace connection
