// Decoder.cpp
//
// This file contains the implementation of the Decoder class, which is responsible for decoding
// the incoming data from the IBKR API.

#include "decoder.hpp"
#include "frame_analyzer.hpp"
#include <iostream>
#include <algorithm>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <bitset>

namespace ibkr_decoder {

    // Constructor implementation
    IBKRDecoder::IBKRDecoder(ibkr_frame_analyzer::FrameAnalyzer& frameAnalyzer) 
        : m_frameAnalyzer(frameAnalyzer) {
    }

    // ============ LEGACY CUSTOM DECODING METHODS ============
    // These maintain compatibility with existing connection code
    
    bool IBKRDecoder::isSpecialSizeValue(double size) {
        // Check if size is unrealistically large (IBKR encodes flags in large values)
        // The common pattern we've observed is values around 3.58e+18
        return (size > MINIMUM_SPECIAL_SIZE);
    }
    
    double IBKRDecoder::interpretSizeValue(double size, int tickType) {
        // Simple implementation: if it's a special size value, return 0
        // Otherwise return the size as-is
        if (isSpecialSizeValue(size)) {
            return 0.0; // Special values are treated as no data
        }
        return size;
    }
    
    // ALL LEGACY METHODS REMOVED, FROM NOW ON MUST USE IBKR METHODS FROM THEIR API DECODER

    // ============ NEW IBKR-COMPLIANT DECODING METHODS ============
    // These implement the official IBKR decoding approach

    bool IBKRDecoder::checkOffset(const char* ptr, const char* endPtr) {
        assert(ptr && ptr <= endPtr);
        return (ptr && ptr < endPtr);
    }

    const char* IBKRDecoder::findFieldEnd(const char* ptr, const char* endPtr) {
        return static_cast<const char*>(std::memchr(ptr, 0, endPtr - ptr));
    }

    bool IBKRDecoder::decodeField(bool& boolValue, const char*& ptr, const char* endPtr) {
        int intValue;
        if (!decodeField(intValue, ptr, endPtr))
            return false;
        boolValue = (intValue > 0);
        return true;
    }

    bool IBKRDecoder::decodeField(int& intValue, const char*& ptr, const char* endPtr) {
        if (!checkOffset(ptr, endPtr))
            return false;
        const char* fieldBeg = ptr;
        const char* fieldEnd = findFieldEnd(fieldBeg, endPtr);
        if (!fieldEnd)
            return false;
        intValue = std::atoi(fieldBeg);
        ptr = ++fieldEnd;
        return true;
    }

    bool IBKRDecoder::decodeField(long& longValue, const char*& ptr, const char* endPtr) {
        if (!checkOffset(ptr, endPtr))
            return false;
        const char* fieldBeg = ptr;
        const char* fieldEnd = findFieldEnd(fieldBeg, endPtr);
        if (!fieldEnd)
            return false;
        longValue = std::atol(fieldBeg);
        ptr = ++fieldEnd;
        return true;
    }

    bool IBKRDecoder::decodeField(long long& longLongValue, const char*& ptr, const char* endPtr) {
        if (!checkOffset(ptr, endPtr))
            return false;
        const char* fieldBeg = ptr;
        const char* fieldEnd = findFieldEnd(fieldBeg, endPtr);
        if (!fieldEnd)
            return false;
        longLongValue = std::atoll(fieldBeg);
        ptr = ++fieldEnd;
        return true;
    }

    bool IBKRDecoder::decodeField(double& doubleValue, const char*& ptr, const char* endPtr) {
        if (!checkOffset(ptr, endPtr))
            return false;
        const char* fieldBeg = ptr;
        const char* fieldEnd = findFieldEnd(fieldBeg, endPtr);
        if (!fieldEnd)
            return false;
        
        // Check for infinity
        if (std::strcmp(fieldBeg, INFINITY_STR) == 0) {
            doubleValue = INFINITY;
        } else {
            doubleValue = std::atof(fieldBeg);
        }
        ptr = ++fieldEnd;
        return true;
    }

    bool IBKRDecoder::decodeField(std::string& stringValue, const char*& ptr, const char* endPtr) {
        if (!checkOffset(ptr, endPtr))
            return false;
        const char* fieldBeg = ptr;
        const char* fieldEnd = findFieldEnd(ptr, endPtr);
        if (!fieldEnd)
            return false;
        stringValue = fieldBeg;
        ptr = ++fieldEnd;
        return true;
    }

