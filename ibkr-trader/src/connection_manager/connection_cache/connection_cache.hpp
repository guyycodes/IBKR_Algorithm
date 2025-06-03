#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include "../../models/stock_data_tick/stock_data_tick.hpp"
namespace ibkr_decoder { class IBKRDecoder; }

namespace connection {

struct CacheResult {
    stock_data_tick::StockData data;
    bool tickChanged{false};
    bool isComplete{false};
};

class ConnectionCache {
public:
    explicit ConnectionCache(ibkr_decoder::IBKRDecoder& d) noexcept : m_dec{d}{}
    ~ConnectionCache() = default;

    CacheResult merge(std::string_view sym, uint64_t ts,
                      double last,double vol,
                      double bid,double ask,
                      double bidSz,double askSz,
                      std::string_view ex = {},
                      double o=0,double h=0,double l=0,
                      double c=0,double vwap=0);

    int prune(int maxAgeMin = 60);         // no-op for single-symbol but kept for API compatibility

private:
    std::unordered_map<std::string, stock_data_tick::StockData> m_map;
    ibkr_decoder::IBKRDecoder& m_dec;
    static bool isComplete(const stock_data_tick::StockData&);
};

} // namespace connection
