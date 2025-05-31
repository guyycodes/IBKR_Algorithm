// // old_frame_analyzer.cpp - Lean analyzer for IBKR volume/VWAP extraction
// //
// // Purpose: Extract actual volume and VWAP data from tick string field 48
// // Format: price;size;timestamp;volume;vwap;flag

// #include "frame_analyzer.hpp"
// #include "decoder.hpp"  // Include decoder for IBKR wrapper methods
// #include <iostream>
// #include <sstream>
// #include <vector>
// #include <string>
// #include <iomanip>

// namespace ibkr_frame_analyzer {

//     FrameAnalyzer::FrameAnalyzer(ibkr_decoder::IBKRDecoder& decoder) : m_decoder(&decoder) {
//         // Constructor with decoder reference
//     }

//     TickStringResult FrameAnalyzer::analyzeTickStringData(int tickerId, int field, const std::string& value) {
//         // Route to specific field handlers based on field type
//         switch (field) {
//             case 48:
//                 return processField48(value);
//             case 88:
//                 return processField88(value);
//             default:
//                 return processGenericField(field, value);
//         }
//     }

//     TickStringResult FrameAnalyzer::processField48(const std::string& value) {
//         TickStringResult result;
        
//         // std::cout << "[Field48] Processing volume/VWAP data: " << value << std::endl;
        
//         // Parse the semicolon-delimited fields
//         std::vector<std::string> fields = splitString(value, ';');
        
//         if (fields.size() >= 6) {
//             try {
//                 std::string price = fields[0];
//                 std::string size = fields[1]; 
//                 std::string timestamp = fields[2];
//                 std::string volume = fields[3];  // This is our target!
//                 std::string vwap = fields[4];    // And this!
//                 std::string flag = fields[5];
                
//                 // Convert volume and VWAP to doubles for validation
//                 double volumeValue = std::stod(volume);
//                 double vwapValue = std::stod(vwap);
                
//                 result.hasDecodedData = true;
//                 result.dataType = "VOLUME+VWAP";
//                 result.decodedValue = "Vol: " + std::to_string(volumeValue/10000.0) + "M, VWAP: $" + std::to_string(vwapValue);
//                 result.numericValue = vwapValue;
//                 result.volume = volumeValue / 10000.0;  // Convert to millions and store
//                 result.vwap = vwapValue;                // Store VWAP directly
                 
//                 // std::cout << "[Field48] Decoded - Volume: " << std::fixed << std::setprecision(2) 
//                 //           << volumeValue/10000.0 << "M shares, VWAP: $" << std::fixed << std::setprecision(5) << vwapValue << std::endl;
                
//             } catch (const std::exception& e) {
//                 std::cout << "[Field48] ERROR parsing: " << e.what() << std::endl;
//                 result.hasDecodedData = false;
//             }
//         } else {
//             std::cout << "[Field48] WARNING: Expected 6 fields but got " << fields.size() << std::endl;
//             result.hasDecodedData = false;
//         }
        
//         return result;
//     }

//     TickStringResult FrameAnalyzer::processField88(const std::string& value) {
//         TickStringResult result;
        
//         try {
//             // Convert string to timestamp (Unix epoch)
//             uint64_t timestamp = std::stoull(value);
            
//             // Create a readable time format
//             time_t time_t_timestamp = static_cast<time_t>(timestamp);
//             char timeStr[30];
//             struct tm timeinfo;
            
//             #ifdef _WIN32
//             localtime_s(&timeinfo, &time_t_timestamp);
//             #else
//             localtime_r(&time_t_timestamp, &timeinfo);
//             #endif
            
//             strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            
//             result.hasDecodedData = true;
//             result.dataType = "TIMESTAMP";
//             result.decodedValue = std::string(timeStr);
//             result.timestamp = timestamp * 1000; // Convert to ms
//             result.numericValue = static_cast<double>(timestamp);
            
//             // std::cout << "[Field88] Decoded timestamp: " << timeStr << " (epoch: " << timestamp << ")" << std::endl;
            
//         } catch (const std::exception& e) {
//             std::cout << "[Field88] ERROR parsing timestamp: " << e.what() << std::endl;
//             result.hasDecodedData = false;
//         }
        
