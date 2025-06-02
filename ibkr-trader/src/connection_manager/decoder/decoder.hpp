#pragma once
// ───────────────────────────────────────────────────────────────
//  IBKRDecoder – *minimal* field/stream decoder (IBKR spec)
//  Only the pieces currently used by higher layers are included.
// ───────────────────────────────────────────────────────────────
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>
#include "ibkr/cppclient/client/CommonDefs.h"
#include "ibkr/cppclient/client/Decimal.h"

namespace ibkr_frame_analyzer { class FrameAnalyzer; }

namespace ibkr_decoder {

class IBKRDecoder {
public:
    explicit IBKRDecoder(ibkr_frame_analyzer::FrameAnalyzer& fa) noexcept
        : m_frameAnalyzer{fa} {}

    /* ---------- tiny helpers still used by connection layer --------- */
    static double decimalToDouble(Decimal d)            { return DecimalFunctions::decimalToDouble(d); }
    static Decimal doubleToDecimal(double v)            { return DecimalFunctions::doubleToDecimal(v); }

    /* ---------- generic “EDecoder-style” field parsers -------------- */
    static bool checkOffset(const char* p,const char* end){ return p && p<end; }
    static const char* findFieldEnd(const char* p,const char* end);

    template <typename T> static bool decodeField(T&, const char*&, const char*);      // generic
    static bool decodeFieldMax(int& v,    const char*& p,const char* e);
    static bool decodeFieldMax(double& v, const char*& p,const char* e);

    /* ---------- message structs used in diagnostics ----------------- */
    struct RealtimeBarData {
        int version{}, reqId{}, time{}, count{};
        double open{}, high{}, low{}, close{};
        Decimal volume, wap;
    };

    struct TickByTickData {
        int reqId{}, tickType{}, attrMask{};
        time_t time{};
        double price{};
        Decimal size;
        std::string exchange, special;
        bool pastLimit{}, unreported{};
    };

    /* ---------- parsers still referenced by FrameAnalyzer ----------- */
    bool processRealtimeBarMessage(const char*& , const char* , RealtimeBarData&);
    bool processTickByTickMessage(const char*& , const char* , TickByTickData&);

private:
    ibkr_frame_analyzer::FrameAnalyzer& m_frameAnalyzer;
    static constexpr const char* INFINITY_STR = "Infinity";
    static constexpr int   DECODER_UNSET_INT  = INT32_MAX;
    static constexpr double DECODER_UNSET_DBL = DBL_MAX;

    /* type-specific decode helpers – explicit specialisations in .cpp */
};

} // namespace ibkr_decoder
