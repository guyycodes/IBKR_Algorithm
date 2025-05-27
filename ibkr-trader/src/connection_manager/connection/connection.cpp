#include "connection.hpp"
#include "../connection_cache/connection_cache.hpp"
#include "../../models/model_manager.hpp"  // Include ModelManager for direct access
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>  // For std::hex, std::setw, etc.
#include "../decoder/decoder.hpp"  // Include IBKRDecoder for special size value handling
#include "../decoder/frame_analyzer.hpp"  // Include FrameAnalyzer for tick string field 48 analysis

namespace connection {

    // Define connection constants
    const char* HOST = "host.docker.internal";
    // Paper Trading port is 4002, Live Trading would be 7496
    // We're using Paper Trading for testing
    int PORT = 4002;  // Paper Trading port
    int client_id = 0;  // Default client ID - we'll override this in connect()
    
    // Constructor implementation
    IBKRTrader::IBKRTrader() 
        : m_osSignal(2000)
        , m_client(new EClientSocket(this, &m_osSignal)) // "register" your callback handler
        , m_modelManager(nullptr)
        , m_requestId(-1)
        , m_lastPrice(0.0)
        , m_symbol("")
        , m_contract()
    {
    }
    
    // Destructor implementation
    IBKRTrader::~IBKRTrader() {
        // Stop any running data stream
        stopScalpingDataStream();

        
        // Clean up the client
        delete m_client;
    }
    
    // Connect to IBKR Gateway :: Called by model_manager.cpp to make initial conneection and request initial market data
    bool IBKRTrader::connect(int clientId, const std::string& symbol, const Contract& contract) {
        // Set the connection options to enable API extensions
        m_client->setConnectOptions("+PACEAPI");
        
        // Use provided client ID or fall back to default
        int actualClientId = (clientId >= 0) ? clientId : client_id;
        
        bool success = m_client->eConnect(HOST, PORT, actualClientId, /*extraAuth=*/false);
        if (success) {
            std::cout << "[INFO] Connection initiated to " << HOST << ":" << PORT 
                      << " with client ID: " << actualClientId << std::endl;
            
            // Wait a bit for connection to stabilize
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // set the symbol and contract to the class variables
            m_symbol = symbol;
            m_contract = contract;

        } else {
            std::cerr << "[ERROR] Failed to connect to IBKR." << std::endl;
        }
        return success;
    }
    
    void IBKRTrader::startDataStream() {
        std::cout << "[INFO] Automatically requesting scalping data for symbol: " << m_symbol << " and contract: " << m_contract.symbol << std::endl;
        startScalpingDataStream(m_symbol, m_contract);
    }
    
