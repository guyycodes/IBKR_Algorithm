// raw_data_model.cpp
#include "raw_data_model.hpp"

namespace raw_data_model {

//
// RawDataModel implementation
//

RawDataModel::RawDataModel(const std::string& symbol)
    : m_symbol(symbol) {
    // Initialize trading parameters with default values
    m_params = {};
    m_status = "initialized";
    m_timestamp = 0;
    
    // Initialize the stock queue
    m_stockQueue = std::make_unique<stk_q::STK_Q>();
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

// Convert MarketDataTick to STK_Q_Data
stk_q::STK_Q_Data RawDataModel::convertTickToQueueData(const MarketDataTick& tick) const {
    stk_q::STK_Q_Data queueData;
    queueData.symbol = m_symbol;
    queueData.price = tick.price;
    queueData.time = static_cast<long>(tick.timestamp);
    queueData.size = tick.volume;
    queueData.exchange = ""; // Can be populated with actual exchange if available
    return queueData;
}

// Add a market data tick - updates the queue only
// This is the core instance method that adds data directly to this model's queue
void RawDataModel::addTick(const MarketDataTick& tick) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // ********************************************************************
    // IMPORTANT DESIGN DECISION:
    // Ticks are stored ONLY in the queue (STK_Q).
    // The ModelManager will prune the queue based on time window settings.
    // ********************************************************************
    
    // Store data in the queue
    if (m_stockQueue) {
        stk_q::STK_Q_Data queueData = convertTickToQueueData(tick);
        m_stockQueue->push(std::move(queueData));
    }
}

// Queue operations
void RawDataModel::pushToQueue(stk_q::STK_Q_Data& data) {
    if (m_stockQueue) {
        m_stockQueue->push(data);
    }
}

void RawDataModel::pushToQueue(stk_q::STK_Q_Data&& data) {
    if (m_stockQueue) {
        m_stockQueue->push(std::move(data));
    }
}

bool RawDataModel::popFromQueue(stk_q::STK_Q_Data& outData) {
    if (m_stockQueue) {
        return m_stockQueue->pop(outData);
    }
    return false;
}

stk_q::STK_Q* RawDataModel::getStockQueue() const {
    return m_stockQueue.get();
}

size_t RawDataModel::getQueueSize() const {
    if (m_stockQueue) {
        return m_stockQueue->size();
    }
    return 0;
}

void RawDataModel::clearQueue() {
    if (m_stockQueue) {
        m_stockQueue->clear();
    }
}

std::string RawDataModel::getSymbol() const {
    return m_symbol;
}

const TradingParams& RawDataModel::getParams() const {
    return m_params;
}


void RawDataModel::clearTicks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Note: This method is kept for backward compatibility
    // It now only clears the queue
    if (m_stockQueue) {
        m_stockQueue->clear();
    }
}

// Get the most recent tick directly from the queue (more accurate with current design)
bool RawDataModel::getLatestTickFromQueue(MarketDataTick& outTick) const {
    if (!m_stockQueue || m_stockQueue->empty()) {
        return false;
    }
    
    stk_q::STK_Q_Data data;
    if (m_stockQueue->peek(data)) {
        // Convert queue data to tick format
        outTick.price = data.price;
        outTick.volume = data.size;
        outTick.timestamp = data.time;
        return true;
    }
    
    return false;
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

// Manager method to add a tick to a model identified by symbol
// This is a convenience method that handles model lookup/creation
bool RawDataModelManager::addTickToModel(const std::string& symbol, const MarketDataTick& tick) {
    auto model = getModel(symbol);
    if (model) {
        model->addTick(tick);  // Delegates to the instance method
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