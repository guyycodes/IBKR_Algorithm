// Decoder.hpp
//
// This file contains the declaration of the Decoder class, which is responsible for decoding
// the incoming data from the IBKR API.
//
// The Decoder class is responsible for:
// - Decoding the incoming data from the IBKR API.

#ifndef DECODER_HPP
#define DECODER_HPP

#include <string>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cfloat>
#include "ibkr/cppclient/client/CommonDefs.h"
#include "ibkr/cppclient/client/EWrapper.h" // Contains TickType definitions
#include "ibkr/cppclient/client/Decimal.h"

namespace ibkr_decoder {

    class IBKRDecoder {
    public:
        // ============ LEGACY CUSTOM DECODING METHODS ============
        // These methods maintain compatibility with existing connection code
        
        // Check if a size value is encoded with special format
        static bool isSpecialSizeValue(double size);
        
        // Main decoder for special size values (legacy custom implementation)
        static double interpretSizeValue(double size, int tickType);
        
        // Utility methods for specific size types (legacy custom implementation)
        static double decodeBidAskSize(uint64_t encodedValue);
        static double decodeLastSize(uint64_t encodedValue);
        static double decodeVolume(uint64_t encodedValue);
        
        // Debugging and analysis utilities
        static std::string formatHexDump(uint64_t value);
        static void logSizeValueComponents(uint64_t sizeAsInt, int tickType);
        
        // ============ NEW IBKR-COMPLIANT DECODING METHODS ============
        // These methods implement the official IBKR decoding approach
        
        // Raw message parsing utilities (based on official IBKR implementation)
        static bool checkOffset(const char* ptr, const char* endPtr);
        static const char* findFieldEnd(const char* ptr, const char* endPtr);
        
        // Field decoders (based on official IBKR EDecoder)
        static bool decodeField(bool& boolValue, const char*& ptr, const char* endPtr);
        static bool decodeField(int& intValue, const char*& ptr, const char* endPtr);
        static bool decodeField(long& longValue, const char*& ptr, const char* endPtr);
        static bool decodeField(long long& longLongValue, const char*& ptr, const char* endPtr);
        static bool decodeField(double& doubleValue, const char*& ptr, const char* endPtr);
        static bool decodeField(std::string& stringValue, const char*& ptr, const char* endPtr);
        static bool decodeField(Decimal& decimalValue, const char*& ptr, const char* endPtr);
        
        // Max field decoders for optional values
        static bool decodeFieldMax(int& intValue, const char*& ptr, const char* endPtr);
        static bool decodeFieldMax(double& doubleValue, const char*& ptr, const char* endPtr);
        
        // High-level message processors
        struct TickPriceData {
            int tickerId;
            int tickType;
            double price;
            Decimal size;
            bool canAutoExecute;
            bool pastLimit;
            bool preOpen;
        };
        
        struct TickSizeData {
            int tickerId;
            int tickType;
            Decimal size;
        };
        
        static bool processTickPriceMessage(const char*& ptr, const char* endPtr, TickPriceData& data, int serverVersion);
        static bool processTickSizeMessage(const char*& ptr, const char* endPtr, TickSizeData& data);
        
        // Raw message analysis for debugging
        static void analyzeRawMessage(const char* msgData, size_t msgLength);
        static std::vector<std::string> extractFields(const char* msgData, size_t msgLength);
        
        // Utility to convert Decimal to double for compatibility
        static double decimalToDouble(Decimal decimal);
        static Decimal doubleToDecimal(double value);
        
        // Tick string analysis for volume/VWAP extraction
        static void analyzeTickStringMessage(int tickerId, int field, const std::string& value);
        
    private:
        // Constants for decoding
        static constexpr double DEFAULT_LOT_SIZE = 100.0;
        static constexpr double MINIMUM_SPECIAL_SIZE = 1.0e15;
        static constexpr int DECODER_UNSET_INTEGER = INT_MAX;
        static constexpr double DECODER_UNSET_DOUBLE = DBL_MAX;
        static constexpr const char* INFINITY_STR = "Infinity";
        
        // Helper methods (legacy custom implementation)
        static uint16_t extractLowSize(uint64_t encodedValue);
        static uint32_t extractLowSize32(uint64_t encodedValue);
        static uint8_t extractFlagByte(uint64_t encodedValue);
        static uint8_t extractSecondFlagByte(uint64_t encodedValue);
        static bool isDelayedDataFlag(uint8_t flagByte);
    };

} // namespace ibkr_decoder

// Convenience macros for IBKR-style decoding (optional usage)
#define DECODE_FIELD(x) if (!ibkr_decoder::IBKRDecoder::decodeField(x, ptr, endPtr)) return false;
#define DECODE_FIELD_MAX(x) if (!ibkr_decoder::IBKRDecoder::decodeFieldMax(x, ptr, endPtr)) return false;

#endif // DECODER_HPP