    // // Set the model manager for direct market data routing
    void IBKRTrader::setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol) {
        m_modelManager = modelManager;
        m_symbol = symbol;
        std::cout << "[IBKRTrader] Set ModelManager for symbol: " << m_symbol << std::endl;
    }

    //     //  createReader() is responsible for making a reader for retrieving messages from the IBKR connection, std::unique_ptr<EReader> (smart pointer that automatically manages memory
    std::unique_ptr<EReader> IBKRTrader::createReader() {
        std::unique_ptr<EReader> reader(new EReader(m_client, &m_osSignal));
        reader->start();
        return reader;
    }
    
    // Start message processing in a separate thread
    std::thread IBKRTrader::startMessageProcessing(std::unique_ptr<EReader>& reader) {
    // When your code makes a request (like requestMarketData (from inside the model_manager.cpp)), 
    // the IBKR server will respond with data. The EReader in the message 
    // processing thread will parse these responses  and call the appropriate 
    // EWrapper methods on your IBKRTrader instance.
        return std::thread([this, &reader]() {
            while (m_client->isConnected()) {
                m_osSignal.waitForSignal(); // waits for a notification that messages are available
                reader->processMsgs(); // processes any received messages
            }
        });
    }
    
    // Disconnect from IBKR Gateway
    void IBKRTrader::disconnect() {
        // First stop any data streams
        stopScalpingDataStream();
        
        // Cancel any active account summary request
        if (m_client && m_client->isConnected() && m_lastAccountSummaryReqId > 9000) {
            std::cout << "[INFO] Canceling active account summary request with reqId: " 
                      << m_lastAccountSummaryReqId << std::endl;
            m_client->cancelAccountSummary(m_lastAccountSummaryReqId);
            
            // Give the cancellation a moment to process
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Clear account summary data
        m_accountSummaryManager.clear();
        
        // Then disconnect from the API
        if (m_client) {
            m_client->eDisconnect();
        }
        std::cout << "[INFO] Disconnected from IBKR.\n";
    }
    
        // Route tick market data to ModelManager/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::routeTickToModelManager(double price, double volume, uint64_t timestamp,
                                           double bid, double ask, double bidSize, double askSize,
                                           const std::string& exchange, const std::string& specialConditions,
                                           double open, double high, double low, double close, double wap) {
        // Only process if we have a ModelManager
        if (!m_modelManager) {
            return;
        }
        
        // If timestamp is 0, use current time
        if (timestamp == 0) {
            timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        }
        
        // Process potential special size values using the ConnectionCache
        volume = ConnectionCache::decodeSpecialValue(volume, static_cast<int>(TickType::VOLUME));
        bidSize = ConnectionCache::decodeSpecialValue(bidSize, static_cast<int>(TickType::BID_SIZE));
        askSize = ConnectionCache::decodeSpecialValue(askSize, static_cast<int>(TickType::ASK_SIZE));
        
        // Use the ConnectionCache to merge new data with cached data
        stock_data_tick::StockData stockData = ConnectionCache::mergeWithCache(
            m_symbol,
            timestamp,
            price,
            volume,
            bid,
            ask,
            bidSize,
            askSize,
            exchange,
            open,
            high,
            low,
            close,
            wap
        );
        
        // Prune old entries from the cache (keep only last 60 minutes)
        ConnectionCache::pruneOldEntries(60);
        
        // If we have a new "last" price but no bid/ask, use it to update the last price
        if (price > 0 && stockData.bid == 0 && stockData.ask == 0) {
            m_lastPrice = price;
        }
        
        // If we have bid/ask but no last price, use midpoint
        if (stockData.last == 0 && bid > 0 && ask > 0) {
            stockData.last = (bid + ask) / 2.0;
        } else if (stockData.last == 0 && m_lastPrice > 0) {
            // If no new price and no bid/ask, use the last known price
            stockData.last = m_lastPrice;
        }
        
        // Check if we have a complete data element before processing
        bool isComplete = (stockData.last > 0 && 
                          stockData.bid > 0 && 
                          stockData.ask > 0 && 
                          stockData.bidSize > 0 && 
                          stockData.askSize > 0);
        
        if (isComplete) {
        
        // Get thread ID for logging
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        
        // Log the data being sent to ModelManager (compact format)
        std::cout << "[Data][" << m_symbol << "] "
                  << "L:" << (stockData.last > 0 ? std::to_string(stockData.last) : "-") << " "
                  << "B:" << (stockData.bid > 0 ? std::to_string(stockData.bid) : "-") << " "
                  << "A:" << (stockData.ask > 0 ? std::to_string(stockData.ask) : "-") << " "
                  << "V:" << (stockData.volume > 0 ? std::to_string(stockData.volume) : "-") << " "
                  << "BS:" << (stockData.bidSize > 0 ? std::to_string(stockData.bidSize) : "-") << " "
                  << "AS:" << (stockData.askSize > 0 ? std::to_string(stockData.askSize) : "-") << " "
                  << "WAP:" << (stockData.wap > 0 ? std::to_string(stockData.wap) : "-") << " "
                  << "OHLC:" << (stockData.open > 0 ? std::to_string(stockData.open) : "-") << "/"
                            << (stockData.high > 0 ? std::to_string(stockData.high) : "-") << "/"
                            << (stockData.low > 0 ? std::to_string(stockData.low) : "-") << "/"
                            << (stockData.close > 0 ? std::to_string(stockData.close) : "-") << " "
                  << "Ex:" << (!stockData.exchange.empty() ? stockData.exchange : "-") << " "
                  << "Cond:" << (!specialConditions.empty() ? specialConditions : "-") << " " 
                  << "Time:" << (stockData.timestamp > 0 ? std::to_string(stockData.timestamp) : "-") << " "
                  << "WAP:" << (stockData.wap > 0 ? std::to_string(stockData.wap) : "-") << "\n ready to send" << "\n" << std::endl;
        
        // Add debug output to see if we have any OHLC data
        if (stockData.open > 0 || stockData.high > 0 || stockData.low > 0 || stockData.close > 0) {
            std::cout << "[DEBUG][OHLC] Received valid OHLC data: " 
                      << stockData.open << "/" << stockData.high << "/" << stockData.low << "/" << stockData.close << std::endl;
        }
        
        // Add debug output for WAP
        if (stockData.wap > 0) {
            std::cout << "[DEBUG][WAP] StockData with WAP: " << stockData.wap << " ready to send" << std::endl;
        }
        
        // Send to ModelManager
        m_modelManager->addTick(stockData);
        } else {
            // Log that we're skipping incomplete data
            std::cout << "[Data][" << m_symbol << "] Incomplete data, waiting for more fields before processing" << std::endl;
        }
    }

        ///////////////////////////////////////////////////////////////////////////
    // STRING DATA CALLBACK
    // Processes string data (primarily timestamps) and routes relevant info to ModelManager
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::tickString(TickerId tickerId, TickType field, const std::string& value) {
        // Store the timestamp globally when we receive field 88
        static uint64_t lastTimestamp = 0;
        
        // Handle field 48 specially for volume/VWAP analysis
        if (field == 48) {
            std::cout << "[FrameAnalyzer] Processing field 48 tick string: " << value << std::endl;
            ibkr_decoder::FrameAnalyzer::analyzeTickString48(value);
        }
        
        // Handle field 88 specially as it appears to be a timestamp
        if (field == 88) {
            try {
                // Convert string to timestamp (Unix epoch)
                uint64_t timestamp = std::stoull(value);
                lastTimestamp = timestamp * 1000; // Convert to ms for consistency
                
                // Create a readable time format for logging
                time_t time_t_timestamp = static_cast<time_t>(timestamp);
                char timeStr[30];
                struct tm timeinfo;
                
                #ifdef _WIN32
                localtime_s(&timeinfo, &time_t_timestamp);
                #else
                localtime_r(&time_t_timestamp, &timeinfo);
                #endif
                
                strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
                
                // Log in a compact format
                std::cout << "[TickTimestamp] ID:" << tickerId 
                          << " Timestamp:" << value 
                          << " Time:" << timeStr << std::endl;
                
                // If we have a ModelManager, use this timestamp for synchronization
                if (m_modelManager && tickerId == m_requestId) {
                    // Send a blank update just to update the timestamp
                    routeTickToModelManager(0, 0, lastTimestamp, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0);
                }
                
                return; // Skip detailed logging for timestamps
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to parse timestamp in tickString: " << e.what() << std::endl;
                // Continue with default logging
            }
        }
        
        // For all other fields, use detailed logging
        std::cout << "\n===== TICK STRING DATA =====\n";
        std::cout << "tickerId: " << tickerId << "\n";
        std::cout << "field (code): " << static_cast<int>(field);
        
        // Map field code to readable name
        std::string fieldName;
        switch (field) {
            // Fix to use only existing tick types
            case TickType::LAST_TIMESTAMP: fieldName = "LAST_TIMESTAMP"; break;
            case TickType::LAST_REG_TIME: fieldName = "LAST_REG_TIME"; break;
            case 88: fieldName = "TIMESTAMP"; break;
            default: fieldName = "UNKNOWN_STRING_" + std::to_string(field); break;
        }
        
        std::cout << " (" << fieldName << ")\n";
        std::cout << "value: " << value << "\n";
        std::cout << "==========================\n";
        
        // Original code
        std::cout << "[IBKRTrader::tickString] Raw callback - ID: " << tickerId
                  << ", Field: " << static_cast<int>(field)
                  << ", Value: " << value << std::endl;
    }

        void IBKRTrader::error(int id, long errorTime, int errorCode,
                           const std::string& errorString,
                           const std::string& advancedOrderRejectJson) {
        std::cerr << "[ERROR] ReqId: " << id 
                  << " Code: " << errorCode 
                  << " Msg: " << errorString << std::endl;
                  
        // Simplified error handling - remove delayed data fallbacks
        if (errorCode == 162) {
            std::cerr << "[INFO] Historical data request error." << std::endl;
        } else if (errorCode == 200) {
            std::cerr << "[INFO] No security definition found. "
                      << "Check the contract details and symbol." << std::endl;
        } else if (errorCode == 10092) {
            std::cerr << "[INFO] Deep market data is not supported for this security. "
                      << "Using top-of-book data instead." << std::endl;
            
            // If it's a market depth error, try to fall back to regular market data
            if (id >= 8000 && id < 9000) { // Market depth request IDs are in this range
                // First get the symbol from the error ID context (this is simplified)
                std::string symbolForFallback = m_symbol;
                
                if (!symbolForFallback.empty()) {
                    std::cout << "[FALLBACK] Cannot get market depth for " << symbolForFallback 
                              << ". Using regular market data instead." << std::endl;
                    
                    // Request basic market data as fallback
                    // static int fallbackId = 7500;
                    // requestMarketData(symbolForFallback);
                }
            }
        } else if (errorCode == 10189) {
            std::cerr << "[INFO] Failed to request tick-by-tick data. "
                      << "Verify your market data subscription." << std::endl;
            
            // If it's a tick-by-tick error, try to fall back to regular market data
            if (id >= 7000 && id < 8000) { // Tick-by-tick request IDs are in this range
                // Get the symbol from the context
                std::string symbolForFallback = m_symbol;
                
                if (!symbolForFallback.empty()) {
                    std::cout << "[FALLBACK] Cannot get tick-by-tick data for " << symbolForFallback 
                              << ". Using regular market data instead." << std::endl;
                }
            }
        } else if (errorCode == 321) {
            std::cerr << "[INFO] Historical data request format error. "
                      << "Check duration format (should be 'X Y' where X is integer and Y is unit)." << std::endl;
            
            // If it's a historical data error, log it and move on
            if (id >= 4000 && id < 5000) { // Historical data request IDs are in this range
                std::cout << "[FALLBACK] Historical data request failed. "
                          << "Continuing with available real-time data." << std::endl;
            }
        }
                  
        // If we have a ModelManager, log symbol-specific errors
        if (m_modelManager && id == m_requestId) {
            std::cerr << "[ERROR] Error for symbol " << m_symbol << ": " << errorString << std::endl;
        }
    }

    //     ///////////////////////////////////////////////////////////////////////////