//         return result;
//     }

//     TickStringResult FrameAnalyzer::processGenericField(int field, const std::string& value) {
//         TickStringResult result;
        
//         // Map field code to readable name
//         std::string fieldName;
//         switch (field) {
//             case 45: fieldName = "LAST_TIMESTAMP"; break;
//             case 46: fieldName = "LAST_REG_TIME"; break;
//             default: fieldName = "UNKNOWN_STRING_" + std::to_string(field); break;
//         }
        
//         result.hasDecodedData = true;
//         result.dataType = fieldName;
//         result.decodedValue = value;
        
//         std::cout << "[" << fieldName << "] Raw value: " << value << std::endl;
        
//         return result;
//     }

//     void FrameAnalyzer::analyzeTickString48(const std::string& tickStringValue) {
//         // This is the old implementation - kept for backward compatibility
//         // The new pattern uses processField48() instead
//         processField48(tickStringValue);
//     }

//     void FrameAnalyzer::analyzeRawFrame(const char* frameData, size_t frameLength) {
//         std::cout << "[FrameAnalyzer] Analyzing raw frame (" << frameLength << " bytes)" << std::endl;
        
//         if (frameLength < 4) {
//             std::cout << "[FrameAnalyzer] Frame too short" << std::endl;
//             return;
//         }
        
//         const char* ptr = frameData;
//         const char* endPtr = frameData + frameLength;
        
//         while (ptr < endPtr - 4) {
//             // Read message length (4 bytes, big-endian)
//             uint32_t msgLength = 0;
//             msgLength = (static_cast<uint8_t>(ptr[0]) << 24) |
//                        (static_cast<uint8_t>(ptr[1]) << 16) |
//                        (static_cast<uint8_t>(ptr[2]) << 8) |
//                        static_cast<uint8_t>(ptr[3]);
            
//             ptr += 4; // Skip length prefix
            
//             std::cout << "[FrameAnalyzer] Message length: " << msgLength << " bytes" << std::endl;
            
//             if (ptr + msgLength > endPtr) {
//                 std::cout << "[FrameAnalyzer] Invalid message length, breaking" << std::endl;
//                 break;
//             }
            
//             // Print message content as hex and try to identify field structure
//             std::cout << "[FrameAnalyzer] Message hex: ";
//             for (uint32_t i = 0; i < msgLength && i < 50; ++i) { // Limit output
//                 printf("%02X ", static_cast<uint8_t>(ptr[i]));
//             }
//             if (msgLength > 50) std::cout << "...";
//             std::cout << std::endl;
            
//             // Try to parse as null-delimited fields
//             std::vector<std::string> fields = extractFieldsFromMessage(ptr, msgLength);
//             std::cout << "[FrameAnalyzer] Fields in message:" << std::endl;
//             for (size_t i = 0; i < fields.size(); ++i) {
//                 std::cout << "[FrameAnalyzer]   Field " << i << ": '" << fields[i] << "'" << std::endl;
//             }
            
//             ptr += msgLength; // Move to next message
//             std::cout << "[FrameAnalyzer] ---" << std::endl;
//         }
//     }

//     std::vector<std::string> FrameAnalyzer::splitString(const std::string& str, char delimiter) {
//         std::vector<std::string> fields;
//         std::stringstream ss(str);
//         std::string field;
        
//         while (std::getline(ss, field, delimiter)) {
//             fields.push_back(field);
//         }
        
//         return fields;
//     }

//     std::vector<std::string> FrameAnalyzer::extractFieldsFromMessage(const char* msgData, size_t msgLength) {
//         std::vector<std::string> fields;
//         const char* ptr = msgData;
//         const char* endPtr = msgData + msgLength;
        
//         while (ptr < endPtr) {
//             const char* fieldEnd = static_cast<const char*>(std::memchr(ptr, 0, endPtr - ptr));
//             if (!fieldEnd) {
//                 // No null terminator found, treat rest as final field
//                 if (ptr < endPtr) {
//                     fields.emplace_back(ptr, endPtr - ptr);
//                 }
//                 break;
//             }
            
//             fields.emplace_back(ptr, fieldEnd - ptr);
//             ptr = fieldEnd + 1; // Skip null terminator
//         }
        
