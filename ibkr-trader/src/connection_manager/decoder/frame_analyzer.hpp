#pragma once
// ───────────────────────────────────────────────────────────────
//  FrameAnalyzer – lightweight, allocation-free helpers that
//  derive extra metrics (VWAP, spread%, etc.) from callback data.
// ───────────────────────────────────────────────────────────────
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <ctime>

namespace ibkr_decoder { class IBKRDecoder; }

namespace ibkr_frame_analyzer {

struct TickStringResult {
    bool   hasDecodedData{false};
    std::string dataType, decodedValue;
    uint64_t timestamp{0};
    double numericValue{0}, volume{0}, vwap{0};
};

struct AnalyzedBarData {
    int reqId{}, epochTime{}, count{};
    std::string formattedTime;
    double open{}, high{}, low{}, close{}, volume{}, wap{};
    double priceChange{}, percentChange{}, barRange{};
    bool hasValidPriceChange{false}, hasValidRange{false};
};

struct AnalyzedTickByTickData {
    int reqId{}, tickType{};
    uint64_t epochTime{};
    std::string formattedTime, exchange, specialConditions;
    double price{}, volume{}, dollarsTraded{};
    bool pastLimit{}, unreported{}, hasValidTrade{};
};

struct AnalyzedTickByTickBidAskData {
    int reqId{};
    uint64_t epochTime{};
    std::string formattedTime;
    double bidPrice{}, askPrice{}, bidSize{}, askSize{};
    bool bidPastLow{}, askPastHigh{};
    double spread{}, spreadPercent{}, midPoint{};
    bool hasValidSpread{}, hasValidMidPoint{};
};

class FrameAnalyzer {
public:
    FrameAnalyzer() = default;
    explicit FrameAnalyzer(ibkr_decoder::IBKRDecoder& d) noexcept : m_decoder{&d} {}
    ~FrameAnalyzer() = default;

    /* high-level helpers used by connection.cpp */
    TickStringResult analyzeTickStringData(int tickerId,int field,std::string_view v);
    AnalyzedBarData  analyzeRealtimeBarData(int reqId,int epoch,
                                            double o,double h,double l,double c,
                                            double vol,double wap,int count);
    AnalyzedTickByTickData
        analyzeTickByTickData(int reqId,int tickType,time_t,
                              double price,double size,
                              std::string_view ex,std::string_view spec,
                              bool pastLimit,bool unreported);
    AnalyzedTickByTickBidAskData
        analyzeTickByTickBidAskData(int reqId,time_t,
                                    double bid,double ask,
                                    double bidSz,double askSz,
                                    bool bidPastLow,bool askPastHigh);

private:
    ibkr_decoder::IBKRDecoder* m_decoder{};          // optional

    /* helpers */
    static std::vector<std::string_view> splitView(std::string_view sv,char delim);
};

} // namespace ibkr_frame_analyzer