//     // PRICE DATA CALLBACK
//     // Processes price updates (BID, ASK, LAST) and routes them to ModelManager
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
        // Only log for fields we care about in scalping (focus on real-time data)
        bool isRelevantField = (field == TickType::BID || field == TickType::ASK || field == TickType::LAST ||
                               field == TickType::OPEN || field == TickType::HIGH || 
                               field == TickType::LOW || field == TickType::CLOSE ||
                               // Keep delayed types as fallback
                               field == TickType::DELAYED_BID || field == TickType::DELAYED_ASK || 
                               field == TickType::DELAYED_LAST ||
                               field == TickType::DELAYED_OPEN || field == TickType::DELAYED_HIGH || 
                               field == TickType::DELAYED_LOW || field == TickType::DELAYED_CLOSE);
        
        if (isRelevantField) {
            // Map field code to readable name
            std::string fieldName;
            switch (field) {
                case TickType::BID: fieldName = "BID"; break;
                case TickType::ASK: fieldName = "ASK"; break;
                case TickType::LAST: fieldName = "LAST"; break;
                case TickType::DELAYED_BID: fieldName = "DELAYED_BID"; break;
                case TickType::DELAYED_ASK: fieldName = "DELAYED_ASK"; break;
                case TickType::DELAYED_LAST: fieldName = "DELAYED_LAST"; break;
                case TickType::OPEN: fieldName = "OPEN"; break;
                case TickType::HIGH: fieldName = "HIGH"; break;
                case TickType::LOW: fieldName = "LOW"; break;
                case TickType::CLOSE: fieldName = "CLOSE"; break;
                case TickType::DELAYED_OPEN: fieldName = "DELAYED_OPEN"; break;
                case TickType::DELAYED_HIGH: fieldName = "DELAYED_HIGH"; break;
                case TickType::DELAYED_LOW: fieldName = "DELAYED_LOW"; break;
                case TickType::DELAYED_CLOSE: fieldName = "DELAYED_CLOSE"; break;
                default: fieldName = "UNKNOWN_" + std::to_string(field); break;
            }
            
            // Log compact price update
            std::cout << "[Price] ID:" << tickerId << " " << fieldName << ":" << price << std::endl;
        }
        
        // Check if this is for our ModelManager's request ID
        if (m_modelManager && tickerId == m_requestId) {
            // Store the last price we've seen if it's a LAST price type
            if (field == TickType::LAST || field == TickType::DELAYED_LAST) {
                m_lastPrice = price;
            }
            
            // If it's a price we care about (BID, ASK, LAST), route it to the model manager
            if (field == TickType::BID || field == TickType::DELAYED_BID) {
                // For BID prices, use the bid parameter
                routeTickToModelManager(0, 0, 0, price, 0, 0, 0, "", "", 0, 0, 0, 0, 0);
            }
            else if (field == TickType::ASK || field == TickType::DELAYED_ASK) {
                // For ASK prices, use the ask parameter
                routeTickToModelManager(0, 0, 0, 0, price, 0, 0, "", "", 0, 0, 0, 0, 0);
            }
            else if (field == TickType::LAST || field == TickType::DELAYED_LAST) {
                // For LAST prices, use the price parameter
                routeTickToModelManager(price, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0);
            }
            else if (field == TickType::OPEN || field == TickType::DELAYED_OPEN) {
                // For OPEN price, set the open parameter
                routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", price, 0, 0, 0, 0);
            }
            else if (field == TickType::HIGH || field == TickType::DELAYED_HIGH) {
                // For HIGH price, set the high parameter
                routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", 0, price, 0, 0, 0);
            }
            else if (field == TickType::LOW || field == TickType::DELAYED_LOW) {
                // For LOW price, set the low parameter
                routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", 0, 0, price, 0, 0);
            }
            else if (field == TickType::CLOSE || field == TickType::DELAYED_CLOSE) {
                // For CLOSE price, set the close parameter
                routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, price, 0);
            }
        }
    }

