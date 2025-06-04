#pragma once
#include <unordered_map>
#include <string>
#include <string_view>
#include <cstdint>
#include "../../models/stock_data_tick/stock_data_tick.hpp"
namespace ibkr_decoder { class IBKRDecoder; }

namespace connection {

// Transparent hash and equality helpers for GCC 11 compatibility
struct StrHash {                        // transparent hash
    using is_transparent = void;         // <- enables heterogeneous lookup

    size_t operator()(std::string_view sv) const noexcept
    { return std::hash<std::string_view>{}(sv); }

    size_t operator()(const std::string& s) const noexcept
    { return std::hash<std::string_view>{}(s); }
};

struct StrEqual {                       // transparent equality
    using is_transparent = void;

    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    bool operator()(const std::string& a, const std::string& b) const noexcept { return a == b; }
    bool operator()(const std::string& a, std::string_view  b) const noexcept { return a == b; }
    bool operator()(std::string_view  a, const std::string& b) const noexcept { return a == b; }
};

struct CacheResult {
    stock_data_tick::StockData data;
    bool tickChanged{false};
    bool isComplete{false};
};

class ConnectionCache {
public:
    explicit ConnectionCache(ibkr_decoder::IBKRDecoder& d) noexcept : m_dec{d}{ m_map.reserve(1);  }
    ~ConnectionCache() = default;

    CacheResult merge(std::string_view sym, uint64_t ts,
                      double last,double vol,
                      double bid,double ask,
                      double bidSz,double askSz,
                      std::string_view ex = {},
                      double o=0,double h=0,double l=0,
                      double c=0,double vwap=0,double priceChange=0,double barRange=0,
                      double spread=0,double spreadPercent=0,double midPoint=0);

    int prune(int maxAgeMin = 60);         // no-op for single-symbol but kept for API compatibility

private:
    std::unordered_map<
        std::string,
        stock_data_tick::StockData,
        StrHash,            // transparent hash
        StrEqual            // transparent equality
    > m_map;
    ibkr_decoder::IBKRDecoder& m_dec;
    static bool isComplete(const stock_data_tick::StockData&);
};

} // namespace connection
