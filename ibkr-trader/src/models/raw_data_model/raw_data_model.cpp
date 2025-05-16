// raw_data_model.cpp
#include "raw_data_model.hpp"

namespace raw_data_model {

//
// RawDataModel implementation
//

RawDataModel::RawDataModel(const std::string& symbol) 
    : m_symbol(symbol), m_timestamp(0) {
}

bool RawDataModel::initFromJson(const nlohmann::json& jsonData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    try {
        // Get the entry for this symbol
        const auto& entry = jsonData[m_symbol];
        
        // Extract status and timestamp
        m_status = entry["status"];
        m_timestamp = entry["timestamp"];
        
        // Extract parameters
        const auto& params = entry["params"];
        m_params.lots = params["lots"];
        m_params.margin = params["margin"];
        m_params.stopLoss = params["stopLoss"];
        m_params.maxTrades = params["maxTrades"];
        m_params.lossThreshold = params["lossThreshold"];
        m_params.winThreshold = params["winThreshold"];
        m_params.minWinRate = params["minWinRate"];
        m_params.maxHoldSeconds = params["maxHoldSeconds"];
        
        return true;
    }
    catch (const std::exception& e) {
        // Handle parsing errors
        return false;
    }
}

void RawDataModel::addTick(const MarketDataTick& tick) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ticks.push_back(tick);
}

std::string RawDataModel::getSymbol() const {
    return m_symbol;
}

const TradingParams& RawDataModel::getParams() const {
    return m_params;
}

const std::vector<MarketDataTick>& RawDataModel::getTicks() const {
    return m_ticks;
}

const MarketDataTick* RawDataModel::getLatestTick() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ticks.empty()) {
        return nullptr;
    }
    return &m_ticks.back();
}

size_t RawDataModel::getTickCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ticks.size();
}

void RawDataModel::clearTicks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ticks.clear();
}

//
// RawDataModelManager implementation
//

// Initialize static members
std::unique_ptr<RawDataModelManager> RawDataModelManager::s_instance = nullptr;
std::mutex RawDataModelManager::s_instanceMutex;

// Get singleton instance
RawDataModelManager& RawDataModelManager::getInstance() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance) {
        s_instance = std::unique_ptr<RawDataModelManager>(new RawDataModelManager());
    }
    return *s_instance;
}

std::shared_ptr<RawDataModel> RawDataModelManager::getModel(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_models.find(symbol);
    if (it != m_models.end()) {
        return it->second;
    }
    
    // Create new model if it doesn't exist
    auto model = std::make_shared<RawDataModel>(symbol);
    m_models[symbol] = model;
    return model;
}

bool RawDataModelManager::initModelFromJson(const nlohmann::json& jsonData) {
    try {
        // Extract the symbol from the JSON
        if (jsonData.size() != 1) {
            return false; // Expecting exactly one symbol
        }
        
        std::string symbol = jsonData.begin().key();
        
        // Get or create the model
        auto model = getModel(symbol);
        
        // Initialize it
        return model->initFromJson(jsonData);
    }
    catch (const std::exception& e) {
        return false;
    }
}

bool RawDataModelManager::addTickToModel(const std::string& symbol, const MarketDataTick& tick) {
    auto model = getModel(symbol);
    if (model) {
        model->addTick(tick);
        return true;
    }
    return false;
}

bool RawDataModelManager::hasModel(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_models.find(symbol) != m_models.end();
}

bool RawDataModelManager::removeModel(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_models.erase(symbol) > 0;
}

std::vector<std::string> RawDataModelManager::getAllSymbols() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> symbols;
    for (const auto& pair : m_models) {
        symbols.push_back(pair.first);
    }
    return symbols;
}

void RawDataModelManager::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_models.clear();
}

} // namespace raw_data_model