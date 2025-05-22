// raw_data_model.hpp
#ifndef RAW_DATA_MODEL_HPP
#define RAW_DATA_MODEL_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <mutex>
#include "../../util/stk_q/stk_q.hpp"
#include "../metrics_model/stock_data_tick.hpp"

namespace raw_data_model {

// // Structure for a single market data tick from IBKR
// struct MarketDataTick {
//     double price;
//     double volume;  // Changed from int to double to handle extremely large volume values from IBKR API
//     uint64_t timestamp;
//     // Add other tick data fields as needed
// };

// Trading parameters structure matching the JSON format
struct TradingParams {
    int lots;
    std::string margin;
    std::string stopLoss;
    int maxTrades;
    int lossThreshold;
    int winThreshold;
    std::string minWinRate;
    int maxHoldSeconds;
};

// Class for a single symbol's data
class RawDataModel {
private:
    // Symbol this model is for
    std::string m_symbol;
    
    // Trading parameters from user input
    TradingParams m_params;
    
    // Status and timestamp
    std::string m_status;
    uint64_t m_timestamp;
    
    // Thread-safe queue for stock data
    // This allows efficient processing of real-time market data
    std::unique_ptr<stk_q::STK_Q> m_stockQueue;

    // Stock data
    std::unique_ptr<stock_data_tick::StockData> m_stockData;
    
    // Mutex for thread safety
    mutable std::mutex m_mutex;

public:
    // Constructor
    RawDataModel(const std::string& symbol);
    
    // Initialize from JSON input
    bool initFromJson(const nlohmann::json& jsonData);
    
    // Add stock data to the queue
    void addTick(const stock_data_tick::StockData& stockData);
    
    // Convert StockData to STK_Q_Data
    stk_q::STK_Q_Data convertTickToQueueData(const stock_data_tick::StockData& stockData) const;
    
    // Stock queue operations
    void pushToQueue(stk_q::STK_Q_Data& data);
    void pushToQueue(stk_q::STK_Q_Data&& data);
    bool popFromQueue(stk_q::STK_Q_Data& outData);
    
    // Get the stock queue for advanced operations
    stk_q::STK_Q* getStockQueue() const;
    
    // Get the symbol
    std::string getSymbol() const;
    
    // Get trading parameters
    const TradingParams& getParams() const;
    
    // Get the latest tick from queue (more accurate with current design)
    bool getLatestTickFromQueue(stock_data_tick::StockData& outTick) const;
    
    // Get queue size
    size_t getQueueSize() const;
    
    // Clear all ticks (keep trading parameters)
    void clearTicks();
    
    // Clear the queue
    void clearQueue();
    
    // Get the stock data
    stock_data_tick::StockData* getStockData() const;
};

// Singleton manager that handles creation and access to individual models
class RawDataModelManager {
private:
    // Private constructor
    RawDataModelManager() = default;
    
    // Map of symbol -> model
    std::map<std::string, std::shared_ptr<RawDataModel>> m_models;
    
    // Mutex for thread safety
    mutable std::mutex m_mutex;
    
    // Static singleton instance
    static std::unique_ptr<RawDataModelManager> s_instance;
    static std::mutex s_instanceMutex;

public:
    // Deleted copy/move constructors and assignments
    RawDataModelManager(const RawDataModelManager&) = delete;
    RawDataModelManager(RawDataModelManager&&) = delete;
    RawDataModelManager& operator=(const RawDataModelManager&) = delete;
    RawDataModelManager& operator=(RawDataModelManager&&) = delete;
    
    // Destructor
    ~RawDataModelManager() = default;
    
    // Get singleton instance
    static RawDataModelManager& getInstance();
    
    // Get or create a model for a symbol
    std::shared_ptr<RawDataModel> getModel(const std::string& symbol);
    
    // Initialize a model from JSON
    bool initModelFromJson(const nlohmann::json& jsonData);
    
    // Add a tick to a model identified by symbol
    // This is a high-level manager method that:
    // 1. First gets or creates the model for the given symbol
    // 2. Then calls RawDataModel::addTick() on that model
    // Returns true if successful, false if the model couldn't be created
    // Use this when you only have a symbol name and not a model reference
    bool addTickToModel(const std::string& symbol, const stock_data_tick::StockData& stockData);
    
    // Check if a model exists
    bool hasModel(const std::string& symbol);
    
    // Remove a model
    bool removeModel(const std::string& symbol);
    
    // Get all symbols
    std::vector<std::string> getAllSymbols() const;
    
    // Clear all models
    void clearAll();
};

} // namespace raw_data_model

#endif