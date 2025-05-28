// frame_analyzer.hpp - Lean analyzer for IBKR volume/VWAP extraction
//
// Purpose: Extract actual volume and VWAP data from tick string field 48

#ifndef FRAME_ANALYZER_HPP
#define FRAME_ANALYZER_HPP

#include <string>
#include <vector>
#include <cstddef>
#include <cstring>

namespace ibkr_frame_analyzer {

    class FrameAnalyzer {
    public:
        /**
         * @brief Constructor for FrameAnalyzer instance
         */
        FrameAnalyzer() = default;
        
        /**
         * @brief Destructor for FrameAnalyzer instance  
         */
        ~FrameAnalyzer() = default;
        
        // Analyze tick string field 48 to extract volume and VWAP
        // Expected format: price;size;timestamp;volume;vwap;flag
        void analyzeTickString48(const std::string& tickStringValue);
        
        // Analyze tick-by-tick trade data for individual trades
        void analyzeTickByTickData(int reqId, int tickType, time_t time, double price, 
                                  uint64_t rawDecimal, double actualVolume,
                                  const std::string& exchange, const std::string& specialConditions,
                                  bool pastLimit, bool unreported);
        
        // Analyze raw frame data to understand message structure
        void analyzeRawFrame(const char* frameData, size_t frameLength);
        
    private:
        // Helper to split string by delimiter
        std::vector<std::string> splitString(const std::string& str, char delimiter);
        
        // Helper to extract null-delimited fields from message
        std::vector<std::string> extractFieldsFromMessage(const char* msgData, size_t msgLength);
    };

} // namespace ibkr_frame_analyzer

#endif // FRAME_ANALYZER_HPP 