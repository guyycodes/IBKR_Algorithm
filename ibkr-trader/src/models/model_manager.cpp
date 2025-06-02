// ───────────────────────────────────────────────────────────────────────────────
//  model_manager.cpp
// ───────────────────────────────────────────────────────────────────────────────

#include "models/model_manager.hpp"
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <sstream>
#include <numeric>
#include <algorithm>

using namespace std::chrono_literals;
namespace model_manager {

// ────────────────────────────────────────────────────────────────────────────────
// Thread logging utility - same as InputManager for consistency
// ────────────────────────────────────────────────────────────────────────────────
void logThreadRole(const std::string& role, const std::string& action) {
    auto tid = std::this_thread::get_id();
    std::ostringstream ss;
    ss << tid;
    std::cout << "[" << role << ":" << ss.str() << "] " << action << std::endl;
}

// ────────────────────────────────────────────────────────────────────────────────
//  ctor / dtor
// ────────────────────────────────────────────────────────────────────────────────
ModelManager::ModelManager(std::string sym,
                           std::size_t windowSize,
                           TimeWindowUnit unit)
    : m_symbol(std::move(sym)),
      m_windowSize(windowSize),
      m_windowUnit(unit),
      m_lastPrune(std::chrono::steady_clock::now())
      // m_toBuffer(windowToDuration()),
      // m_rbHandler(&m_toBuffer)
{
    m_rawModel   = std::make_unique<raw_data_model::RawDataModel>(m_symbol);
    // m_volProfile = std::make_unique<VolumeProfileMap>(0.05);
    // m_toBuffer   = TimeOrderedTickBuffer(windowToDuration().count());
    // m_rbHandler  = std::make_unique<RingBufferTradeHandler>(
    //                 m_toBuffer, *m_volProfile, *m_rawModel);
    m_connMgr    = std::make_unique<connection_manager::ConnectionManager>();
    logThreadRole("ModelManager", "Creating ModelManager for " + m_symbol);
}

ModelManager::~ModelManager() { 
    disconnectFromIBKR(); 
    logThreadRole("ModelManager", "ModelManager destroyed for " + m_symbol);
}

// ────────────────────────────────────────────────────────────────────────────────
//  IBKR connectivity (no blocking loops; retry handled by caller)
// ────────────────────────────────────────────────────────────────────────────────
bool ModelManager::connectToIBKR(){
    std::scoped_lock lg{m_mx};
    if (m_connected) return true;
    if (m_connectionAttempts >= MAX_CONN_ATTEMPTS) return false;

    if (m_connectionAttempts>0 &&
       (std::chrono::steady_clock::now()-m_lastAttempt)<std::chrono::seconds(RETRY_DELAY_SECONDS))
        return false;                                   // back-off

    ++m_connectionAttempts;
    m_lastAttempt = std::chrono::steady_clock::now();

    // build IBKR contract
    Contract c; 
    c.symbol = getSymbol(); 
    c.secType = "STK"; 
    c.exchange = "SMART"; 
    c.currency = "USD";
    
    std::random_device rd; 
    std::mt19937 gen(rd());
    int clientId = (std::accumulate(c.symbol.begin(),c.symbol.end(),0)+1000)%9000+1000;

    logThreadRole("ModelManager", "Connecting to IBKR for " + getSymbol());
    
    if (!m_connMgr->connect(clientId, getSymbol(), c)) {
        logThreadRole("ModelManager", "Connection failed for " + getSymbol());
        return false;
    }
    
    m_reqId = std::uniform_int_distribution<int>(10'000,99'999)(gen);
    auto& trader = m_connMgr->trader();
    auto cli = trader.getClient();
    if (cli) {
        cli->setServerLogLevel(5); 
        cli->reqMarketDataType(1);
    }
    trader.setModelManager(this, getSymbol());
    
    // Make the main market data request (like legacy code) using m_reqId
    if (cli) {
        std::cout << "[ModelManager] Making main market data request for " << getSymbol() 
                  << " with requestId=" << m_reqId << "\n";
        cli->reqMktData(m_reqId, c, "233,232,221", false, false, {});
    }
    
    // Wait 1 second after reqMktData (like legacy code)
    std::cout << "[ModelManager] Waiting after market data request...\n";
    std::this_thread::sleep_for(1s);
    
    // Now start supplementary data streams
    trader.startDataStream(getSymbol());
    
    std::this_thread::sleep_for(2s);
    if (!trader.isConnected()) { 
        m_connMgr->disconnect(); 
        return false; 
    }

    m_connectionAttempts = 0;
    m_connected = true;
    logThreadRole("ModelManager", "IBKR connected for " + getSymbol());
    return true;
}

void ModelManager::disconnectFromIBKR(){
    std::scoped_lock lg{m_mx};
    if (!m_connected) return;
    
    logThreadRole("ModelManager", "Disconnecting from IBKR for " + getSymbol());
    
    if (m_reqId >= 0 && m_connMgr->isConnected()) {
        auto& trader = m_connMgr->trader();
        auto cli = trader.getClient();
        if (cli) {
            cli->cancelMktData(m_reqId);
        }
    }
    m_connMgr->disconnect();
    m_connected = false; 
    m_reqId = -1;
}

std::string ModelManager::getConnectionStatus() const {
    return m_connected ? "CONNECTED" : "NOT CONNECTED";
}

// ────────────────────────────────────────────────────────────────────────────────
//  time-window helpers
// ────────────────────────────────────────────────────────────────────────────────
std::chrono::milliseconds ModelManager::windowToDuration() const {
    using namespace std::chrono;
    switch (m_windowUnit){
        case TimeWindowUnit::SECONDS: return milliseconds(m_windowSize*1'000);
        case TimeWindowUnit::MINUTES: return milliseconds(m_windowSize*60'000);
        case TimeWindowUnit::HOURS  : return milliseconds(m_windowSize*3'600'000);
    }
    return milliseconds(0);
}

void ModelManager::pruneOldData(){
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastPrune < 5s) return;        // prune at most every 5 s
    m_lastPrune = now;
    
    // STUB: Just log that we're pruning
    std::cout << "[ModelManager] STUB: Pruning old data for " << getSymbol() << '\n';
    
    /* ORIGINAL COMMENTED OUT:
    auto* q = m_rawModel->getStockQueue();
    if (!q) return;
    auto cutoff = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                - windowToDuration();
    q->removeOlderThan(cutoff.count());
    */
}

// ────────────────────────────────────────────────────────────────────────────────
//  queue processing (called in AppState worker loop)
// ────────────────────────────────────────────────────────────────────────────────
std::size_t ModelManager::processQueueData(std::size_t maxBatch){
    // Log only on first call to avoid spam
    static thread_local bool first_process = true;
    if (first_process) {
        logThreadRole("ModelManager", "Starting queue processing for " + getSymbol());
        first_process = false;
    }
    
    std::size_t processed=0;
    // TODO: Implement stk_q later
    /*
    stk_q::STK_Q_Data tick;
    while (processed<maxBatch && m_rawModel->popNextTick(tick)){
        // m_toBuffer.addTick(tick);                       // technical stack
        // m_rbHandler.checkForTradeOpportunity(tick);     // ring-buffer logic
        processed++;
    }
    */
    if (processed) pruneOldData();
    return processed;
}

// ────────────────────────────────────────────────────────────────────────────────
//  data ingestion (called by connection pipeline)
// ────────────────────────────────────────────────────────────────────────────────
void ModelManager::addTick(const stock_data_tick::StockData& t){
    m_rawModel->addTick(t);
}

void ModelManager::addTradeTick(double p,int v){ 
    // STUB: Just log
    std::cout << "[ModelManager] STUB: Adding trade tick for " << getSymbol() << " price=" << p << " vol=" << v << '\n';
    // if (v>0) m_volProfile.add_transaction(p,v); 
}

// ────────────────────────────────────────────────────────────────────────────────
//  misc accessors
// ────────────────────────────────────────────────────────────────────────────────
std::string ModelManager::getSymbol() const { return m_rawModel->symbol(); }

const raw_data_model::TradingParams& ModelManager::getParams() const { return m_rawModel->params(); }

std::size_t ModelManager::getTickCount() const { 
    return m_rawModel->queueSize(); 
}
const stock_data_tick::StockData* ModelManager::getLatestTick() const {
    static stock_data_tick::StockData tmp;
    return m_rawModel->latestTick(tmp) ? &tmp : nullptr;
}

std::vector<stock_data_tick::StockData> ModelManager::getTicksInWindow() const {
    // STUB: Return empty vector
    return {};
    
    /* ORIGINAL COMMENTED OUT:
    auto* q = m_rawModel->getStockQueue();
    if (!q) return {};
    std::vector<stock_data_tick::StockData> v;
    std::uint64_t cutoff = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()
                           ).count() - windowToDuration().count();
    stk_q::STK_Q_Data d;
    std::vector<stk_q::STK_Q_Data> tmp;
    while (q->pop(d)){ if (d.time>=cutoff) tmp.push_back(d); }
    for (auto& x:tmp) q->push(x);
    v.reserve(tmp.size());
    for (auto& x:tmp){
        stock_data_tick::StockData t;
        t.symbol=getSymbol(); t.timestamp=x.time;
        t.last=x.last; t.bid=x.bid; t.ask=x.ask;
        t.volume=x.volume; v.push_back(t);
    }
    return v;
    */
}

std::pair<std::size_t,TimeWindowUnit> ModelManager::getTimeWindow() const {
    return {m_windowSize,m_windowUnit};
}

void ModelManager::setTimeWindow(std::size_t sz,TimeWindowUnit u){
    m_windowSize=sz; m_windowUnit=u;
    // m_toBuffer.setWindow(windowToDuration());
    pruneOldData();
}

void ModelManager::clearTicks(){ 
    m_rawModel->clear(); 
}

bool ModelManager::initFromJson(const nlohmann::json& js) {
    auto result = m_rawModel->initialise(js);
    return !result; // error_code converts to bool (true if error), so we negate it
}

} // namespace model_manager
