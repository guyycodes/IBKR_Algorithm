// frame_analyzer.cpp - Lean analyzer for IBKR volume/VWAP extraction
//
// Purpose: Extract actual volume and VWAP data from tick string field 48
// Format: price;size;timestamp;volume;vwap;flag

#include "frame_analyzer.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>

namespace ibkr_frame_analyzer {

    void FrameAnalyzer::analyzeTickString48(const std::string& tickStringValue) {
        std::cout << "[FrameAnalyzer] Analyzing tick string field 48 for volume/VWAP..." << std::endl;
        std::cout << "[FrameAnalyzer] Raw value: " << tickStringValue << std::endl;
        
        // Parse the semicolon-delimited fields
        std::vector<std::string> fields = splitString(tickStringValue, ';');
        
        if (fields.size() >= 6) {
            try {
                std::string price = fields[0];
                std::string size = fields[1]; 
                std::string timestamp = fields[2];
                std::string volume = fields[3];  // This is our target!
                std::string vwap = fields[4];    // And this!
                std::string flag = fields[5];
                
                // Convert volume and VWAP to doubles for validation
                double volumeValue = std::stod(volume);
                double vwapValue = std::stod(vwap);
                
                // Format volume in millions for clarity (since this is total market volume)
                double volumeInMillions = volumeValue / 10000.0;
                
                std::cout << "[FrameAnalyzer] ===== Total Market VOLUME DATA EXTRACTED =====" << std::endl;
                std::cout << "[FrameAnalyzer] Volume (Total Market): " << std::fixed << std::setprecision(2) << volumeInMillions << "M shares" << std::endl;
                std::cout << "[FrameAnalyzer] VWAP: " << vwap << std::endl;
                std::cout << "[FrameAnalyzer] Price: " << price << std::endl;
                std::cout << "[FrameAnalyzer] Size: " << size << std::endl;
                std::cout << "[FrameAnalyzer] Timestamp: " << timestamp << std::endl;
                std::cout << "[FrameAnalyzer] Flag: " << flag << std::endl;
                std::cout << "[FrameAnalyzer] ============================" << std::endl;
                
                std::cout << "[FrameAnalyzer] CONFIRMED - Total Market Volume: " << std::fixed << std::setprecision(2) << volumeInMillions << "M shares" << std::endl;
                std::cout << "[FrameAnalyzer] CONFIRMED - VWAP: $" << std::fixed << std::setprecision(5) << vwapValue << std::endl;
                
            } catch (const std::exception& e) {
                std::cout << "[FrameAnalyzer] ERROR parsing tick string fields: " << e.what() << std::endl;
            }
        } else {
            std::cout << "[FrameAnalyzer] WARNING: Expected 6 fields but got " << fields.size() << std::endl;
            for (size_t i = 0; i < fields.size(); ++i) {
                std::cout << "[FrameAnalyzer] Field " << i << ": " << fields[i] << std::endl;
            }
        }
    }

    void FrameAnalyzer::analyzeRawFrame(const char* frameData, size_t frameLength) {
        std::cout << "[FrameAnalyzer] Analyzing raw frame (" << frameLength << " bytes)" << std::endl;
        
        if (frameLength < 4) {
            std::cout << "[FrameAnalyzer] Frame too short" << std::endl;
            return;
        }
        
        const char* ptr = frameData;
        const char* endPtr = frameData + frameLength;
        
        while (ptr < endPtr - 4) {
            // Read message length (4 bytes, big-endian)
            uint32_t msgLength = 0;
            msgLength = (static_cast<uint8_t>(ptr[0]) << 24) |
                       (static_cast<uint8_t>(ptr[1]) << 16) |
                       (static_cast<uint8_t>(ptr[2]) << 8) |
                       static_cast<uint8_t>(ptr[3]);
            
            ptr += 4; // Skip length prefix
            
            std::cout << "[FrameAnalyzer] Message length: " << msgLength << " bytes" << std::endl;
            
            if (ptr + msgLength > endPtr) {
                std::cout << "[FrameAnalyzer] Invalid message length, breaking" << std::endl;
                break;
            }
            
            // Print message content as hex and try to identify field structure
            std::cout << "[FrameAnalyzer] Message hex: ";
            for (uint32_t i = 0; i < msgLength && i < 50; ++i) { // Limit output
                printf("%02X ", static_cast<uint8_t>(ptr[i]));
            }
            if (msgLength > 50) std::cout << "...";
            std::cout << std::endl;
            
            // Try to parse as null-delimited fields
            std::vector<std::string> fields = extractFieldsFromMessage(ptr, msgLength);
            std::cout << "[FrameAnalyzer] Fields in message:" << std::endl;
            for (size_t i = 0; i < fields.size(); ++i) {
                std::cout << "[FrameAnalyzer]   Field " << i << ": '" << fields[i] << "'" << std::endl;
            }
            
            ptr += msgLength; // Move to next message
            std::cout << "[FrameAnalyzer] ---" << std::endl;
        }
    }

