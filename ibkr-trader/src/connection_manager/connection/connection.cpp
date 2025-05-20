#include "connection.hpp"
#include "../../models/model_manager.hpp"  // Include ModelManager for direct access
#include <iostream>
#include <chrono>
#include <sstream>

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
        , m_client(new EClientSocket(this, &m_osSignal))
        , m_modelManager(nullptr)
        , m_requestId(-1)
        , m_lastPrice(0.0)
    {
    }
    
    // Destructor implementation
    IBKRTrader::~IBKRTrader() {
        delete m_client;
    }
    
    // Connect to IBKR Gateway
    bool IBKRTrader::connect(int clientId) {
        // Set the connection options to enable API extensions
        m_client->setConnectOptions("+PACEAPI");
        
        // Use provided client ID or fall back to default
        int actualClientId = (clientId >= 0) ? clientId : client_id;
        
        bool success = m_client->eConnect(HOST, PORT, actualClientId, /*extraAuth=*/false);
        if (success) {
            std::cout << "[INFO] Connection initiated to " << HOST << ":" << PORT 
                      << " with client ID: " << actualClientId << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to connect to IBKR." << std::endl;
        }
        return success;
    }
    
    // Set the model manager for direct market data routing
    void IBKRTrader::setModelManager(model_manager::ModelManager* modelManager, const std::string& symbol) {
        m_modelManager = modelManager;
        m_symbol = symbol;
        std::cout << "[IBKRTrader] Set ModelManager for symbol: " << m_symbol << std::endl;
    }
    
    // Disconnect from IBKR Gateway
    void IBKRTrader::disconnect() {
        m_client->eDisconnect();
        std::cout << "[INFO] Disconnected from IBKR.\n";
    }
    
    //  createReader() is responsible for making a reader for retrieving messages from the IBKR connection, std::unique_ptr<EReader> (smart pointer that automatically manages memory
    std::unique_ptr<EReader> IBKRTrader::createReader() {
        std::unique_ptr<EReader> reader(new EReader(m_client, &m_osSignal));
        reader->start();
        return reader;
    }
    
    // Start message processing in a separate thread
    std::thread IBKRTrader::startMessageProcessing(std::unique_ptr<EReader>& reader) {
        return std::thread([this, &reader]() {
            while (m_client->isConnected()) {
                m_osSignal.waitForSignal(); // waits for a notification that messages are available
                reader->processMsgs(); // processes any received messages
            }
        });
    }
    
    // Send a ping and return the request ID
    int IBKRTrader::sendPing() {
        std::lock_guard<std::mutex> lock(m_pingMutex);
        int pingId = m_nextPingId++;
        
        // Record the time we sent the request
        m_pingRequests[pingId] = std::chrono::high_resolution_clock::now();
        
        // Use reqCurrentTime as our ping mechanism
        m_client->reqCurrentTime();
        
        return pingId;
    }
    
    // Measure average latency over multiple pings
    double IBKRTrader::measureAverageLatency(int numPings) {
        double totalLatency = 0;
        int successfulPings = 0;
        
        for (int i = 0; i < numPings; i++) {
            sendPing();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Wait for responses
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Calculate average from completed pings (placeholder example)
        std::lock_guard<std::mutex> lock(m_pingMutex);
        // In a real scenario, you'd track each ping's round-trip precisely.
        return successfulPings > 0 ? totalLatency / successfulPings : -1.0;
    }

    /***************************************************
     * EWrapper overrides for connection handling
     ***************************************************/

    void IBKRTrader::nextValidId(OrderId orderId) {
        std::cout << "[INFO] nextValidId: " << orderId << std::endl;
        m_nextOrderId = orderId;
    }

    void IBKRTrader::error(int id, long errorTime, int errorCode,
                           const std::string& errorString,
                           const std::string& advancedOrderRejectJson) {
        std::cerr << "[ERROR] ReqId: " << id 
                  << " Code: " << errorCode 
                  << " Msg: " << errorString << std::endl;
                  
        // If we have a ModelManager, log symbol-specific errors
        if (m_modelManager && id == m_requestId) {
            std::cerr << "[ERROR] Error for symbol " << m_symbol << ": " << errorString << std::endl;
        }
    }

    void IBKRTrader::connectAck() {
        std::cout << "[INFO] connectAck: Connected to IBKR" << std::endl;
    }

    void IBKRTrader::connectionClosed() {
        std::cout << "[INFO] Connection closed." << std::endl;
    }

    // This is called when server responds to reqCurrentTime() (for pings)
    void IBKRTrader::currentTime(long time) {
        auto now = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> lock(m_pingMutex);
        
        if (!m_pingRequests.empty()) {
            auto it = m_pingRequests.begin(); // FIFO
            auto sendTime = it->second;
            auto latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(now - sendTime).count();
            double latencyMs = latencyUs / 1000.0;

            std::cout << "[Ping] latency: " << latencyMs << " ms (Server time: " << time << ")" << std::endl;
            
            m_pingRequests.erase(it);
        }
    }

    // Route tick market data to ModelManager
    void IBKRTrader::routeTickToModelManager(double price, double volume, uint64_t timestamp) {
        // Only process if we have a ModelManager
        if (!m_modelManager) {
            return;
        }
        
        // If timestamp is 0, use current time
        if (timestamp == 0) {
            timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        }
        
        // Note large volume values but preserve them in scientific notation
        if (volume > 1.0e10) {  // If volume is unrealistically large (>10 billion)
            std::cout << "[IBKRTrader] Note: Extremely large volume detected: " << std::scientific 
                      << volume << std::fixed << " (preserved as-is)" << std::endl;
            // We don't cap the volume anymore - we preserve the original value
            // These are real values from the IBKR API that might be unrealistic
            // but are kept for accuracy
        }
        
        // Create a MarketDataTick object
        raw_data_model::MarketDataTick tick;
        tick.price = price;
        tick.volume = volume;
        tick.timestamp = timestamp;
        
        // Get thread ID for logging
        std::stringstream threadIdStr;
        threadIdStr << std::this_thread::get_id();
        
        // Log the tick with thread ID
        std::cout << "[IBKRTrader][ThreadID: " << threadIdStr.str() << "][Symbol: " << m_symbol << "] "
                  << "Routing tick - Price: " << price << ", Volume: " << (volume > 1.0e10 ? std::scientific : std::fixed) 
                  << volume << std::fixed << std::endl;
        
        // Add directly to ModelManager
        m_modelManager->addTick(tick);
    }

    // Tick data callbacks now route directly to ModelManager
    void IBKRTrader::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
        std::cout << "[IBKRTrader::tickPrice] Raw callback - ID: " << tickerId 
                  << ", Field: " << static_cast<int>(field) 
                  << ", Price: " << price 
                  << ", TickAttrib: " << (attrib.canAutoExecute ? "autoexec" : "non-eligible") 
                  << std::endl;
        
        // Check if this is for our ModelManager's request ID
        if (m_modelManager && tickerId == m_requestId) {
            std::string fieldName;
            
            switch (field) {
                // Regular market data fields
                case TickType::BID: fieldName = "BID"; break;
                case TickType::ASK: fieldName = "ASK"; break;
                case TickType::LAST: fieldName = "LAST"; break;
                case TickType::HIGH: fieldName = "HIGH"; break;
                case TickType::LOW: fieldName = "LOW"; break;
                case TickType::CLOSE: fieldName = "CLOSE"; break;
                case TickType::OPEN: fieldName = "OPEN"; break;
                
                // Delayed market data fields
                case TickType::DELAYED_BID: fieldName = "DELAYED_BID"; break;
                case TickType::DELAYED_ASK: fieldName = "DELAYED_ASK"; break;
                case TickType::DELAYED_LAST: fieldName = "DELAYED_LAST"; break;
                case TickType::DELAYED_HIGH: fieldName = "DELAYED_HIGH"; break;
                case TickType::DELAYED_LOW: fieldName = "DELAYED_LOW"; break;
                case TickType::DELAYED_CLOSE: fieldName = "DELAYED_CLOSE"; break;
                case TickType::DELAYED_OPEN: fieldName = "DELAYED_OPEN"; break;
                
                default: fieldName = "UNKNOWN_" + std::to_string(field); break;
            }
            
            // Store the last price we've seen
            m_lastPrice = price;
            
            // If it's a price we care about (including delayed versions), route it to the model manager
            if (field == TickType::LAST || field == TickType::BID || field == TickType::ASK ||
                field == TickType::DELAYED_LAST || field == TickType::DELAYED_BID || field == TickType::DELAYED_ASK) {
                std::cout << "[IBKRTrader] Routing " << fieldName << " price to ModelManager for " << m_symbol << std::endl;
                routeTickToModelManager(price, 0);  // Zero volume for price-only updates
            }
        }
    }

    void IBKRTrader::tickSize(TickerId tickerId, TickType field, Decimal size) {
        std::cout << "[IBKRTrader::tickSize] Raw callback - ID: " << tickerId 
                  << ", Field: " << static_cast<int>(field)
                  << ", Size: " << static_cast<double>(size) 
                  << std::endl;
        
        // Check if this is for our ModelManager's request ID
        if (m_modelManager && tickerId == m_requestId) {
            std::string fieldName;
            double sizeValue = static_cast<double>(size);
            
            switch (field) {
                // Regular market data size fields
                case TickType::BID_SIZE: fieldName = "BID_SIZE"; break;
                case TickType::ASK_SIZE: fieldName = "ASK_SIZE"; break;
                case TickType::LAST_SIZE: fieldName = "LAST_SIZE"; break;
                case TickType::VOLUME: fieldName = "VOLUME"; break;
                
                // Delayed market data size fields
                case TickType::DELAYED_BID_SIZE: fieldName = "DELAYED_BID_SIZE"; break;
                case TickType::DELAYED_ASK_SIZE: fieldName = "DELAYED_ASK_SIZE"; break;
                case TickType::DELAYED_LAST_SIZE: fieldName = "DELAYED_LAST_SIZE"; break;
                case TickType::DELAYED_VOLUME: fieldName = "DELAYED_VOLUME"; break;
                
                default: fieldName = "UNKNOWN_SIZE_" + std::to_string(field); break;
            }
            
            // Process both regular and delayed volume ticks with the last price
            if ((field == TickType::VOLUME || field == TickType::DELAYED_VOLUME) && m_lastPrice > 0) {
                std::cout << "[IBKRTrader] Routing " << fieldName << " to ModelManager for " << m_symbol << std::endl;
                routeTickToModelManager(m_lastPrice, sizeValue);
            }
            
            // Process both regular and delayed last_size ticks with the last price
            if ((field == TickType::LAST_SIZE || field == TickType::DELAYED_LAST_SIZE) && m_lastPrice > 0) {
                std::cout << "[IBKRTrader] Routing " << fieldName << " to ModelManager for " << m_symbol << std::endl;
                routeTickToModelManager(m_lastPrice, sizeValue);
            }
        }
    }

    void IBKRTrader::tickString(TickerId tickerId, TickType field, const std::string& value) {
        std::cout << "[IBKRTrader::tickString] Raw callback - ID: " << tickerId
                  << ", Field: " << static_cast<int>(field)
                  << ", Value: " << value << std::endl;
    }

    void IBKRTrader::tickGeneric(TickerId tickerId, TickType field, double value) {
        std::cout << "[IBKRTrader::tickGeneric] Raw callback - ID: " << tickerId
                  << ", Field: " << static_cast<int>(field)
                  << ", Value: " << value << std::endl;
    }

    void IBKRTrader::tickByTickAllLast(int reqId, int tickType, time_t time, double price, 
                                    Decimal size, const TickAttribLast& tickAttribLast, 
                                    const std::string& exchange, const std::string& specialConditions) {
        std::cout << "[IBKRTrader::tickByTickAllLast] Raw callback - ID: " << reqId
                 << ", Type: " << tickType
                 << ", Exchange: " << exchange
                 << ", Price: " << price
                 << ", Size: " << static_cast<double>(size) << std::endl;
                 
        // Check if this is for our ModelManager's request ID
        if (m_modelManager && reqId == m_requestId) {
            // Store the last price we've seen
            m_lastPrice = price;
            
            // Create timestamp from the provided time_t
            uint64_t timestamp = static_cast<uint64_t>(time) * 1000; // Convert to ms
            
            // Tick-by-tick data is most granular and includes both price and size
            routeTickToModelManager(price, static_cast<double>(size), timestamp);
        }
    }

    // Implement this if you suspect your connection handling isn't set up correctly
    void IBKRTrader::managedAccounts(const std::string& accountsList) {
        std::cout << "[INFO] Managed accounts: " << accountsList << std::endl;
    }

} // namespace connection