    bool IBKRDecoder::decodeField(Decimal& decimalValue, const char*& ptr, const char* endPtr) {
        if (!checkOffset(ptr, endPtr))
            return false;
        const char* fieldBeg = ptr;
        const char* fieldEnd = findFieldEnd(fieldBeg, endPtr);
        if (!fieldEnd)
            return false;
        decimalValue = DecimalFunctions::stringToDecimal(fieldBeg);
        ptr = ++fieldEnd;
        return true;
    }

    bool IBKRDecoder::decodeFieldMax(int& intValue, const char*& ptr, const char* endPtr) {
        std::string stringValue;
        if (!decodeField(stringValue, ptr, endPtr))
            return false;
        intValue = stringValue.empty() ? DECODER_UNSET_INTEGER : std::atoi(stringValue.c_str());
        return true;
    }

    bool IBKRDecoder::decodeFieldMax(double& doubleValue, const char*& ptr, const char* endPtr) {
        std::string stringValue;
        if (!decodeField(stringValue, ptr, endPtr))
            return false;
        doubleValue = stringValue.empty() ? DECODER_UNSET_DOUBLE : std::atof(stringValue.c_str());
        return true;
    }

    bool IBKRDecoder::processTickPriceMessage(const char*& ptr, const char* endPtr, TickPriceData& data, int serverVersion) {
        int version;
        int attrMask;

        // Decode fields according to IBKR specification
        if (!decodeField(version, ptr, endPtr)) return false;
        if (!decodeField(data.tickerId, ptr, endPtr)) return false;
        if (!decodeField(data.tickType, ptr, endPtr)) return false;
        if (!decodeField(data.price, ptr, endPtr)) return false;
        if (!decodeField(data.size, ptr, endPtr)) return false;
        if (!decodeField(attrMask, ptr, endPtr)) return false;

        // Parse attribute mask
        data.canAutoExecute = attrMask == 1;
        data.pastLimit = false;
        data.preOpen = false;

        if (serverVersion >= 38) { // MIN_SERVER_VER_PAST_LIMIT
            std::bitset<32> mask(attrMask);
            data.canAutoExecute = mask[0];
            data.pastLimit = mask[1];

            if (serverVersion >= 72) { // MIN_SERVER_VER_PRE_OPEN_BID_ASK
                data.preOpen = mask[2];
            }
        }

        std::cout << "[IBKRDecoder] Processed tick price message: "
                  << "TickerId=" << data.tickerId 
                  << ", TickType=" << data.tickType 
                  << ", Price=" << data.price 
                  << ", Size=" << DecimalFunctions::decimalStringToDisplay(data.size) 
                  << std::endl;

        return true;
    }

    bool IBKRDecoder::processTickSizeMessage(const char*& ptr, const char* endPtr, TickSizeData& data) {
        int version;

        // Decode fields according to IBKR specification
        if (!decodeField(version, ptr, endPtr)) return false;
        if (!decodeField(data.tickerId, ptr, endPtr)) return false;
        if (!decodeField(data.tickType, ptr, endPtr)) return false;
        if (!decodeField(data.size, ptr, endPtr)) return false;

        std::cout << "[IBKRDecoder] Processed tick size message: "
                  << "TickerId=" << data.tickerId 
                  << ", TickType=" << data.tickType 
                  << ", Size=" << DecimalFunctions::decimalStringToDisplay(data.size) 
                  << std::endl;

        return true;
    }

    bool IBKRDecoder::processRealtimeBarMessage(const char*& ptr, const char* endPtr, RealtimeBarData& data) {
        // Decode fields according to official IBKR specification
        if (!decodeField(data.version, ptr, endPtr)) return false;
        if (!decodeField(data.reqId, ptr, endPtr)) return false;
        if (!decodeField(data.time, ptr, endPtr)) return false;
        if (!decodeField(data.open, ptr, endPtr)) return false;
        if (!decodeField(data.high, ptr, endPtr)) return false;
        if (!decodeField(data.low, ptr, endPtr)) return false;
        if (!decodeField(data.close, ptr, endPtr)) return false;
        if (!decodeField(data.volume, ptr, endPtr)) return false;
        if (!decodeField(data.wap, ptr, endPtr)) return false;
        if (!decodeField(data.count, ptr, endPtr)) return false;

        std::cout << "[IBKRDecoder] Processed realtime bar message: "
                  << "ReqId=" << data.reqId 
                  << ", Time=" << data.time 
                  << ", OHLC=" << data.open << "/" << data.high << "/" << data.low << "/" << data.close
                  << ", Volume=" << DecimalFunctions::decimalStringToDisplay(data.volume)
                  << ", WAP=" << DecimalFunctions::decimalStringToDisplay(data.wap)
                  << ", Count=" << data.count
                  << std::endl;

        return true;
    }