    std::vector<std::string> FrameAnalyzer::splitString(const std::string& str, char delimiter) {
        std::vector<std::string> fields;
        std::stringstream ss(str);
        std::string field;
        
        while (std::getline(ss, field, delimiter)) {
            fields.push_back(field);
        }
        
        return fields;
    }

    std::vector<std::string> FrameAnalyzer::extractFieldsFromMessage(const char* msgData, size_t msgLength) {
        std::vector<std::string> fields;
        const char* ptr = msgData;
        const char* endPtr = msgData + msgLength;
        
        while (ptr < endPtr) {
            const char* fieldEnd = static_cast<const char*>(std::memchr(ptr, 0, endPtr - ptr));
            if (!fieldEnd) {
                // No null terminator found, treat rest as final field
                if (ptr < endPtr) {
                    fields.emplace_back(ptr, endPtr - ptr);
                }
                break;
            }
            
            fields.emplace_back(ptr, fieldEnd - ptr);
            ptr = fieldEnd + 1; // Skip null terminator
        }
        
        return fields;
    }

    void FrameAnalyzer::analyzeTickByTickData(int reqId, int tickType, time_t time, double price, 
                                            uint64_t rawDecimal, double actualVolume,
                                            const std::string& exchange, const std::string& specialConditions,
                                            bool pastLimit, bool unreported) {
        bool isBlock = (actualVolume >= 10000.00) 
               || (specialConditions.find('B') != std::string::npos);

        // Print ALL raw data we're receiving
        std::cout << "\n========== RAW TICK-BY-TICK DATA FRAME ==========" << std::endl;
        std::cout << "reqId: " << reqId << std::endl;
        std::cout << "tickType: " << tickType << std::endl;
        std::cout << "time: " << time << std::endl;
        std::cout << "price: " << price << std::endl;
        std::cout << "size (raw Decimal): " << rawDecimal << std::endl;
        std::cout << "size (as double): " << static_cast<double>(rawDecimal) << std::endl;
        std::cout << "exchange: '" << exchange << "'" << std::endl;
        std::cout << "specialConditions: '" << specialConditions << "'" << std::endl;

        // DECODE the BID64 Decimal to get actual volume
        std::cout << "\n====== DECODED VOLUME ======" << std::endl;
        std::cout << "Raw Decimal (BID64): " << rawDecimal << std::endl;
        std::cout << "Decoded volume: " << actualVolume << std::endl;
        std::cout << "============================" << std::endl;
        
        // Print raw bytes of the size value
        double rawVolume = static_cast<double>(rawDecimal);
        uint64_t rawBits = *reinterpret_cast<uint64_t*>(&rawVolume);
        std::cout << "\n====== RAW VOLUME BYTES DEBUG ======" << std::endl;
        std::cout << "Raw volume as double: " << rawVolume << std::endl;
        std::cout << "Raw bits (hex): 0x" << std::hex << rawBits << std::dec << std::endl;
        std::cout << "Raw bytes (little-endian):" << std::endl;
        for (int i = 0; i < 8; i++) {
            uint8_t byte = (rawBits >> (i * 8)) & 0xFF;
            std::cout << "  Byte " << i << ": 0x" << std::hex << (int)byte << std::dec << " (" << (int)byte << ")" << std::endl;
        }
        std::cout << "Raw bytes (big-endian):" << std::endl;
        for (int i = 7; i >= 0; i--) {
            uint8_t byte = (rawBits >> (i * 8)) & 0xFF;
            std::cout << "  Byte " << (7-i) << ": 0x" << std::hex << (int)byte << std::dec << " (" << (int)byte << ")" << std::endl;
        }
        std::cout << "====================================" << std::endl;
        
        // Print tick attributes
        std::cout << "TickAttribLast attributes:" << std::endl;
        std::cout << "  pastLimit: " << (pastLimit ? "true" : "false") << std::endl;
        std::cout << "  unreported: " << (unreported ? "true" : "false") << std::endl;
        if (isBlock) {
            std::cout << "[BLOCK TRADE] " 
                    << actualVolume << " shares @ $" << price 
                    << " Conditions: " << specialConditions 
                    << std::endl;
        }
        std::cout << "=================================================" << std::endl;
        
        // Simple logging for now - just show what we got
        std::cout << "[TRADE] ID:" << reqId 
                  << " Time:" << time
                  << " Price:" << price
                  << " RAW_VOLUME:" << rawVolume
                  << " ACTUAL_VOLUME:" << actualVolume
                  << " Exchange:" << exchange
                  << " Conditions:" << specialConditions << std::endl;
                           
        // Highlight this is individual trade volume, not cumulative
        std::cout << "[INDIVIDUAL TRADE VOLUME] " << actualVolume 
                  << " shares traded at $" << price << std::endl;
        std::cout << "=================================================" << std::endl;
    }

} // namespace ibkr_frame_analyzer 