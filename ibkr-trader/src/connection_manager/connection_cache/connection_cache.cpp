#include "connection_cache.hpp"
#include "../decoder/decoder.hpp"
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

CacheResult ConnectionCache::merge(std::string_view  sym,
                                   uint64_t          ts,
                                   double            last,
                                   double            vol,
                                   double            bid,
                                   double            ask,
                                   double            bidSz,
                                   double            askSz,
                                   std::string_view  ex,
                                   double            o,
                                   double            h,
                                   double            l,
                                   double            c,
                                   double            vwap,
                                   double            priceChange,
                                   double            barRange,
                                   double            spread,
                                   double            spreadPercent,
                                   double            midPoint)
{
    /* ---------------------------------------------------------------------
       1.  Lookup                 (zero allocations on hits)
       ------------------------------------------------------------------ */
    auto it = m_map.find(sym);                       // heterogeneous lookup (C++20)
    if (it == m_map.end())
        it = m_map.emplace(sym, stock_data_tick::StockData{}).first;

    stock_data_tick::StockData& st = it->second;

    /* ---------------------------------------------------------------------
       2.  Fast-path field updates (macro avoids the lambda capture)
       ------------------------------------------------------------------ */
#define CONN_CHG(field,val) \
        if ((val) > 0 && (val) != (field)) { (field) = (val); changed = true; }

    bool changed = false;

    /* tick-by-tick change tracker is folded into the update pass itself   */
    bool tickByTickChanged = false;

    auto track_and_set = [&](auto& field, double v){
        if (v > 0 && v != field) { field = v; tickByTickChanged = true; }
    };

    track_and_set(st.bid,     bid);
    track_and_set(st.ask,     ask);
    track_and_set(st.bidSize, bidSz);
    track_and_set(st.askSize, askSz);
    track_and_set(st.last,    last);          // mid-point or trade price

    /* Bulk updates that do **not** participate in tick-change semantics  */
    CONN_CHG(st.volume,   vol);
    CONN_CHG(st.open,     o);  CONN_CHG(st.high,h); CONN_CHG(st.low,l); CONN_CHG(st.close,c);
    CONN_CHG(st.vwap,     vwap);
    CONN_CHG(st.priceChange,  priceChange);
    CONN_CHG(st.barRange,     barRange);
    CONN_CHG(st.spread,       spread);
    CONN_CHG(st.spreadPercent,spreadPercent);
    CONN_CHG(st.midPoint,     midPoint);

    /* ---------------------------------------------------------------------
       3.  Non-critical fields – executed only when necessary
       ------------------------------------------------------------------ */
    if (!ex.empty())      st.exchange  = ex;     // String copy ONLY when feed sends it
    if (st.symbol.empty())     // symbol set only once
        st.symbol        = it->first;            // no copy on subsequent ticks

    /* Timestamp – avoid the expensive syscall unless caller passed zero  */
    
    ts = nowEpochMs();
    st.timestamp = ts;

    /* ---------------------------------------------------------------------
       4.  Completeness test – done **after** fast path so it never hurts
       ------------------------------------------------------------------ */
    const bool complete = isComplete(st);

#if CONN_CACHE_VERBOSE
    std::cout << "[Cache] " << sym << " L=" << st.last
              << " B=" << st.bid << " A=" << st.ask
              << " @(" << st.bidSize << '/' << st.askSize
              << ") V=" << st.volume << '\n';
    if (!complete) { /* verbose diagnostics here…*/ }
#endif

    // Add detailed logging like legacy code
    if (!complete) {
        // std::cout << "[Cache][DEBUG] ❌ Data incomplete for " << sym << ":" << std::endl;
        // std::cout << "  last: " << st.last << " (required > 0)" << std::endl;
        // std::cout << "  bid: " << st.bid << " (required > 0)" << std::endl;
        // std::cout << "  ask: " << st.ask << " (required > 0)" << std::endl;
        // std::cout << "  bidSize: " << st.bidSize << " (required > 0)" << std::endl;
        // std::cout << "  askSize: " << st.askSize << " (required > 0)" << std::endl;
        // std::cout << "  timestamp: " << st.timestamp << " (required > 0)" << std::endl;
        // std::cout << "  volume: " << st.volume << " (optional, current: " << st.volume << ")" << std::endl;
        // std::cout << "  vwap: " << st.vwap << " (optional, current: " << st.vwap << ")" << std::endl;
        // std::cout << "  priceChange: " << st.priceChange << " (optional, current: " << st.priceChange << ")" << std::endl;
        // std::cout << "  barRange: " << st.barRange << " (optional, current: " << st.barRange << ")" << std::endl;
        // std::cout << "  spread: " << st.spread << " (optional, current: " << st.spread << ")" << std::endl;
        // std::cout << "  spreadPercent: " << st.spreadPercent << " (optional, current: " << st.spreadPercent << ")" << std::endl;
        // std::cout << "  midPoint: " << st.midPoint << " (optional, current: " << st.midPoint << ")" << std::endl;
    } else if (tickByTickChanged) {
        // std::cout << "[Cache][DEBUG] ✅ Data ready for " << sym << " (tick-by-tick changed)" << std::endl;
        // std::cout << "  last: $" << std::fixed << std::setprecision(4) << st.last <<  std::endl;
        // std::cout << "  volume: " << st.volume << " (optional, current: " << st.volume << ")" << std::endl;
        // std::cout << "  bid: $" << std::fixed << std::setprecision(4) << st.bid << " x " << static_cast<int>(st.bidSize) << std::endl;
        // std::cout << "  ask: $" << std::fixed << std::setprecision(4) << st.ask << " x " << static_cast<int>(st.askSize) << std::endl;
        // std::cout << "  bidSize: " << st.bidSize << " (required > 0)" << std::endl;
        // std::cout << "  askSize: " << st.askSize << " (required > 0)" << std::endl;
        // std::cout << "  priceChange: " << st.priceChange << " (optional, current: " << st.priceChange << ")" << std::endl;
        // std::cout << "  barRange: " << st.barRange << " (optional, current: " << st.barRange << ")" << std::endl;
        // std::cout << "  spread: " << st.spread << " (optional, current: " << st.spread << ")" << std::endl;
        // std::cout << "  spreadPercent: " << st.spreadPercent << " (optional, current: " << st.spreadPercent << ")" << std::endl;
        // std::cout << "  midPoint: " << st.midPoint << " (optional, current: " << st.midPoint << ")" << std::endl;
        // std::cout << "  timestamp: " << st.timestamp << " (required > 0)" << std::endl;
        // std::cout << "  open: " << st.open << std::endl;
        // std::cout << "  high: " << st.high << std::endl;
        // std::cout << "  low: " << st.low << std::endl;
        // std::cout << "  close: " << st.close << std::endl;
        if (st.vwap > 0) {
            // std::cout << "  vwap: $" << std::fixed << std::setprecision(5) << st.vwap << std::endl;
        }
        if (st.volume > 0) {
            // std::cout << "  " << static_cast<int>(st.volume) <<"  shares @ " << st.last <<  std::endl;
        }
    }

    return {st, tickByTickChanged, complete};
}

int ConnectionCache::prune(int) { return 0; }      // nothing ever removed (single-symbol)

} // namespace connection