    bool IBKRDecoder::processTickByTickMessage(const char*& ptr, const char* endPtr, TickByTickData& data) {
        // Decode fields according to official IBKR specification for tick-by-tick messages
        if (!decodeField(data.reqId, ptr, endPtr)) return false;
        if (!decodeField(data.tickType, ptr, endPtr)) return false;
        if (!decodeField(data.time, ptr, endPtr)) return false;

        // Process based on tick type (1 = Last, 2 = AllLast)
        if (data.tickType == 1 || data.tickType == 2) { // Last/AllLast
            if (!decodeField(data.price, ptr, endPtr)) return false;
            if (!decodeField(data.size, ptr, endPtr)) return false;
            if (!decodeField(data.attrMask, ptr, endPtr)) return false;

            // Parse attribute mask using official IBKR bitset approach
            std::bitset<32> mask(data.attrMask);
            data.pastLimit = mask[0];
            data.unreported = mask[1];

            if (!decodeField(data.exchange, ptr, endPtr)) return false;
            if (!decodeField(data.specialConditions, ptr, endPtr)) return false;

            std::cout << "[IBKRDecoder] Processed tick-by-tick message: "
                      << "ReqId=" << data.reqId 
                      << ", TickType=" << data.tickType 
                      << ", Time=" << data.time
                      << ", Price=" << data.price 
                      << ", Size=" << DecimalFunctions::decimalStringToDisplay(data.size)
                      << ", Exchange=" << data.exchange
                      << ", PastLimit=" << data.pastLimit
                      << ", Unreported=" << data.unreported
                      << std::endl;

            return true;
        }

        // For other tick types (BidAsk, MidPoint), we don't process them in this method
        // as tickByTickAllLast only handles Last/AllLast types
        return false;
    }

    void IBKRDecoder::analyzeRawMessage(const char* msgData, size_t msgLength) {
        std::cout << "[IBKRDecoder] Raw message analysis (" << msgLength << " bytes):" << std::endl;
        
        // Print hex dump
        std::cout << "Hex dump: ";
        for (size_t i = 0; i < msgLength; ++i) {
            printf("%02X ", static_cast<unsigned char>(msgData[i]));
            if ((i + 1) % 16 == 0) std::cout << std::endl << "          ";
        }
        std::cout << std::endl;

        // Extract and print fields
        auto fields = extractFields(msgData, msgLength);
        std::cout << "Fields (" << fields.size() << "):" << std::endl;
        for (size_t i = 0; i < fields.size(); ++i) {
            std::cout << "  Field " << i << ": '" << fields[i] << "'" << std::endl;
        }
    }

    std::vector<std::string> IBKRDecoder::extractFields(const char* msgData, size_t msgLength) {
        std::vector<std::string> fields;
        const char* ptr = msgData;
        const char* endPtr = msgData + msgLength;

        while (ptr < endPtr) {
            const char* fieldEnd = findFieldEnd(ptr, endPtr);
            if (!fieldEnd) {
                // No null terminator found, treat rest as final field
                fields.emplace_back(ptr, endPtr - ptr);
                break;
            }
            
            fields.emplace_back(ptr, fieldEnd - ptr);
            ptr = fieldEnd + 1; // Skip null terminator
        }

        return fields;
    }

    double IBKRDecoder::decimalToDouble(Decimal decimal) {
        return DecimalFunctions::decimalToDouble(decimal);
    }

    Decimal IBKRDecoder::doubleToDecimal(double value) {
        return DecimalFunctions::doubleToDecimal(value);
    }

    void IBKRDecoder::analyzeTickStringMessage(int tickerId, int field, const std::string& value) {
        std::cout << "[IBKRDecoder] Tick string analysis - ID: " << tickerId 
                  << ", Field: " << field << std::endl;
        
        // Focus specifically on field 48 which contains volume and VWAP data
        if (field == 48) {
            std::cout << "[IBKRDecoder] *** FIELD 48 DETECTED - CONTAINS VOLUME/VWAP DATA ***" << std::endl;
            // method not implemented yet
            // m_frameAnalyzer.analyzeTickString48(value);
        } else {
            std::cout << "[IBKRDecoder] Field " << field << " value: " << value << std::endl;
        }
    }

    double IBKRDecoder::decodeTradeVolume(Decimal size) {
        // Use IBKR's official BID64 decimal decoder to get actual volume
        return DecimalFunctions::decimalToDouble(size);
    }

} // namespace ibkr_decoder
