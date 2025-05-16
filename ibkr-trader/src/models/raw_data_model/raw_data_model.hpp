// raw_data_model.hpp
#ifndef RAW_DATA_MODEL_HPP
#define RAW_DATA_MODEL_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <mutex>

namespace raw_data_model {

// Structure for a single market data tick from IBKR
struct MarketDataTick {
    double price;
    int volume;
    uint64_t timestamp;
    // Add other tick data fields as needed
};

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
    
    // Vector of market data ticks
    std::vector<MarketDataTick> m_ticks;
    
    // Mutex for thread safety
    std::mutex m_mutex;

public:
    // Constructor
    RawDataModel(const std::string& symbol);
    
    // Initialize from JSON input
    bool initFromJson(const nlohmann::json& jsonData);
    
    // Add a market data tick
    void addTick(const MarketDataTick& tick);
    
    // Get the symbol
    std::string getSymbol() const;
    
    // Get trading parameters
    const TradingParams& getParams() const;
    
    // Get all ticks
    const std::vector<MarketDataTick>& getTicks() const;
    
    // Get the latest tick (or nullptr if none)
    const MarketDataTick* getLatestTick() const;
    
    // Get number of ticks
    size_t getTickCount() const;
    
    // Clear all ticks (keep trading parameters)
    void clearTicks();
};

// Singleton manager that handles creation and access to individual models
class RawDataModelManager {
private:
    // Private constructor
    RawDataModelManager() = default;
    
    // Map of symbol -> model
    std::map<std::string, std::shared_ptr<RawDataModel>> m_models;
    
    // Mutex for thread safety
    std::mutex m_mutex;
    
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
    
    // Add a tick to a model
    bool addTickToModel(const std::string& symbol, const MarketDataTick& tick);
    
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