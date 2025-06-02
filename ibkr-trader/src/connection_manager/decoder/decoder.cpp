#include "decoder.hpp"
#include <cstring>
#include <bitset>
#include <iostream>

#ifndef IBKR_DECODER_VERBOSE
#define IBKR_DECODER_VERBOSE 0
#endif

#ifndef DECODE_FIELD
#define DECODE_FIELD(x) if (!ibkr_decoder::IBKRDecoder::decodeField(x, ptr, end)) return false;
#endif
#ifndef DECODE_FIELD_MAX
#define DECODE_FIELD_MAX(x) if (!ibkr_decoder::IBKRDecoder::decodeFieldMax(x, ptr, end)) return false;
#endif

namespace ibkr_decoder {

/* -------------------------------------------------- generic helpers */

const char* IBKRDecoder::findFieldEnd(const char* p,const char* end)
{
    return static_cast<const char*>(std::memchr(p, 0, end - p));
}

template<> bool IBKRDecoder::decodeField(int& v,const char*& p,const char* e){
    if(!checkOffset(p,e)) return false;
    const char* stop=findFieldEnd(p,e); if(!stop) return false;
    v=std::atoi(p); p=++stop; return true;
}
template<> bool IBKRDecoder::decodeField(long& v,const char*& p,const char* e){
    if(!checkOffset(p,e)) return false;
    const char* stop=findFieldEnd(p,e); if(!stop) return false;
    v=std::atol(p); p=++stop; return true;
}
template<> bool IBKRDecoder::decodeField(double& v,const char*& p,const char* e){
    if(!checkOffset(p,e)) return false;
    const char* stop=findFieldEnd(p,e); if(!stop) return false;
    v = (std::strcmp(p,INFINITY_STR)==0) ? INFINITY : std::atof(p);
    p = ++stop; return true;
}
template<> bool IBKRDecoder::decodeField(std::string& s,const char*& p,const char* e){
    if(!checkOffset(p,e)) return false;
    const char* stop=findFieldEnd(p,e); if(!stop) return false;
    s.assign(p, stop); p=++stop; return true;
}
template<> bool IBKRDecoder::decodeField(Decimal& d,const char*& p,const char* e){
    if(!checkOffset(p,e)) return false;
    const char* stop=findFieldEnd(p,e); if(!stop) return false;
    d = DecimalFunctions::stringToDecimal(p); p=++stop; return true;
}

bool IBKRDecoder::decodeFieldMax(int& v,const char*& p,const char* e){
    std::string tmp; if(!decodeField(tmp,p,e)) return false;
    v = tmp.empty() ? DECODER_UNSET_INT : std::atoi(tmp.c_str()); return true;
}
bool IBKRDecoder::decodeFieldMax(double& v,const char*& p,const char* e){
    std::string tmp; if(!decodeField(tmp,p,e)) return false;
    v = tmp.empty() ? DECODER_UNSET_DBL : std::atof(tmp.c_str()); return true;
}

/* -------------------------------------------------- message decoders  */

bool IBKRDecoder::processRealtimeBarMessage(const char*& ptr,const char* end,RealtimeBarData& d)
{
    DECODE_FIELD(d.version); DECODE_FIELD(d.reqId); DECODE_FIELD(d.time);
    DECODE_FIELD(d.open);    DECODE_FIELD(d.high);  DECODE_FIELD(d.low);
    DECODE_FIELD(d.close);   DECODE_FIELD(d.volume);DECODE_FIELD(d.wap);
    DECODE_FIELD(d.count);

#if IBKR_DECODER_VERBOSE
    std::cout << "[Decoder] RTBar req="<<d.reqId<<" t="<<d.time
              <<" ohlc="<<d.open<<"/"<<d.high<<"/"<<d.low<<"/"<<d.close
              <<" vol="<<DecimalFunctions::decimalToDouble(d.volume) << '\n';
#endif
    return true;
}

bool IBKRDecoder::processTickByTickMessage(const char*& ptr,const char* end,TickByTickData& d)
{
    DECODE_FIELD(d.reqId); DECODE_FIELD(d.tickType); DECODE_FIELD(d.time);
    if (d.tickType==1||d.tickType==2){           // AllLast / Last
        DECODE_FIELD(d.price); DECODE_FIELD(d.size); DECODE_FIELD(d.attrMask);
        std::bitset<32> m(d.attrMask); d.pastLimit=m[0]; d.unreported=m[1];
        DECODE_FIELD(d.exchange); DECODE_FIELD(d.special);
#if IBKR_DECODER_VERBOSE
        std::cout<<"[Decoder] TBT req="<<d.reqId<<" p="<<d.price<<" sz="
                 <<DecimalFunctions::decimalToDouble(d.size)<<"\n";
#endif
        return true;
    }
    return false;
}

} // namespace ibkr_decoder