//         return fields;
//     }

//     void FrameAnalyzer::analyzeTickByTickData(int reqId, int tickType, time_t time, double price, 
//                                             uint64_t rawDecimal, double actualVolume,
//                                             const std::string& exchange, const std::string& specialConditions,
//                                             bool pastLimit, bool unreported) {
//         bool isBlock = (actualVolume >= 10000.00) 
//                || (specialConditions.find('B') != std::string::npos);

//         // Print ALL raw data we're receiving
//         std::cout << "\n========== RAW TICK-BY-TICK DATA FRAME ==========" << std::endl;
//         std::cout << "reqId: " << reqId << std::endl;
//         std::cout << "tickType: " << tickType << std::endl;
//         std::cout << "time: " << time << std::endl;
//         std::cout << "price: " << price << std::endl;
//         std::cout << "size (raw Decimal): " << rawDecimal << std::endl;
//         std::cout << "size (as double): " << static_cast<double>(rawDecimal) << std::endl;
//         std::cout << "exchange: '" << exchange << "'" << std::endl;
//         std::cout << "specialConditions: '" << specialConditions << "'" << std::endl;

//         // DECODE the BID64 Decimal to get actual volume
//         std::cout << "\n====== DECODED VOLUME ======" << std::endl;
//         std::cout << "Raw Decimal (BID64): " << rawDecimal << std::endl;
//         std::cout << "Decoded volume: " << actualVolume << std::endl;
//         std::cout << "============================" << std::endl;
        
//         // Print raw bytes of the size value
//         double rawVolume = static_cast<double>(rawDecimal);
//         uint64_t rawBits = *reinterpret_cast<uint64_t*>(&rawVolume);
//         // std::cout << "\n====== RAW VOLUME BYTES DEBUG ======" << std::endl;
//         // std::cout << "Raw volume as double: " << rawVolume << std::endl;
//         // std::cout << "Raw bits (hex): 0x" << std::hex << rawBits << std::dec << std::endl;
//         // std::cout << "Raw bytes (little-endian):" << std::endl;
//         // for (int i = 0; i < 8; i++) {
//         //     uint8_t byte = (rawBits >> (i * 8)) & 0xFF;
//         //     std::cout << "  Byte " << i << ": 0x" << std::hex << (int)byte << std::dec << " (" << (int)byte << ")" << std::endl;
//         // }
//         // std::cout << "Raw bytes (big-endian):" << std::endl;
//         // for (int i = 7; i >= 0; i--) {
//         //     uint8_t byte = (rawBits >> (i * 8)) & 0xFF;
//         //     std::cout << "  Byte " << (7-i) << ": 0x" << std::hex << (int)byte << std::dec << " (" << (int)byte << ")" << std::endl;
//         // }
//         std::cout << "====================================" << std::endl;
        
//         // Print tick attributes
//         std::cout << "TickAttribLast attributes:" << std::endl;
//         std::cout << "  pastLimit: " << (pastLimit ? "true" : "false") << std::endl;
//         std::cout << "  unreported: " << (unreported ? "true" : "false") << std::endl;
//         if (isBlock) {
//             std::cout << "[BLOCK TRADE] " 
//                     << actualVolume << " shares @ $" << price 
//                     << " Conditions: " << specialConditions 
//                     << std::endl;
//         }
//         std::cout << "=================================================" << std::endl;
        
//         // Simple logging for now - just show what we got
//         std::cout << "[TRADE] ID:" << reqId 
//                   << " Time:" << time
//                   << " Price:" << price
//                   << " RAW_VOLUME:" << rawVolume
//                   << " ACTUAL_VOLUME:" << actualVolume
//                   << " Exchange:" << exchange
//                   << " Conditions:" << specialConditions << std::endl;
                           
//         // Highlight this is individual trade volume, not cumulative
//         std::cout << "[INDIVIDUAL TRADE VOLUME] " << actualVolume 
//                   << " shares traded at $" << price << std::endl;
//         std::cout << "=================================================" << std::endl;
//     }

//     TickPriceResult FrameAnalyzer::analyzeTickPriceData(int tickerId, int field, double price, const TickAttrib& attrib) {
//         TickPriceResult result;
        
