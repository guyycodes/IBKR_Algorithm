// frame_analyzer.cpp - Lean analyzer for IBKR volume/VWAP extraction
//
// Purpose: Extract actual volume and VWAP data from tick string field 48
// Format: price;size;timestamp;volume;vwap;flag

#include "frame_analyzer.hpp"
#include "decoder.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>

namespace ibkr_frame_analyzer {
    
    FrameAnalyzer::FrameAnalyzer(ibkr_decoder::IBKRDecoder& decoder) : m_decoder(&decoder) {

    }

    TickStringResult FrameAnalyzer::analyzeTickStringData(int tickerId, int field, const std::string& value) {
        // Route to specific field handlers based on field type
        switch (field) {
            case 48:
                return processField48(value);
            case 88:
                return processField88(value);
            default:
                return processGenericField(field, value);
        }
    }

    TickStringResult FrameAnalyzer::processField48(const std::string& value) {
        TickStringResult result;
        
        // std::cout << "[Field48] Processing volume/VWAP data: " << value << std::endl;
        
        // Parse the semicolon-delimited fields
        std::vector<std::string> fields = splitString(value, ';');
        
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
                
                result.hasDecodedData = true;
                result.dataType = "VOLUME+VWAP";
                result.decodedValue = "Vol: " + std::to_string(volumeValue/10000.0) + "M, VWAP: $" + std::to_string(vwapValue);
                result.numericValue = vwapValue;
                result.volume = volumeValue / 10000.0;  // Convert to millions and store
                result.vwap = vwapValue;                // Store VWAP directly
                 
                // std::cout << "[Field48] Decoded - Volume: " << std::fixed << std::setprecision(2) 
                //           << volumeValue/10000.0 << "M shares, VWAP: $" << std::fixed << std::setprecision(5) << vwapValue << std::endl;
                
            } catch (const std::exception& e) {
                std::cout << "[Field48] ERROR parsing: " << e.what() << std::endl;
                result.hasDecodedData = false;
            }
        } else {
            std::cout << "[Field48] WARNING: Expected 6 fields but got " << fields.size() << std::endl;
            result.hasDecodedData = false;
        }
        
        return result;
    }

    TickStringResult FrameAnalyzer::processField88(const std::string& value) {
        TickStringResult result;
        
        try {
            // Convert string to timestamp (Unix epoch)
            uint64_t timestamp = std::stoull(value);
            
            // Create a readable time format
            time_t time_t_timestamp = static_cast<time_t>(timestamp);
            char timeStr[30];
            struct tm timeinfo;
            
            #ifdef _WIN32
            localtime_s(&timeinfo, &time_t_timestamp);
            #else
            localtime_r(&time_t_timestamp, &timeinfo);
            #endif
            
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            
            result.hasDecodedData = true;
            result.dataType = "TIMESTAMP";
            result.decodedValue = std::string(timeStr);
            result.timestamp = timestamp * 1000; // Convert to ms
            result.numericValue = static_cast<double>(timestamp);
            
            // std::cout << "[Field88] Decoded timestamp: " << timeStr << " (epoch: " << timestamp << ")" << std::endl;
            
        } catch (const std::exception& e) {
            std::cout << "[Field88] ERROR parsing timestamp: " << e.what() << std::endl;
            result.hasDecodedData = false;
        }
        
        return result;
    }

    AnalyzedBarData FrameAnalyzer::analyzeRealtimeBarData(int reqId, int time, double open, double high, double low, 
                                          double close, double volume, double wap, int count) {
        AnalyzedBarData analyzed;
        
        // Basic data
        analyzed.reqId = reqId;
        analyzed.epochTime = time;
        analyzed.open = open;
        analyzed.high = high;
        analyzed.low = low;
        analyzed.close = close;
        analyzed.volume = volume;
        analyzed.wap = wap;
        analyzed.count = count;
        
        // Format time as human-readable
        time_t epochTime = time;
        char timeStr[30];
        struct tm* timeinfo = localtime(&epochTime);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        analyzed.formattedTime = std::string(timeStr);
        
        // Calculate price change for the bar
        if (open > 0) {
            analyzed.priceChange = close - open;
            analyzed.percentChange = (analyzed.priceChange / open) * 100.0;
            analyzed.hasValidPriceChange = true;
        } else {
            analyzed.priceChange = 0.0;
            analyzed.percentChange = 0.0;
            analyzed.hasValidPriceChange = false;
        }
        
        // Calculate bar range
        if (high > 0 && low > 0) {
            analyzed.barRange = high - low;
            analyzed.hasValidRange = true;
        } else {
            analyzed.barRange = 0.0;
            analyzed.hasValidRange = false;
        }
        
        return analyzed;
    }
