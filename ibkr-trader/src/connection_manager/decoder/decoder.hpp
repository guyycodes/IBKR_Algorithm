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
#include "ibkr/cppclient/client/CommonDefs.h"
#include "ibkr/cppclient/client/EWrapper.h" // Contains TickType definitions

namespace ibkr_decoder {

    class IBKRDecoder {
    public:
        // Check if a size value is encoded with special format
        static bool isSpecialSizeValue(double size);
        
        // Main decoder for special size values
        static double interpretSizeValue(double size, int tickType);
        
        // Utility methods for specific size types
        static double decodeBidAskSize(uint64_t encodedValue);
        static double decodeLastSize(uint64_t encodedValue);
        static double decodeVolume(uint64_t encodedValue);
        
        // Debugging and analysis utilities
        static std::string formatHexDump(uint64_t value);
        static void logSizeValueComponents(uint64_t sizeAsInt, int tickType);
        
    private:
        // Constants for decoding
        static constexpr double DEFAULT_LOT_SIZE = 100.0;
        static constexpr double MINIMUM_SPECIAL_SIZE = 1.0e15;
        
        // Helper methods
        static uint16_t extractLowSize(uint64_t encodedValue);
        static uint32_t extractLowSize32(uint64_t encodedValue);
        static uint8_t extractFlagByte(uint64_t encodedValue);
        static uint8_t extractSecondFlagByte(uint64_t encodedValue);
        static bool isDelayedDataFlag(uint8_t flagByte);
    };

} // namespace ibkr_decoder

#endif // DECODER_HPP