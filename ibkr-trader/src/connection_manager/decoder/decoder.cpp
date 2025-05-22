// Decoder.cpp
//
// This file contains the implementation of the Decoder class, which is responsible for decoding
// the incoming data from the IBKR API.

#include "decoder.hpp"
#include <iostream>

namespace ibkr_decoder {

    bool IBKRDecoder::isSpecialSizeValue(double size) {
        // Check if size is unrealistically large (IBKR encodes flags in large values)
        // The common pattern we've observed is values around 3.58e+18
        return (size > MINIMUM_SPECIAL_SIZE);
    }

    std::string IBKRDecoder::formatHexDump(uint64_t value) {
        std::stringstream hexStream;
        hexStream << std::hex << std::setfill('0');
        
        // Print each byte of the 64-bit value for detailed analysis
        for (int i = 7; i >= 0; i--) {
            uint8_t byte = (value >> (i * 8)) & 0xFF;
            hexStream << std::setw(2) << static_cast<int>(byte);
            if (i > 0) hexStream << " ";
        }
        
        return hexStream.str();
    }

    void IBKRDecoder::logSizeValueComponents(uint64_t sizeAsInt, int tickType) {
        uint16_t lowSize = extractLowSize(sizeAsInt);
        uint32_t lowSize32 = extractLowSize32(sizeAsInt);
        uint32_t midSize = (sizeAsInt >> 16) & 0xFFFFFFFF;
        uint8_t flagByte = extractFlagByte(sizeAsInt);
        uint8_t flagByte2 = extractSecondFlagByte(sizeAsInt);
        
        std::cout << "[SizeBitfields] Highest byte: 0x" << std::hex << static_cast<int>(flagByte) 
                  << ", Second byte: 0x" << static_cast<int>(flagByte2) << std::dec
                  << ", Low16: " << lowSize 
                  << ", Low32: " << lowSize32
                  << ", Mid32: " << midSize << std::endl;
    }

    uint16_t IBKRDecoder::extractLowSize(uint64_t encodedValue) {
        return encodedValue & 0xFFFF;  // Lowest 16 bits
    }

    uint32_t IBKRDecoder::extractLowSize32(uint64_t encodedValue) {
        return encodedValue & 0xFFFFFFFF;  // Lowest 32 bits
    }

    uint8_t IBKRDecoder::extractFlagByte(uint64_t encodedValue) {
        return (encodedValue >> 56) & 0xFF;  // Highest byte
    }

    uint8_t IBKRDecoder::extractSecondFlagByte(uint64_t encodedValue) {
        return (encodedValue >> 48) & 0xFF;  // Second highest byte
    }

    bool IBKRDecoder::isDelayedDataFlag(uint8_t flagByte) {
        // 0x31 and 0x32 appear to indicate delayed data
        return (flagByte == 0x31 || flagByte == 0x32);
    }

    double IBKRDecoder::decodeBidAskSize(uint64_t encodedValue) {
        // Extract the low 16 bits which might contain the size
        uint16_t lowSize = extractLowSize(encodedValue);
        
        // Check if there's a reasonable size in the low 16 bits
        if (lowSize > 0 && lowSize < 10000) {
            std::cout << "[SizeDecoder] Found size in low 16 bits: " << lowSize << std::endl;
            return static_cast<double>(lowSize);
        }
        
        // Try shifted position (sometimes size is in a different bit position)
        uint32_t shiftedSize = (encodedValue >> 8) & 0xFFFF;
        if (shiftedSize > 0 && shiftedSize < 10000) {
            std::cout << "[SizeDecoder] Found size in shifted position: " << shiftedSize << std::endl;
            return static_cast<double>(shiftedSize);
        }
        
        // Check flag byte for delayed data indication
        uint8_t flagByte = extractFlagByte(encodedValue);
        if (isDelayedDataFlag(flagByte)) {
            std::cout << "[SizeDecoder] Identified flag 0x" << std::hex 
                      << static_cast<int>(flagByte) << std::dec
                      << " as standard lot" << std::endl;
            return DEFAULT_LOT_SIZE;  // Default to standard lot (100 shares)
        }
        
        // Default fallback
        return DEFAULT_LOT_SIZE;
    }

    double IBKRDecoder::decodeLastSize(uint64_t encodedValue) {
        // Similar to bid/ask size but with some differences
        uint16_t lowSize = extractLowSize(encodedValue);
        
        if (lowSize > 0 && lowSize < 10000) {
            std::cout << "[LastSizeDecoder] Found size in low 16 bits: " << lowSize << std::endl;
            return static_cast<double>(lowSize);
        }
        
        // Check flag byte for delayed data indication
        uint8_t flagByte = extractFlagByte(encodedValue);
        if (isDelayedDataFlag(flagByte)) {
            return DEFAULT_LOT_SIZE;
        }
        
        return DEFAULT_LOT_SIZE;
    }

    double IBKRDecoder::decodeVolume(uint64_t encodedValue) {
        // For volume, we've observed the size in the low 32 bits
        uint32_t lowSize32 = extractLowSize32(encodedValue);
        
        if (lowSize32 > 0 && lowSize32 < 1000000000) {  // Sanity check for reasonable volume
            std::cout << "[VolumeDecoder] Found volume in low 32 bits: " << lowSize32 << std::endl;
            return static_cast<double>(lowSize32);
        }
        
        return 0.0;  // Default to zero volume if we can't decode
    }

    double IBKRDecoder::interpretSizeValue(double size, int tickType) {
        // Convert to unsigned long long to work with the bits
        uint64_t sizeAsInt = static_cast<uint64_t>(size);
        
        // Log detailed info for debugging
        std::cout << "[SizeInterpreter] Decoding special size value: " << std::scientific
                  << size << std::fixed << " for tick type: " << tickType << std::endl;
        
        std::cout << "[SizeHexDump] Value: 0x" << formatHexDump(sizeAsInt) << std::endl;
        
        // Log the component breakdown
        logSizeValueComponents(sizeAsInt, tickType);
        
        // Based on tick type, use the appropriate decoder
        if (tickType == TickType::VOLUME || tickType == TickType::DELAYED_VOLUME) {
            return decodeVolume(sizeAsInt);
        }
        else if (tickType == TickType::BID_SIZE || tickType == TickType::ASK_SIZE ||
                 tickType == TickType::DELAYED_BID_SIZE || tickType == TickType::DELAYED_ASK_SIZE) {
            return decodeBidAskSize(sizeAsInt);
        }
        else if (tickType == TickType::LAST_SIZE || tickType == TickType::DELAYED_LAST_SIZE) {
            return decodeLastSize(sizeAsInt);
        }
        
        // For unknown types, default to standard lot size
        std::cout << "[SizeDecoder] Using default size 100 for tick type: " << tickType << std::endl;
        return DEFAULT_LOT_SIZE;
    }

} // namespace ibkr_decoder