//     ///////////////////////////////////////////////////////////////////////////
//     // SIZE DATA CALLBACK
//     // Processes size updates (BID_SIZE, ASK_SIZE, LAST_SIZE, VOLUME) and routes them to ModelManager
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::tickSize(TickerId tickerId, TickType field, Decimal size) {
        // Only log for fields we care about in scalping (focus on real-time data)
        bool isRelevantField = (field == TickType::BID_SIZE || field == TickType::ASK_SIZE || 
                               field == TickType::LAST_SIZE || field == TickType::VOLUME ||
                               // Keep delayed types as fallback
                               field == TickType::DELAYED_BID_SIZE || field == TickType::DELAYED_ASK_SIZE || 
                               field == TickType::DELAYED_LAST_SIZE || field == TickType::DELAYED_VOLUME);
        
        if (isRelevantField) {
            // Map field code to readable name
            std::string fieldName;
            switch (field) {
                case TickType::BID_SIZE: fieldName = "BID_SIZE"; break;
                case TickType::ASK_SIZE: fieldName = "ASK_SIZE"; break;
                case TickType::LAST_SIZE: fieldName = "LAST_SIZE"; break;
                case TickType::VOLUME: fieldName = "VOLUME"; break;
                case TickType::DELAYED_BID_SIZE: fieldName = "DELAYED_BID_SIZE"; break;
                case TickType::DELAYED_ASK_SIZE: fieldName = "DELAYED_ASK_SIZE"; break;
                case TickType::DELAYED_LAST_SIZE: fieldName = "DELAYED_LAST_SIZE"; break;
                case TickType::DELAYED_VOLUME: fieldName = "DELAYED_VOLUME"; break;
                default: fieldName = "UNKNOWN_SIZE_" + std::to_string(field); break;
            }
            
            // Log compact size update
            std::cout << "[Size] ID:" << tickerId << " " << fieldName << ":" << static_cast<double>(size) << std::endl;
        }
        
        // Check if this is for our ModelManager's request ID
        if (m_modelManager && tickerId == m_requestId) {
            double sizeValue = static_cast<double>(size);
            
            // Check if this is a special size value
            if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(sizeValue)) {
                sizeValue = ibkr_decoder::IBKRDecoder::interpretSizeValue(sizeValue, field);
            }
            
            // Route data based on the field type
            if (field == TickType::BID_SIZE || field == TickType::DELAYED_BID_SIZE) {
                // For BID_SIZE, use the bidSize parameter
                routeTickToModelManager(0, 0, 0, 0, 0, sizeValue, 0, "", "", 0, 0, 0, 0, 0);
            }
            else if (field == TickType::ASK_SIZE || field == TickType::DELAYED_ASK_SIZE) {
                // For ASK_SIZE, use the askSize parameter
                routeTickToModelManager(0, 0, 0, 0, 0, 0, sizeValue, "", "", 0, 0, 0, 0, 0);
            }
            else if (field == TickType::LAST_SIZE || field == TickType::DELAYED_LAST_SIZE) {
                // For LAST_SIZE with the last known price
                routeTickToModelManager(m_lastPrice, sizeValue, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0);
            }
            else if (field == TickType::VOLUME || field == TickType::DELAYED_VOLUME) {
                // For VOLUME with the last known price
                routeTickToModelManager(m_lastPrice, sizeValue, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0);
            }
        }
    }

    void IBKRTrader::tickGeneric(TickerId tickerId, TickType field, double value) {
        std::cout << "\n===== TICK GENERIC DATA =====\n";
        std::cout << "tickerId: " << tickerId << "\n";
        std::cout << "field (code): " << static_cast<int>(field);
        
        // Map field code to readable name
        std::string fieldName;
        switch (field) {
            case TickType::HALTED: fieldName = "HALTED"; break;
            case TickType::AVG_VOLUME: fieldName = "AVG_VOLUME"; break;
            case TickType::SHORTABLE: fieldName = "SHORTABLE"; break;
            // WAP - weighted average price (field code typically 14)
            case 14: fieldName = "WAP"; break;
            // Delayed WAP (field code 64 + 14 = 78)
            case 78: fieldName = "DELAYED_WAP"; break;
            default: fieldName = "UNKNOWN_GENERIC_" + std::to_string(field); break;
        }
        
        std::cout << " (" << fieldName << ")\n";
        std::cout << "value: " << value << "\n";
        std::cout << "===========================\n";
        
        // Original code
        std::cout << "[IBKRTrader::tickGeneric] Raw callback - ID: " << tickerId
                  << ", Field: " << static_cast<int>(field)
                  << ", Value: " << value << std::endl;
                  
        // Check if this is for our ModelManager's request ID
        if (m_modelManager && tickerId == m_requestId) {
            // Process WAP value (field code = 14, Delayed WAP = 78)
            if (field == 14 || field == 78) {
                // Decode WAP if needed
                double decodedWap = value;
                
                // If WAP appears to be encoded (large exponent values)
                if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(decodedWap)) {
                    std::cout << "[DEBUG] Decoding special WAP value in tickGeneric: " << decodedWap << std::endl;
                    decodedWap = ibkr_decoder::IBKRDecoder::interpretSizeValue(decodedWap, field);
                }
                
                // Add debug output for WAP
                std::cout << "[DEBUG][WAP] Generic tick provided WAP: " << decodedWap 
                          << " for " << m_symbol << " (original: " << value << ")" << std::endl;
                
                // Pass as WAP parameter
                routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, decodedWap);
            }
            // Also handle HALTED, AVG_VOLUME and SHORTABLE if needed in the future
            else {
                // Pass other generic tick data
                routeTickToModelManager(0, 0, 0, 0, 0, 0, 0, "", "", 0, 0, 0, 0, 0);
            }
        }
    }

    //     ///////////////////////////////////////////////////////////////////////////
