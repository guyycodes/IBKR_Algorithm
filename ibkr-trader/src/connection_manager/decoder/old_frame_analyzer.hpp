// // old_frame_analyzer.hpp - Lean analyzer for IBKR volume/VWAP extraction
// //
// // Purpose: Extract actual volume and VWAP data from tick string field 48

// #ifndef FRAME_ANALYZER_HPP
// #define FRAME_ANALYZER_HPP

// #include <string>
// #include <vector>
// #include <cstddef>
// #include <cstring>

// // Include IBKR types for tick data analysis
// #include "ibkr/cppclient/client/TickAttrib.h"

// // Forward declaration to avoid circular dependency
// namespace ibkr_decoder {
//     class IBKRDecoder;
// }

// namespace ibkr_frame_analyzer {

//     struct TickStringResult {
//         bool hasDecodedData = false;
//         std::string decodedValue;
//         std::string dataType;
//         uint64_t timestamp = 0;
//         double numericValue = 0.0;
//         double volume = 0.0;       // Isolated volume value
//         double vwap = 0.0;         // Isolated VWAP value
//     };

//     struct TickPriceResult {
//         bool hasDecodedData = false;
//         std::string dataType;
//         double decodedPrice = 0.0;
//         int tickerId = 0;
//         int fieldType = 0;
//     };

//     struct TickSizeResult {
//         bool hasDecodedData = false;
//         std::string dataType;
//         double decodedSize = 0.0;
//         int tickerId = 0;
//         int fieldType = 0;
//         bool isSpecialValue = false;
//     };

//     class FrameAnalyzer {
//     public:
//         /**
//          * @brief Constructor for FrameAnalyzer instance
//          */
//         FrameAnalyzer() = default;
        
//         /**
//          * @brief Constructor for FrameAnalyzer instance with decoder reference
//          * @param decoder Reference to the decoder instance for IBKR wrapper methods
//          */
//         FrameAnalyzer(ibkr_decoder::IBKRDecoder& decoder);
        
//         /**
//          * @brief Destructor for FrameAnalyzer instance  
//          */
//         ~FrameAnalyzer() = default;
        
//         // Main tick string analysis function - delegates to specific handlers
//         TickStringResult analyzeTickStringData(int tickerId, int field, const std::string& value);
        
//         // Analyze tick price data for BID/ASK/LAST prices
//         TickPriceResult analyzeTickPriceData(int tickerId, int field, double price, const TickAttrib& attrib);
        
//         // Analyze tick size data for BID_SIZE/ASK_SIZE/LAST_SIZE/VOLUME
//         TickSizeResult analyzeTickSizeData(int tickerId, int field, double size);
        
//         // Analyze tick string field 48 to extract volume and VWAP
//         // Expected format: price;size;timestamp;volume;vwap;flag
//         void analyzeTickString48(const std::string& tickStringValue);
        
//         // Analyze tick-by-tick trade data for individual trades
//         void analyzeTickByTickData(int reqId, int tickType, time_t time, double price, 
//                                   uint64_t rawDecimal, double actualVolume,
//                                   const std::string& exchange, const std::string& specialConditions,
//                                   bool pastLimit, bool unreported);
        
//         // Analyze raw frame data to understand message structure
//         void analyzeRawFrame(const char* frameData, size_t frameLength);
        
//     private:
//         // Optional decoder reference for IBKR wrapper methods
//         ibkr_decoder::IBKRDecoder* m_decoder = nullptr;
        
//         // Helper to split string by delimiter
//         std::vector<std::string> splitString(const std::string& str, char delimiter);
        
//         // Helper to extract null-delimited fields from message
//         std::vector<std::string> extractFieldsFromMessage(const char* msgData, size_t msgLength);
        
//         // Specific field handlers
//         TickStringResult processField48(const std::string& value);
//         TickStringResult processField88(const std::string& value);
//         TickStringResult processGenericField(int field, const std::string& value);
//     };

// } // namespace ibkr_frame_analyzer

// #endif // FRAME_ANALYZER_HPP 