//helper functions
    std::vector<std::string> FrameAnalyzer::splitString(const std::string& str, char delimiter) {
        std::vector<std::string> fields;
        std::stringstream ss(str);
        std::string field;
        
        while (std::getline(ss, field, delimiter)) {
            fields.push_back(field);
        }
        
        return fields;
    }
//helper function to extract fields from message
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

    AnalyzedTickByTickData FrameAnalyzer::analyzeTickByTickData(int reqId, int tickType, time_t time, double price, 
                                                               double size, const std::string& exchange, 
                                                               const std::string& specialConditions, 
                                                               bool pastLimit, bool unreported) {
        AnalyzedTickByTickData analyzed;
        
        // Basic data
        analyzed.reqId = reqId;
        analyzed.tickType = tickType;
        analyzed.epochTime = static_cast<uint64_t>(time);
        analyzed.price = price;
        analyzed.volume = size;  // Already decoded from Decimal to double
        analyzed.exchange = exchange;
        analyzed.specialConditions = specialConditions;
        analyzed.pastLimit = pastLimit;
        analyzed.unreported = unreported;
        
        // Format time as human-readable
        char timeStr[30];
        struct tm* timeinfo = localtime(&time);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        analyzed.formattedTime = std::string(timeStr);
        
        // Calculate derived metrics
        if (price > 0 && size > 0) {
            analyzed.dollarsTraded = price * size;
            analyzed.hasValidTrade = true;
        } else {
            analyzed.dollarsTraded = 0.0;
            analyzed.hasValidTrade = false;
        }
        
        return analyzed;
    }

    AnalyzedTickByTickBidAskData FrameAnalyzer::analyzeTickByTickBidAskData(int reqId, time_t time, double bidPrice, double askPrice,
                                                                           double bidSize, double askSize, 
                                                                           bool bidPastLow, bool askPastHigh) {
        AnalyzedTickByTickBidAskData analyzed;
        
        // Basic data
        analyzed.reqId = reqId;
        analyzed.epochTime = static_cast<uint64_t>(time);
        analyzed.bidPrice = bidPrice;
        analyzed.askPrice = askPrice;
        analyzed.bidSize = bidSize;  // Already decoded from Decimal to double
        analyzed.askSize = askSize;  // Already decoded from Decimal to double
        analyzed.bidPastLow = bidPastLow;
        analyzed.askPastHigh = askPastHigh;
        
        // Format time as human-readable
        char timeStr[30];
        struct tm* timeinfo = localtime(&time);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        analyzed.formattedTime = std::string(timeStr);
        
        // Calculate derived metrics
        if (bidPrice > 0 && askPrice > 0) {
            analyzed.spread = askPrice - bidPrice;
            analyzed.midPoint = (bidPrice + askPrice) / 2.0;
            analyzed.spreadPercent = (analyzed.spread / analyzed.midPoint) * 100.0;
            analyzed.hasValidSpread = true;
            analyzed.hasValidMidPoint = true;
        } else {
            analyzed.spread = 0.0;
            analyzed.spreadPercent = 0.0;
            analyzed.midPoint = 0.0;
            analyzed.hasValidSpread = false;
            analyzed.hasValidMidPoint = false;
        }
        
        return analyzed;
    }

} // namespace ibkr_frame_analyzer 