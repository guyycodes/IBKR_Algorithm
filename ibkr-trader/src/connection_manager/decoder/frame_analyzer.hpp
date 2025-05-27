// frame_analyzer.hpp - Lean analyzer for IBKR volume/VWAP extraction
//
// Purpose: Extract actual volume and VWAP data from tick string field 48

#ifndef FRAME_ANALYZER_HPP
#define FRAME_ANALYZER_HPP

#include <string>
#include <vector>
#include <cstddef>
#include <cstring>

namespace ibkr_decoder {

    class FrameAnalyzer {
    public:
        // Analyze tick string field 48 to extract volume and VWAP
        // Expected format: price;size;timestamp;volume;vwap;flag
        static void analyzeTickString48(const std::string& tickStringValue);
        
        // Analyze raw frame data to understand message structure
        static void analyzeRawFrame(const char* frameData, size_t frameLength);
        
    private:
        // Helper to split string by delimiter
        static std::vector<std::string> splitString(const std::string& str, char delimiter);
        
        // Helper to extract null-delimited fields from message
        static std::vector<std::string> extractFieldsFromMessage(const char* msgData, size_t msgLength);
    };

} // namespace ibkr_decoder

#endif // FRAME_ANALYZER_HPP 