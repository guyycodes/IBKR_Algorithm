#include "connection_cache.hpp"
#include "../../decoder/decoder.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>

#ifndef CONN_CACHE_VERBOSE
#define CONN_CACHE_VERBOSE 0
#endif

namespace connection {

using namespace std::chrono;

static uint64_t nowEpochMs()
{
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool ConnectionCache::isComplete(const stock_data_tick::StockData& d)
{
    // For real-time trading, we need at minimum: last price, bid/ask quotes with sizes
    // Volume and VWAP are supplementary and not always available in tick-by-tick data
    return d.last>0 && d.bid>0 && d.ask>0 &&
           d.bidSize>0 && d.askSize>0 && d.timestamp>0;
    // Note: volume and vwap are optional for tick-by-tick data
}

CacheResult ConnectionCache::merge(std::string_view sym, uint64_t ts,
                                   double last,double vol,
                                   double bid,double ask,
                                   double bidSz,double askSz,
                                   std::string_view ex,
                                   double o,double h,double l,
                                   double c,double vwap)
{
    auto& st = m_map[std::string(sym)];            // one entry reused forever

    bool changed = false;
    auto chg = [&](auto& field, auto v){ if(v>0 && field!=v){ field=v; changed=true; } };

    // Track tick-by-tick specific changes
    bool tickByTickChanged = false;
    if (bid > 0 && st.bid != bid) tickByTickChanged = true;
    if (ask > 0 && st.ask != ask) tickByTickChanged = true;
    if (bidSz > 0 && st.bidSize != bidSz) tickByTickChanged = true;
    if (askSz > 0 && st.askSize != askSz) tickByTickChanged = true;
    if (last > 0 && st.last != last) tickByTickChanged = true;

    chg(st.last,     last);
    chg(st.bid,      bid);
    chg(st.ask,      ask);
    chg(st.bidSize,  bidSz);
    chg(st.askSize,  askSz);
    chg(st.volume,   vol);
    chg(st.open,     o);  chg(st.high,h); chg(st.low,l); chg(st.close,c); chg(st.vwap,vwap);

    if(!ex.empty())          st.exchange = ex;
    if(ts==0)                ts = nowEpochMs();
    chg(st.timestamp, ts);                         // must be after ts calc
    st.symbol = sym;

    bool complete = isComplete(st);

#if CONN_CACHE_VERBOSE
    std::cout<<"[Cache] "<<sym<<" upd "
             <<"L="<<st.last<<" B="<<st.bid<<" A="<<st.ask
             <<"@("<<st.bidSize<<'/'<<st.askSize<<") V="<<st.volume<<'\n';
#endif

    // Add detailed logging like legacy code
    if (!complete) {
        std::cout << "[Cache][DEBUG] ❌ Data incomplete for " << sym << ":" << std::endl;
        std::cout << "  last: " << st.last << " (required > 0)" << std::endl;
        std::cout << "  bid: " << st.bid << " (required > 0)" << std::endl;
        std::cout << "  ask: " << st.ask << " (required > 0)" << std::endl;
        std::cout << "  bidSize: " << st.bidSize << " (required > 0)" << std::endl;
        std::cout << "  askSize: " << st.askSize << " (required > 0)" << std::endl;
        std::cout << "  timestamp: " << st.timestamp << " (required > 0)" << std::endl;
        std::cout << "  volume: " << st.volume << " (optional, current: " << st.volume << ")" << std::endl;
        std::cout << "  vwap: " << st.vwap << " (optional, current: " << st.vwap << ")" << std::endl;
    } else if (tickByTickChanged) {
        std::cout << "[Cache][DEBUG] ✅ Data ready for " << sym << " (tick-by-tick changed)" << std::endl;
        std::cout << "  last: $" << std::fixed << std::setprecision(4) << st.last << std::endl;
        std::cout << "  bid: $" << std::fixed << std::setprecision(4) << st.bid << " x " << static_cast<int>(st.bidSize) << std::endl;
        std::cout << "  ask: $" << std::fixed << std::setprecision(4) << st.ask << " x " << static_cast<int>(st.askSize) << std::endl;
        if (st.vwap > 0) {
            std::cout << "  vwap: $" << std::fixed << std::setprecision(5) << st.vwap << std::endl;
        }
        if (st.volume > 0) {
            std::cout << "  volume: " << static_cast<int>(st.volume) << std::endl;
        }
    }

    return {st, tickByTickChanged, complete};
}

int ConnectionCache::prune(int) { return 0; }      // nothing ever removed (single-symbol)

} // namespace connection
