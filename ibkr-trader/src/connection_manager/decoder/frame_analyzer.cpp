#include "frame_analyzer.hpp"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <iostream>

#ifndef IBKR_FRAME_VERBOSE
#define IBKR_FRAME_VERBOSE 0
#endif

namespace ibkr_frame_analyzer {

/* ───────────────────── utilities ─────────────────────────── */
std::vector<std::string_view> FrameAnalyzer::splitView(std::string_view in,char d)
{
    std::vector<std::string_view> out;
    size_t b=0;
    while (b<in.size()){
        size_t e=in.find(d,b);
        if (e==std::string_view::npos) e=in.size();
        out.emplace_back(in.substr(b, e-b));
        b=e+1;
    }
    return out;
}

// Helper function from legacy code for string splitting
std::vector<std::string> FrameAnalyzer::splitString(const std::string& str, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream ss(str);
    std::string field;
    
    while (std::getline(ss, field, delimiter)) {
        fields.push_back(field);
    }
    
    return fields;
}

/* ─────────────── Tick-String (field 48 / 88) ─────────────── */
TickStringResult FrameAnalyzer::analyzeTickStringData(int /*tickerId*/,int field,std::string_view v)
{
    TickStringResult r;
    if (field==48){                                      // price;size;ts;vol;vwap;flag
        auto parts=splitView(v,';');
        if (parts.size()>=5){
            try{
                double volume  = std::stod(std::string(parts[3]));
                double vwap    = std::stod(std::string(parts[4]));
                r.hasDecodedData=true;
                r.dataType="VOL+VWAP";
                r.volume=volume/1'0000.0;                 // millions
                r.vwap  =vwap;
                std::ostringstream ss;
                ss<<std::fixed<<std::setprecision(2)<<r.volume<<"M VWAP="
                  <<std::setprecision(5)<<r.vwap;
                r.decodedValue=ss.str();
            }catch(...){}
        }
    } else if (field==88){                               // timestamp
        try{
            r.timestamp = std::stoull(std::string(v))*1000ULL;
            r.numericValue = static_cast<double>(r.timestamp);
            r.dataType="TIMESTAMP";
            r.hasDecodedData=true;
        }catch(...){}
    }
#if IBKR_FRAME_VERBOSE
    if(r.hasDecodedData)
        std::cout<<"[FA] "<<r.dataType<<" -> "<<r.decodedValue<<'\n';
#endif
    return r;
}

/* ─────────────── 5-sec realtime bars ─────────────────────── */
AnalyzedBarData FrameAnalyzer::analyzeRealtimeBarData(int req,int epoch,
                                                      double o,double h,double l,double c,
                                                      double vol,double wap,int cnt)
{
    AnalyzedBarData a;
    a.reqId=req; a.epochTime=epoch; a.open=o; a.high=h; a.low=l; a.close=c;
    a.volume=vol; a.wap=wap; a.count=cnt;

    // Format time as human-readable (from legacy code)
    time_t epochTime = static_cast<time_t>(epoch);
    char timeStr[30];
    struct tm timeinfo;
    
#ifdef _WIN32
    localtime_s(&timeinfo, &epochTime);
#else
    localtime_r(&epochTime, &timeinfo);
#endif
    
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    a.formattedTime = std::string(timeStr);

    // Calculate price change for the bar (enhanced from legacy)
    if(o>0){ 
        a.priceChange=c-o; 
        a.percentChange=(a.priceChange/o)*100.0; 
        a.hasValidPriceChange=true; 
    } else {
        a.priceChange = 0.0;
        a.percentChange = 0.0;
        a.hasValidPriceChange = false;
    }
    
    // Calculate bar range (enhanced from legacy)
    if(h>0&&l>0){ 
        a.barRange=h-l; 
        a.hasValidRange=true; 
    } else {
        a.barRange = 0.0;
        a.hasValidRange = false;
    }

#if IBKR_FRAME_VERBOSE
    std::cout<<"[FA] RTBar "<<a.formattedTime<<" Δ="<<a.priceChange<<" rng="<<a.barRange<<'\n';
#endif
    return a;
}

/* ───────────── Tick-by-Tick helpers ──────────────────────── */
AnalyzedTickByTickData
FrameAnalyzer::analyzeTickByTickData(int req,int type,time_t t,double p,double sz,
                                     std::string_view ex,std::string_view sp,
                                     bool pl,bool ur)
{
    AnalyzedTickByTickData a;
    a.reqId=req; a.tickType=type; a.epochTime=t; a.price=p; a.volume=sz;
    a.exchange=ex; a.specialConditions=sp; a.pastLimit=pl; a.unreported=ur;
    if(p>0&&sz>0){ a.dollarsTraded=p*sz; a.hasValidTrade=true; }

#if IBKR_FRAME_VERBOSE
    std::cout<<"[FA] TBT $" << a.dollarsTraded << '\n';
#endif
    return a;
}

AnalyzedTickByTickBidAskData
FrameAnalyzer::analyzeTickByTickBidAskData(int req,time_t t,double bid,double ask,
                                           double bidSz,double askSz,bool bpl,bool aph)
{
    AnalyzedTickByTickBidAskData a;
    a.reqId=req; a.epochTime=t; a.bidPrice=bid; a.askPrice=ask;
    a.bidSize=bidSz; a.askSize=askSz; a.bidPastLow=bpl; a.askPastHigh=aph;

    if(bid>0 && ask>0){
        a.spread=ask-bid;
        a.midPoint=(ask+bid)/2.0;
        a.spreadPercent=(a.spread/a.midPoint)*100.0;
        a.hasValidSpread=a.hasValidMidPoint=true;
    }

#if IBKR_FRAME_VERBOSE
    std::cout<<"[FA] Spread "<<a.spread<<" ("<<a.spreadPercent<<"%)\n";
#endif
    return a;
}

} // namespace ibkr_frame_analyzer