//     // REAL-TIME BAR DATA CALLBACK
//     // Processes OHLC, volume and WAP from 5-second bars and routes to ModelManager
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::realtimeBar(TickerId reqId, long time, double open, double high, 
                             double low, double close, Decimal volume, Decimal wap, int count) {
        // Format time as human-readable
        time_t epochTime = time;
        char timeStr[30];
        struct tm* timeinfo = localtime(&epochTime);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        std::cout << "\n======== REALTIME BAR DATA (" << reqId << ") ========\n";
        std::cout << "TIME: " << timeStr << "\n";
        std::cout << "OPEN: " << open << "\n";
        std::cout << "HIGH: " << high << "\n";
        std::cout << "LOW: " << low << "\n";
        std::cout << "CLOSE: " << close << "\n";
        std::cout << "VOLUME: " << static_cast<double>(volume) << "\n";
        std::cout << "WAP: " << static_cast<double>(wap) << "\n";
        std::cout << "COUNT: " << count << "\n";
        std::cout << "==========================================\n";
        
        // Original code continues...
        std::cout << "[IBKRTrader::realtimeBar] Raw callback - ID: " << reqId
                  << ", Time: " << timeStr
                  << ", OHLC: " << open << "/" << high << "/" << low << "/" << close
                  << ", Volume: " << static_cast<double>(volume)
                  << ", WAP: " << static_cast<double>(wap)
                  << ", Count: " << count << std::endl;
                  
        // If we have a ModelManager, process the data
        if (m_modelManager && (reqId == m_requestId || (reqId >= 6000 && reqId < 7000))) {
            // Create timestamp from the provided time_t
            uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
            // Process WAP value - decode if needed
            double decodedWap = static_cast<double>(wap);
            
            // If WAP appears to be encoded (like with the large exponent values we're seeing)
            if (ibkr_decoder::IBKRDecoder::isSpecialSizeValue(decodedWap)) {
                std::cout << "[DEBUG] Decoding special WAP value in realtime bar: " << decodedWap << std::endl;
                // Use the size decoder since WAP appears to be encoded like sizes
                decodedWap = ibkr_decoder::IBKRDecoder::interpretSizeValue(decodedWap, 14); // 14 is WAP field code
            }
            
            // Route OHLC, WAP and other data - all at once for complete data
            routeTickToModelManager(
                close,                         // Use close as the last price
                static_cast<double>(volume),   // Volume
                timestamp,                     // Timestamp
                0,                             // No bid in bar data
                0,                             // No ask in bar data
                0,                             // No bidSize in bar data
                0,                             // No askSize in bar data
                "",                            // No exchange info in bar data
                "",                            // No special conditions in bar data
                open,                          // Open
                high,                          // High
                low,                           // Low
                close,                         // Close
                decodedWap                     // Decoded WAP
            );

            // Add debug output for WAP
            if (decodedWap > 0) {
                std::cout << "[DEBUG][WAP] Real-time bar provided WAP: " << decodedWap 
                          << " for " << m_symbol << " (original: " << static_cast<double>(wap) << ")" << std::endl;
            }
        }
    }

     // Start continuous data stream for scalping with timed updates
    void IBKRTrader::startScalpingDataStream(const std::string& symbol, const Contract& contract) {
        if (!m_client || !m_client->isConnected()) {
            std::cerr << "[ERROR] Cannot start scalping data stream: not connected" << std::endl;
            return;
        }
        
        // Stop any existing data stream thread
        stopScalpingDataStream();
        
        // Reset timing variables
        m_stopDataStream = false;
        m_lastAccountUpdate = std::chrono::high_resolution_clock::now();
        
        // Make initial data subscriptions
        std::cout << "[INFO] Making initial data subscriptions for " << symbol << std::endl;
        requestScalpingData(symbol, contract);
        
        // Start the refresh thread for account data only
        m_dataRefreshThread = std::thread([this]() {
            std::cout << "[INFO] Starting account data refresh thread" << std::endl;
            
            while (!m_stopDataStream && m_client->isConnected()) {
                auto now = std::chrono::high_resolution_clock::now();
                
                // Account data refresh (every 30 seconds)
                auto accountElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastAccountUpdate).count();
                
                if (accountElapsed >= 30000) { // 30 seconds
                    std::cout << "[Timer] Refreshing account data" << std::endl;
                    requestAccountSummary();
                    m_lastAccountUpdate = now;
                }
                
                // Sleep to prevent CPU hogging (100ms)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            std::cout << "[INFO] Account data refresh thread stopped" << std::endl;
        });
    }

    // Request only essential data for scalping algorithms - these are one-time subscriptions
    void IBKRTrader::requestScalpingData(const std::string& symbol, const Contract& contract) {
        if (!m_client || !m_client->isConnected()) {
            std::cerr << "[ERROR] Cannot request scalping data: not connected" << std::endl;
            return;
        }
        
        std::cout << "\n==================================================\n";
        std::cout << "SUBSCRIBING TO SCALPING DATA FOR: " << symbol << "\n";
        std::cout << "==================================================\n";
        
        // Set to use real-time data instead of delayed
        m_client->reqMarketDataType(1); // 1 = REALTIME (was 3 = DELAYED)
        
        // 1. Account Information (will be refreshed periodically by timer)
        std::cout << "[1] Requesting Account Information\n";
        requestAccountSummary();
        
        // 3. Tick-by-Tick Data (continuous subscription)
        std::cout << "[3] Subscribing to Tick-by-Tick Data for " << symbol << "\n";
        static int tickRequestId = 7001;
        requestTickByTickData(tickRequestId++, symbol, "AllLast", 0, false, contract); // Last trades
        requestTickByTickData(tickRequestId++, symbol, "BidAsk", 0, false, contract);  // Bid/Ask updates
        
        // 4. Market Depth (continuous subscription)
        std::cout << "[4] Subscribing to Market Depth for " << symbol << "\n";
        static int depthReqId = 8001;
        
        // // Create contract for market depth
        // Contract contract;
        // contract.symbol = symbol;
        // contract.secType = "STK";
        // contract.currency = "USD";
        // contract.exchange = "SMART";
        
        m_client->reqMktDepth(depthReqId++, contract, 5, false, {});
        
        // 5. Short-term Historical Context (one-time request with keepUpToDate=true)
        // std::cout << "[5] Requesting Short-term Historical Context for " << symbol << "\n";
        // requestHistoricalData(symbol, "10 min", "1 min", "TRADES"); // Just 10 minutes of 1-min bars
        
        // 6. Add Real-time Bar data (5-second bars) to get continuous OHLC updates
        std::cout << "[6] Subscribing to Real-time Bar Data for " << symbol << "\n";
        static int barRequestId = 6001;
        
        // Create contract for real-time bars
        // Contract barContract;
        // barContract.symbol = symbol;
        // barContract.secType = "STK";
        // barContract.currency = "USD";
        // barContract.exchange = "SMART";
        
        // 5 = 5 seconds, "TRADES" = based on trade data, useRTH = true (regular trading hours only)
        m_client->reqRealTimeBars(barRequestId++, contract, 5, "TRADES", true, {});

        
        std::cout << "\n==================================================\n";
        std::cout << "DATA SUBSCRIPTIONS ACTIVE - RECEIVING CALLBACKS\n";
        std::cout << "==================================================\n";
    }
    
    // // Stop the data refresh thread
    void IBKRTrader::stopScalpingDataStream() {
        // Signal the thread to stop
        m_stopDataStream = true;
        
        // Join the thread if it's running
        if (m_dataRefreshThread.joinable()) {
            std::cout << "[INFO] Waiting for data refresh thread to stop..." << std::endl;
            m_dataRefreshThread.join();
            std::cout << "[INFO] Data refresh thread stopped" << std::endl;
        }
    }
    
//     // Send a ping and return the request ID
//     int IBKRTrader::sendPing() {
//         std::lock_guard<std::mutex> lock(m_pingMutex);
//         int pingId = m_nextPingId++;
        
//         // Record the time we sent the request
//         m_pingRequests[pingId] = std::chrono::high_resolution_clock::now();
        
//         // Use reqCurrentTime as our ping mechanism
//         m_client->reqCurrentTime();
        
//         return pingId;
//     }
    
//     // Measure average latency over multiple pings
//     double IBKRTrader::measureAverageLatency(int numPings) {
//         double totalLatency = 0;
//         int successfulPings = 0;
        
//         for (int i = 0; i < numPings; i++) {
//             sendPing();
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         }
        
//         // Wait for responses
//         std::this_thread::sleep_for(std::chrono::seconds(1));
        
//         // Calculate average from completed pings (placeholder example)
//         std::lock_guard<std::mutex> lock(m_pingMutex);
//         // In a real scenario, you'd track each ping's round-trip precisely.
//         return successfulPings > 0 ? totalLatency / successfulPings : -1.0;
//     }

//     /***************************************************
//      * EWrapper overrides for connection handling
//      ***************************************************/

