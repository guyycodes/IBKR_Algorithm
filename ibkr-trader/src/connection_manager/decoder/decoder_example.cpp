// // decoder_example.cpp
// //
// // Example demonstrating how to use the IBKR-compliant decoder methods
// // to parse raw market data messages according to the official IBKR protocol.

// #include "decoder.hpp"
// #include <iostream>
// #include <vector>
// #include <cstring>

// using namespace ibkr_decoder;

// void demonstrateRawMessageDecoding() {
//     std::cout << "=== IBKR Raw Message Decoding Example ===" << std::endl;
    
//     // Example raw message from IBKR support (tick price message)
//     // This represents: message_type=1, ticker_id=6, tick_type=4, price=198.28, size=100, attributes=0
//     const char rawMessage[] = "\x00\x00\x00\x151\x006\x008\x004\x00198.28\x00100\x000\x00\x00\x00\x00\x00";
//     size_t msgLength = sizeof(rawMessage) - 1; // Exclude null terminator
    
//     std::cout << "\n1. Raw Message Analysis:" << std::endl;
//     IBKRDecoder::analyzeRawMessage(rawMessage, msgLength);
    
//     // Skip the length prefix (first 4 bytes) to get to the actual message content
//     const char* msgContent = rawMessage + 4;
//     const char* endPtr = rawMessage + msgLength;
    
//     std::cout << "\n2. Field-by-Field Decoding:" << std::endl;
//     auto fields = IBKRDecoder::extractFields(msgContent, msgLength - 4);
    
//     if (fields.size() >= 6) {
//         std::cout << "Message Type: " << fields[0] << " (1 = TICK_PRICE)" << std::endl;
//         std::cout << "Version: " << fields[1] << std::endl;
//         std::cout << "Ticker ID: " << fields[2] << std::endl;
//         std::cout << "Tick Type: " << fields[3] << " (4 = BID)" << std::endl;
//         std::cout << "Price: " << fields[4] << std::endl;
//         std::cout << "Size: " << fields[5] << " shares" << std::endl;
//         if (fields.size() > 6) {
//             std::cout << "Attributes: " << fields[6] << std::endl;
//         }
//     }
    
//     std::cout << "\n3. Structured Message Parsing:" << std::endl;
//     // Parse as tick price message (skip message type and parse from version)
//     const char* ptr = msgContent;
//     ptr += strlen(ptr) + 1; // Skip message type field
    
//     IBKRDecoder::TickPriceData tickData;
//     if (IBKRDecoder::processTickPriceMessage(ptr, endPtr, tickData, 100)) {
//         std::cout << "Successfully parsed tick price message!" << std::endl;
//         std::cout << "  Ticker ID: " << tickData.tickerId << std::endl;
//         std::cout << "  Tick Type: " << tickData.tickType << std::endl;
//         std::cout << "  Price: $" << tickData.price << std::endl;
//         std::cout << "  Size: " << IBKRDecoder::decimalToDouble(tickData.size) << " shares" << std::endl;
//         std::cout << "  Can Auto Execute: " << (tickData.canAutoExecute ? "Yes" : "No") << std::endl;
//     }
// }

// void demonstrateTickSizeDecoding() {
//     std::cout << "\n=== IBKR Tick Size Message Example ===" << std::endl;
    
//     // Example tick size message: version=2, ticker_id=6, tick_type=5, size=200
//     const char rawSizeMessage[] = "2\x006\x005\x00200\x00";
//     size_t msgLength = strlen(rawSizeMessage) + 4; // +4 for the null terminators
    
//     std::cout << "\n1. Size Message Analysis:" << std::endl;
//     IBKRDecoder::analyzeRawMessage(rawSizeMessage, msgLength);
    
//     std::cout << "\n2. Structured Size Message Parsing:" << std::endl;
//     const char* ptr = rawSizeMessage;
//     const char* endPtr = rawSizeMessage + msgLength;
    
//     IBKRDecoder::TickSizeData sizeData;
//     if (IBKRDecoder::processTickSizeMessage(ptr, endPtr, sizeData)) {
//         std::cout << "Successfully parsed tick size message!" << std::endl;
//         std::cout << "  Ticker ID: " << sizeData.tickerId << std::endl;
//         std::cout << "  Tick Type: " << sizeData.tickType << " (5 = ASK_SIZE)" << std::endl;
//         std::cout << "  Size: " << IBKRDecoder::decimalToDouble(sizeData.size) << " shares" << std::endl;
//     }
// }

// void demonstrateComparisonWithLegacyDecoding() {
//     std::cout << "\n=== Comparison: Legacy vs IBKR-Compliant Decoding ===" << std::endl;
    
//     // Simulate the large encoded value your legacy decoder was trying to parse
//     double legacyEncodedSize = 3.58442e+18;  // Example of the large values you were seeing
    
//     std::cout << "\n1. Legacy Custom Decoder (what you had before):" << std::endl;
//     if (IBKRDecoder::isSpecialSizeValue(legacyEncodedSize)) {
//         double legacyResult = IBKRDecoder::interpretSizeValue(legacyEncodedSize, TickType::BID_SIZE);
//         std::cout << "Legacy decoder result: " << legacyResult << " shares" << std::endl;
//     }
    
//     std::cout << "\n2. IBKR-Compliant Decoder (new approach):" << std::endl;
//     // With the IBKR approach, size comes directly as a string field
//     const char* sizeField = "150";  // This would be extracted from the message fields
//     Decimal actualSize = DecimalFunctions::stringToDecimal(sizeField);
//     double actualSizeValue = IBKRDecoder::decimalToDouble(actualSize);
//     std::cout << "IBKR-compliant result: " << actualSizeValue << " shares (exact value)" << std::endl;
    
//     std::cout << "\nKey Difference:" << std::endl;
//     std::cout << "- Legacy: Tries to decode special encoded values (unreliable)" << std::endl;
//     std::cout << "- IBKR: Uses actual size values from message fields (accurate)" << std::endl;
// }

// int main() {
//     std::cout << "IBKR Decoder Integration Example" << std::endl;
//     std::cout << "=================================" << std::endl;
    
//     try {
//         demonstrateRawMessageDecoding();
//         demonstrateTickSizeDecoding();
//         demonstrateComparisonWithLegacyDecoding();
        
//         std::cout << "\n=== Integration Notes ===" << std::endl;
//         std::cout << "1. Your existing connection.cpp code continues to work unchanged" << std::endl;
//         std::cout << "2. The new IBKR-compliant methods can be used for accurate decoding" << std::endl;
//         std::cout << "3. To get actual bid/ask sizes, you need to capture raw messages" << std::endl;
//         std::cout << "4. Consider gradually migrating to the IBKR-compliant approach" << std::endl;
        
//     } catch (const std::exception& e) {
//         std::cerr << "Error in decoder example: " << e.what() << std::endl;
//         return 1;
//     }
    
//     return 0;
// } 