//         // Map field code to readable name - only handle live data
//         std::string fieldName;
//         switch (field) {
//             case 1: // TickType::BID
//                 fieldName = "BID";
//                 break;
//             case 2: // TickType::ASK  
//                 fieldName = "ASK";
//                 break;
//             case 4: // TickType::LAST
//                 fieldName = "LAST";
//                 break;
//             default:
//                 // Unknown field type, return empty result
//                 return result;
//         }
        
//         // Use decoder as wrapper for IBKR-style price validation
//         bool isValidPrice = (price > 0.0);
//         double processedPrice = price;
        
//         // If decoder is available, use it for additional processing
//         if (m_decoder) {
//             // Use decoder methods as wrappers for official IBKR validation
//             // This demonstrates the pattern of using decoder as a wrapper
//             processedPrice = price; // For now, direct pass-through
            
//             // Could add decoder-based validation here:
//             // isValidPrice = m_decoder->validatePriceData(price, field);
//         }
        
//         // Process TickAttrib flags (official IBKR attributes)
//         std::string attributeInfo = "";
//         if (attrib.canAutoExecute) {
//             attributeInfo += "AutoExec ";
//         }
//         if (attrib.pastLimit) {
//             attributeInfo += "PastLimit ";
//         }
//         if (attrib.preOpen) {
//             attributeInfo += "PreOpen ";
//         }
        
//         // Populate result structure
//         result.hasDecodedData = isValidPrice;
//         result.dataType = fieldName + (attributeInfo.empty() ? "" : " [" + attributeInfo + "]");
//         result.decodedPrice = processedPrice;
//         result.tickerId = tickerId;
//         result.fieldType = field;
        
//         return result;
//     }

//     TickSizeResult FrameAnalyzer::analyzeTickSizeData(int tickerId, int field, double size) {
//         TickSizeResult result;
        
//         // Map field code to readable name - focus on the key size fields
//         std::string fieldName;
//         switch (field) {
//             case 0: // TickType::BID_SIZE
//                 fieldName = "BID_SIZE";
//                 break;
//             case 3: // TickType::ASK_SIZE  
//                 fieldName = "ASK_SIZE";
//                 break;
//             case 5: // TickType::LAST_SIZE
//                 fieldName = "LAST_SIZE";
//                 break;
//             case 8: // TickType::VOLUME
//                 fieldName = "VOLUME";
//                 break;
//             // Handle delayed versions as fallback
//             case 66: // TickType::DELAYED_BID_SIZE (66 = 0 + 66)
//                 fieldName = "DELAYED_BID_SIZE";
//                 break;
//             case 69: // TickType::DELAYED_ASK_SIZE (69 = 3 + 66)
//                 fieldName = "DELAYED_ASK_SIZE";
//                 break;
//             case 71: // TickType::DELAYED_LAST_SIZE (71 = 5 + 66)
//                 fieldName = "DELAYED_LAST_SIZE";
//                 break;
//             case 74: // TickType::DELAYED_VOLUME (74 = 8 + 66)
//                 fieldName = "DELAYED_VOLUME";
//                 break;
//             default:
//                 // Unknown field type, return empty result
//                 return result;
//         }
        
//         // Use decoder as wrapper for IBKR-style size processing
//         double processedSize = size;
//         bool isSpecialValue = false;
        
//         // If decoder is available, use it for special value processing
//         if (m_decoder) {
//             // Use decoder methods as wrappers for official IBKR special value handling
//             if (m_decoder->isSpecialSizeValue(size)) {
//                 isSpecialValue = true;
//                 processedSize = m_decoder->interpretSizeValue(size, field);
//             }
//         }
        
//         // Validate that we have meaningful size data
//         bool isValidSize = (processedSize >= 0.0);
        
//         // Populate result structure
//         result.hasDecodedData = isValidSize;
//         result.dataType = fieldName + (isSpecialValue ? " [DECODED]" : "");
//         result.decodedSize = processedSize;
//         result.tickerId = tickerId;
//         result.fieldType = field;
//         result.isSpecialValue = isSpecialValue;
        
//         return result;
//     }

// } // namespace ibkr_frame_analyzer 