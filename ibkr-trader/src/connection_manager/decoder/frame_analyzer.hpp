// frame_analyzer.hpp - Lean analyzer for IBKR volume/VWAP extraction
//
// Purpose: Extract actual volume and VWAP data from tick string field 48

#ifndef FRAME_ANALYZER_HPP
#define FRAME_ANALYZER_HPP

#include <string>
#include <vector>
#include <cstddef>
#include <cstring>

// Forward declaration for decoder namespace
namespace ibkr_decoder {
    class IBKRDecoder;
}

namespace ibkr_frame_analyzer {


    struct TickStringResult {
        bool hasDecodedData = false;
        std::string decodedValue;
        std::string dataType;
        uint64_t timestamp = 0;
        double numericValue = 0.0;
        double volume = 0.0;       // Isolated volume value
        double vwap = 0.0;         // Isolated VWAP value
    };

    // Structure to hold analyzed tick-by-tick trade data
    struct AnalyzedTickByTickData {
        int reqId;
        int tickType;
        uint64_t epochTime;
        std::string formattedTime;
        double price;
        double volume;           // Decoded from Decimal
        std::string exchange;
        std::string specialConditions;
        bool pastLimit;
        bool unreported;
        
        // Derived calculations
        double dollarsTraded;    // price * volume
        bool hasValidTrade;
    };

    // Structure to hold analyzed tick-by-tick bid-ask data
    struct AnalyzedTickByTickBidAskData {
        int reqId;
        uint64_t epochTime;
        std::string formattedTime;
        double bidPrice;
        double askPrice;
        double bidSize;          // Decoded from Decimal
        double askSize;          // Decoded from Decimal
        bool bidPastLow;
        bool askPastHigh;
        
        // Derived calculations
        double spread;           // askPrice - bidPrice
        double spreadPercent;    // (spread / midPoint) * 100
        double midPoint;         // (bidPrice + askPrice) / 2
        bool hasValidSpread;
        bool hasValidMidPoint;
    };

    // Structure to hold analyzed realtime bar data
    struct AnalyzedBarData {
        int reqId;
        int epochTime;
        std::string formattedTime;
        double open;
        double high;
        double low;
        double close;
        double volume;
        double wap;
        int count;
        double priceChange;
        double percentChange;
        double barRange;
        bool hasValidPriceChange;
        bool hasValidRange;
    };

    class FrameAnalyzer {
    public:
        /**
         * @brief Constructor for FrameAnalyzer instance
         */
        FrameAnalyzer() = default;
        
        /**
         * @brief Constructor for FrameAnalyzer instance with decoder
         */
        FrameAnalyzer(ibkr_decoder::IBKRDecoder& decoder);
        
        /**
         * @brief Destructor for FrameAnalyzer instance  
         */
        ~FrameAnalyzer() = default;
        

        
        // Analyze realtime bar data for OHLC, volume, and WAP information
        AnalyzedBarData analyzeRealtimeBarData(int reqId, int time, double open, double high, double low, 
                                              double close, double volume, double wap, int count);
        
        // Analyze tick-by-tick trade data for individual trade information
        AnalyzedTickByTickData analyzeTickByTickData(int reqId, int tickType, time_t time, double price, 
                                                    double size, const std::string& exchange, 
                                                    const std::string& specialConditions, 
                                                    bool pastLimit, bool unreported);
        
        // Analyze tick-by-tick bid-ask data for bid/ask quote information
        AnalyzedTickByTickBidAskData analyzeTickByTickBidAskData(int reqId, time_t time, double bidPrice, double askPrice,
                                                                double bidSize, double askSize, 
                                                                bool bidPastLow, bool askPastHigh);
        
        // Analyze tick string data for volume and VWAP extraction
        TickStringResult analyzeTickStringData(int tickerId, int field, const std::string& value);
        
    private:
        // Forward declaration for decoder
        ibkr_decoder::IBKRDecoder* m_decoder = nullptr;
        
        // Helper methods for tick string processing
        TickStringResult processField48(const std::string& value);
        TickStringResult processField88(const std::string& value);
        TickStringResult processGenericField(int field, const std::string& value) {
            TickStringResult result;
            result.hasDecodedData = false;
            return result;
        }
        
        // Utility methods
        std::vector<std::string> splitString(const std::string& str, char delimiter);
        std::vector<std::string> extractFieldsFromMessage(const char* msgData, size_t msgLength);
    };

} // namespace ibkr_frame_analyzer

#endif // FRAME_ANALYZER_HPP 