//     void IBKRTrader::nextValidId(OrderId orderId) {
//         std::cout << "[INFO] nextValidId: " << orderId << std::endl;
//         m_nextOrderId = orderId;
//     }

//     void IBKRTrader::connectAck() {
//         std::cout << "[INFO] connectAck: Connected to IBKR" << std::endl;
//     }

//     void IBKRTrader::connectionClosed() {
//         std::cout << "[INFO] Connection closed." << std::endl;
//     }

//     // This is called when server responds to reqCurrentTime() (for pings)
//     void IBKRTrader::currentTime(long time) {
//         auto now = std::chrono::high_resolution_clock::now();
//         std::lock_guard<std::mutex> lock(m_pingMutex);
        
//         if (!m_pingRequests.empty()) {
//             auto it = m_pingRequests.begin(); // FIFO
//             auto sendTime = it->second;
//             auto latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(now - sendTime).count();
//             double latencyMs = latencyUs / 1000.0;

//             std::cout << "[Ping] latency: " << latencyMs << " ms (Server time: " << time << ")" << std::endl;
            
//             m_pingRequests.erase(it);
//         }
//     }

//     ///////////////////////////////////////////////////////////////////////////
//     // TICK-BY-TICK TRADE DATA CALLBACK
//     // Processes detailed trade information and routes to ModelManager
//     //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void IBKRTrader::tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
                                    Decimal size, const TickAttribLast& tickAttribLast, 
                                    const std::string& exchange, const std::string& specialConditions) {
        // Log compact trade update
        std::cout << "[REAL-TIME TRADE] ID:" << reqId 
                  << " Time:" << time
                  << " Price:" << price
                  << " TRADE_VOLUME:" << static_cast<double>(size)
                  << " Type:" << (tickType == 1 ? "Last" : "AllLast") 
                  << " Exchange:" << exchange
                  << " Conditions:" << specialConditions << std::endl;
                          // Highlight this is individual trade volume, not cumulative
        std::cout << "[INDIVIDUAL TRADE VOLUME] " << static_cast<double>(size) 
                  << " shares traded at $" << price << std::endl;
        
        // Check if this is for our ModelManager's request ID
        if (m_modelManager && reqId == m_requestId) {
            // Store the last price we've seen
            m_lastPrice = price;
            
            // Create timestamp from the provided time_t
            // uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
            // Route trade data through our central point with special conditions
            // routeTickToModelManager(
            //     price,
            //     static_cast<double>(size),
            //     timestamp,
            //     0, // bid
            //     0, // ask
            //     0, // bidSize
            //     0, // askSize
            //     exchange,
            //     specialConditions // pass special conditions
            // );
        }
    }

//     // Implement this if you suspect your connection handling isn't set up correctly
//     void IBKRTrader::managedAccounts(const std::string& accountsList) {
//         std::cout << "[INFO] Managed accounts: " << accountsList << std::endl;
//     }

//     //------------------------------------------
//     // Account and Portfolio Methods
//     //------------------------------------------

    void IBKRTrader::requestAccountSummary() {
        if (!m_client || !m_client->isConnected()) {
            std::cerr << "[ERROR] Cannot request account summary: not connected" << std::endl;
            return;
        }
        
        // Cancel any previous account summary request to avoid hitting API limits
        if (m_lastAccountSummaryReqId > 9000) {
            std::cout << "[INFO] Canceling previous account summary request with reqId: " 
                      << m_lastAccountSummaryReqId << std::endl;
            m_client->cancelAccountSummary(m_lastAccountSummaryReqId);
            
            // Allow a brief pause for the cancellation to process
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Increment request ID for the new request
        m_lastAccountSummaryReqId++;
        
        std::cout << "[INFO] Requesting account summary with reqId: " << m_lastAccountSummaryReqId << std::endl;
        
        // Get all available tags for account summary
        std::string tags = "AccountType,NetLiquidation,TotalCashValue,SettledCash,AccruedCash,";
        tags += "BuyingPower,EquityWithLoanValue,PreviousEquityWithLoanValue,GrossPositionValue,";
        tags += "ReqTEquity,ReqTMargin,SMA,InitMarginReq,MaintMarginReq,AvailableFunds,";
        tags += "ExcessLiquidity,Cushion,FullInitMarginReq,FullMaintMarginReq,FullAvailableFunds,";
        tags += "FullExcessLiquidity,LookAheadNextChange,LookAheadInitMarginReq,";
        tags += "LookAheadMaintMarginReq,LookAheadAvailableFunds,LookAheadExcessLiquidity,";
        tags += "HighestSeverity,DayTradesRemaining,Leverage";
        
        // Request summary for all accounts
        m_client->reqAccountSummary(m_lastAccountSummaryReqId, "All", tags);
    }
    
//     void IBKRTrader::requestPositions() {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request positions: not connected" << std::endl;
//             return;
//         }
        
//         std::cout << "[INFO] Requesting all positions" << std::endl;
//         m_client->reqPositions(); // Request all positions for all accounts
//     }
    
//     void IBKRTrader::requestPnL() {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request PnL: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 7001;
//         reqId++;
        
//         // First we need an account ID, which should be received from managedAccounts callback
//         // For demo purposes using a placeholder - in real implementation, store and use actual account ID
//         std::string account = "DU12345"; // Replace with actual account from managedAccounts
        
//         std::cout << "[INFO] Requesting PnL for account " << account << " with reqId: " << reqId << std::endl;
//         m_client->reqPnL(reqId, account, "");
//     }
    
//     void IBKRTrader::requestAccountUpdates(const std::string& account) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request account updates: not connected" << std::endl;
//             return;
//         }
        
//         std::string accountToUse = account;
//         if (accountToUse.empty()) {
//             // Use the first account from managedAccounts callback
//             // In real implementation, store and use actual account ID
//             accountToUse = "DU12345"; // Replace with actual account
//         }
        
//         std::cout << "[INFO] Requesting account updates for " << accountToUse << std::endl;
//         m_client->reqAccountUpdates(true, accountToUse);
//     }
    
//     void IBKRTrader::requestMarketData(const std::string& symbol, const std::string& secType,
//                                      const std::string& currency, const std::string& exchange) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request market data: not connected" << std::endl;
//             return;
//         }
        
//         m_requestId++; // Use instance variable to track request IDs
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = secType;
//         contract.currency = currency;
//         contract.exchange = exchange;
        
//         // Store symbol for this request ID
//         m_symbol = symbol;
        
//         // Change this line to request WAP explicitly
//         std::string genericTicks = "14"; // Request WAP (field code 14)
//         bool snapshot = false;         // Continuous updates instead of snapshot
//         bool regulatorySnapshot = false;
        
//         // Create empty options list (remove DELAYED tag)
//         TagValueListSPtr mktDataOptions(new TagValueList());
        
//         std::cout << "[INFO] Requesting market data for " << symbol 
//                   << " with reqId: " << m_requestId << " (REALTIME)" << std::endl;
        
//         m_client->reqMktData(m_requestId, contract, genericTicks, snapshot, 
//                            regulatorySnapshot, mktDataOptions);
//     }
    
//     void IBKRTrader::cancelMarketData(TickerId tickerId) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot cancel market data: not connected" << std::endl;
//             return;
//         }
        
//         std::cout << "[INFO] Canceling market data for reqId: " << tickerId << std::endl;
//         m_client->cancelMktData(tickerId);
//     }
    
//     void IBKRTrader::requestHistoricalData(const std::string& symbol, const std::string& duration,
//                                          const std::string& barSize, const std::string& whatToShow) {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot request historical data: not connected" << std::endl;
//             return;
//         }
        
//         static int reqId = 4001;
//         reqId++;
        
//         // Use real-time market data type
//         m_client->reqMarketDataType(1); // 1 = REALTIME (was 3 = DELAYED)
        
//         // Create contract object
//         Contract contract;
//         contract.symbol = symbol;
//         contract.secType = "STK";
//         contract.currency = "USD";
//         contract.exchange = "SMART";
        
//         // Current time formatted as YYYYMMDD HH:MM:SS
//         auto now = std::chrono::system_clock::now();
//         auto in_time_t = std::chrono::system_clock::to_time_t(now);
//         std::stringstream endDateTime;
//         struct tm timeinfo;
// #ifdef _WIN32
//         localtime_s(&timeinfo, &in_time_t);
// #else
//         localtime_r(&in_time_t, &timeinfo);
// #endif
//         endDateTime << std::put_time(&timeinfo, "%Y%m%d %H:%M:%S");
        
//         bool useRTH = true; // Regular Trading Hours only
//         int formatDate = 1;  // Format date as yyyyMMdd HH:mm:ss
        
//         // Create an empty options list - IMPORTANT: don't use "DELAYED" tag as it's not valid
//         TagValueListSPtr chartOptions(new TagValueList());
        
//         // Ensure duration format is correct: integer{SPACE}unit
//         // Fix the format if needed (e.g., "10min" -> "10 min")
//         std::string fixedDuration = duration;
//         // Check if there's a space between number and unit
//         bool hasSpace = false;
//         for (size_t i = 0; i < duration.length(); i++) {
//             if (duration[i] == ' ') {
//                 hasSpace = true;
//                 break;
//             }
//             // If we find a non-digit, and there's no space before it, insert one
//             if (!std::isdigit(duration[i]) && i > 0 && std::isdigit(duration[i-1])) {
//                 fixedDuration.insert(i, " ");
//                 hasSpace = true;
//                 break;
//             }
//         }
        
//         // If we didn't find a space and there are digits, assume format needs fixing
//         if (!hasSpace && !duration.empty() && std::isdigit(duration[0])) {
//             // Find position where digits end
//             size_t pos = 0;
//             while (pos < duration.length() && std::isdigit(duration[pos])) {
//                 pos++;
//             }
//             // Insert space between number and unit
//             if (pos < duration.length()) {
//                 fixedDuration.insert(pos, " ");
//             }
//         }
        
//         std::cout << "[INFO] Requesting historical data for " << symbol 
//                   << " with reqId: " << reqId 
//                   << ", duration: " << fixedDuration 
//                   << ", barSize: " << barSize 
//                   << ", whatToShow: " << whatToShow
//                   << " (Using real-time data)" << std::endl;
                  
//         m_client->reqHistoricalData(reqId, contract, endDateTime.str(), fixedDuration, 
//                                   barSize, whatToShow, useRTH, formatDate, false, chartOptions);
//     }
    
    void IBKRTrader::requestTickByTickData(TickerId reqId, const std::string& symbol, 
                                         const std::string& tickType, int numberOfTicks, 
                                         bool ignoreSize, const Contract& contract) {
        if (!m_client || !m_client->isConnected()) {
            std::cerr << "[ERROR] Cannot request tick-by-tick data: not connected" << std::endl;
            return;
        }
        
        // // Create contract object
        // Contract contract;
        // contract.symbol = symbol;
        // contract.secType = "STK";
        // contract.currency = "USD";
        // contract.exchange = "SMART";
        
        // IBKR API expects specific string values for tick types
        std::string apiTickType = "AllLast"; // Default
        
        if (tickType == "Last") {
            apiTickType = "Last";
        } else if (tickType == "AllLast") {
            apiTickType = "AllLast";
        } else if (tickType == "BidAsk") {
            apiTickType = "BidAsk";
        } else if (tickType == "MidPoint") {
            apiTickType = "MidPoint";
        }
        
        std::cout << "[INFO] Requesting tick-by-tick data for " << symbol 
                  << " with reqId: " << reqId 
                  << ", type: " << apiTickType << std::endl;
                  
        // Note: Tick-by-tick data requires market data subscription in IB
        // For delayed data users, this will either not work or provide delayed data
        m_client->reqTickByTickData(reqId, contract, apiTickType, numberOfTicks, ignoreSize);
    }
    
//     //------------------------------------------
//     // Account and Portfolio Callbacks
//     //------------------------------------------
    
    void IBKRTrader::accountSummary(int reqId, const std::string& account, const std::string& tag, 
                                  const std::string& value, const std::string& currency) {
        // Store the account summary data in our manager
        m_accountSummaryManager.updateAccountSummary(reqId, account, tag, value, currency);
        
        // Log important account values (compact format)
        if (tag == "NetLiquidation" || tag == "BuyingPower" || tag == "AvailableFunds" || tag == "ExcessLiquidity") {
            std::cout << "[Account] " << account << " " << tag << ": " << value << " " << currency << std::endl;
            return;
        }
        
        // For other tags, use more detailed logging
        std::cout << "[AccountSummary] reqId:" << reqId 
                  << " account:" << account
                  << " tag:" << tag
                  << " value:" << value
                  << " currency:" << currency << std::endl;
    }
    
//     void IBKRTrader::accountSummaryEnd(int reqId) {
//         std::cout << "[AccountSummaryEnd] reqId:" << reqId << " - Account summary data complete" << std::endl;
        
//         // If this is an old request that's finally completing, cancel it to ensure clean state
//         if (reqId < m_lastAccountSummaryReqId && reqId > 9000) {
//             std::cout << "[INFO] Cleaning up old account summary request: " << reqId << std::endl;
//             m_client->cancelAccountSummary(reqId);
//         }
//     }
    
//     void IBKRTrader::position(const std::string& account, const Contract& contract, 
//                             Decimal position, double avgCost) {
//         std::cout << "\n===== POSITION DATA =====\n";
//         std::cout << "account: " << account << "\n";
//         std::cout << "symbol: " << contract.symbol << "\n";
//         std::cout << "secType: " << contract.secType << "\n";
//         std::cout << "currency: " << contract.currency << "\n";
//         std::cout << "exchange: " << contract.exchange << "\n";
//         std::cout << "position: " << static_cast<double>(position) << "\n";
//         std::cout << "avgCost: " << avgCost << "\n";
//         std::cout << "=======================\n";
        
//         std::cout << "[IBKRTrader::position] Account: " << account
//                  << ", Symbol: " << contract.symbol
//                  << ", Position: " << static_cast<double>(position)
//                  << ", AvgCost: " << avgCost << std::endl;
//     }
    
//     void IBKRTrader::positionEnd() {
//         std::cout << "[IBKRTrader::positionEnd] All positions received" << std::endl;
//     }
    
//     void IBKRTrader::pnl(int reqId, double dailyPnL, double unrealizedPnL, double realizedPnL) {
//         std::cout << "\n===== PNL DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "dailyPnL: " << dailyPnL << "\n";
//         std::cout << "unrealizedPnL: " << unrealizedPnL << "\n";
//         std::cout << "realizedPnL: " << realizedPnL << "\n";
//         std::cout << "===================\n";
        
//         std::cout << "[IBKRTrader::pnl] Daily PnL: " << dailyPnL
//                  << ", Unrealized: " << unrealizedPnL
//                  << ", Realized: " << realizedPnL << std::endl;
//     }
    
//     void IBKRTrader::pnlSingle(int reqId, Decimal pos, double dailyPnL, double unrealizedPnL, 
//                              double realizedPnL, double value) {
//         std::cout << "\n===== SINGLE POSITION PNL DATA =====\n";
//         std::cout << "reqId: " << reqId << "\n";
//         std::cout << "position: " << static_cast<double>(pos) << "\n";
//         std::cout << "dailyPnL: " << dailyPnL << "\n";
//         std::cout << "unrealizedPnL: " << unrealizedPnL << "\n";
//         std::cout << "realizedPnL: " << realizedPnL << "\n";
//         std::cout << "position value: " << value << "\n";
//         std::cout << "==================================\n";
        
//         std::cout << "[IBKRTrader::pnlSingle] Position: " << static_cast<double>(pos)
//                  << ", Daily PnL: " << dailyPnL
//                  << ", Unrealized: " << unrealizedPnL
//                  << ", Realized: " << realizedPnL
//                  << ", Value: " << value << std::endl;
//     }
    
//     void IBKRTrader::updateAccountValue(const std::string& key, const std::string& val, 
//                                       const std::string& currency, const std::string& accountName) {
//         std::cout << "\n===== ACCOUNT VALUE UPDATE =====\n";
//         std::cout << "account: " << accountName << "\n";
//         std::cout << "key: " << key << "\n";
//         std::cout << "value: " << val << "\n";
//         std::cout << "currency: " << currency << "\n";
//         std::cout << "==============================\n";
        
//         std::cout << "[IBKRTrader::updateAccountValue] Account: " << accountName
//                  << ", Key: " << key
//                  << ", Value: " << val
//                  << ", Currency: " << currency << std::endl;
//     }
    
//     void IBKRTrader::updatePortfolio(const Contract& contract, Decimal position, double marketPrice,
//                                    double marketValue, double averageCost, double unrealizedPNL,
//                                    double realizedPNL, const std::string& accountName) {
//         std::cout << "\n===== PORTFOLIO UPDATE =====\n";
//         std::cout << "account: " << accountName << "\n";
//         std::cout << "symbol: " << contract.symbol << "\n";
//         std::cout << "secType: " << contract.secType << "\n";
//         std::cout << "position: " << static_cast<double>(position) << "\n";
//         std::cout << "marketPrice: " << marketPrice << "\n";
//         std::cout << "marketValue: " << marketValue << "\n";
//         std::cout << "averageCost: " << averageCost << "\n";
//         std::cout << "unrealizedPNL: " << unrealizedPNL << "\n";
//         std::cout << "realizedPNL: " << realizedPNL << "\n";
//         std::cout << "==========================\n";
        
//         std::cout << "[IBKRTrader::updatePortfolio] Account: " << accountName
//                  << ", Symbol: " << contract.symbol
//                  << ", Position: " << static_cast<double>(position)
//                  << ", Market Value: " << marketValue
//                  << ", Unrealized PnL: " << unrealizedPNL << std::endl;
//     }
    
//     void IBKRTrader::updateAccountTime(const std::string& timeStamp) {
//         std::cout << "[IBKRTrader::updateAccountTime] Time: " << timeStamp << std::endl;
//     }
    
//     void IBKRTrader::accountDownloadEnd(const std::string& accountName) {
//         std::cout << "[IBKRTrader::accountDownloadEnd] Account: " << accountName << std::endl;
//     }

    

//     // Cancel any active account summary requests
//     void IBKRTrader::cancelAccountSummaryRequests() {
//         if (!m_client || !m_client->isConnected()) {
//             std::cerr << "[ERROR] Cannot cancel account summary requests: not connected" << std::endl;
//             return;
//         }
        
//         if (m_lastAccountSummaryReqId > 9000) {
//             std::cout << "[INFO] Canceling account summary request with reqId: " 
//                       << m_lastAccountSummaryReqId << std::endl;
//             m_client->cancelAccountSummary(m_lastAccountSummaryReqId);
            
//             // Give the cancellation a moment to process
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         }
//     }

//     ///////////////////////////////////////////////////////////////////////////
//     // TICK-BY-TICK BID/ASK CALLBACK
//     // Processes detailed bid/ask updates with timestamps
//     /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//     void IBKRTrader::tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, 
//                                    Decimal bidSize, Decimal askSize, 
//                                    const TickAttribBidAsk& tickAttribBidAsk) {
//         // Log compact bid/ask update
//         std::cout << "[BidAsk] ID:" << reqId 
//                   << " Time:" << time
//                   << " Bid:" << bidPrice << "x" << static_cast<double>(bidSize)
//                   << " Ask:" << askPrice << "x" << static_cast<double>(askSize) << std::endl;
        
//         // If we have a ModelManager, process the data
//         if (m_modelManager && reqId == m_requestId) {
//             // Create timestamp from the provided time_t
//             uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
//             // Route complete bid/ask data through our central point
//             routeTickToModelManager(
//                 0, // price (no last price in this update)
//                 0, // volume
//                 timestamp,
//                 bidPrice,
//                 askPrice,
//                 static_cast<double>(bidSize),
//                 static_cast<double>(askSize),
//                 "", // exchange not provided in this callback
//                 "" // no special conditions
//             );
//         }
//     }

} // namespace connection