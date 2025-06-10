// ───────────────────────────────────────────────────────────────────────────────
//  model_manager_factory.cpp
// ───────────────────────────────────────────────────────────────────────────────

#include "models/model_manager_factory.hpp"
#include "models/model_manager.hpp"
#include "../../util/app_state.hpp"

#include <iostream>
#include <thread>
#include <sstream>
#include <cstdlib>

using namespace std::chrono_literals;  // Add this line to fix numeric literals

namespace model_manager {

// ────────────────────────────────────────────────────────────────────────────────
// Configuration Constants
// ────────────────────────────────────────────────────────────────────────────────
// DEFAULT WINDOW SIZE: Used when creating ModelManager instances through the factory
// This controls how much historical data the TimeOrderedTickBuffer retains
static constexpr size_t DEFAULT_FACTORY_WINDOW_SIZE = 60;           // 60 minutes = 1 hour
static constexpr TimeWindowUnit DEFAULT_FACTORY_WINDOW_UNIT = TimeWindowUnit::MINUTES;

// ────────────────────────────────────────────────────────────────────────────────
// Utility for logging current thread
// ────────────────────────────────────────────────────────────────────────────────
void logCurrentThread(const std::string& component, const std::string& action) {
    auto tid = std::this_thread::get_id();
    std::ostringstream ss;
    ss << tid;
    std::cout << "[" << component << "][THREAD-ID:" << ss.str() << "] " << action << std::endl;
}

// ────────────────────────────────────────────────────────────────────────────────
// singleton plumbing
// ────────────────────────────────────────────────────────────────────────────────
std::unique_ptr<ModelManagerFactory> ModelManagerFactory::s_instance;
std::mutex                            ModelManagerFactory::s_mxSingleton;

ModelManagerFactory& ModelManagerFactory::getInstance() {
    std::lock_guard lg{s_mxSingleton};
    if (!s_instance) {
        logCurrentThread("ModelManagerFactory", "Creating singleton instance");
        s_instance.reset(new ModelManagerFactory);
    }
    return *s_instance;
}

// ────────────────────────────────────────────────────────────────────────────────
// CRUD helpers (pure object ownership – no threads)
// ────────────────────────────────────────────────────────────────────────────────
std::shared_ptr<ModelManager>
ModelManagerFactory::createModelManager(const std::string& symbol,
                                        size_t windowSize,
                                        TimeWindowUnit unit)
{
    logCurrentThread("ModelManagerFactory", "Creating ModelManager for symbol: " + symbol);
    auto mm = std::make_shared<ModelManager>(symbol, windowSize, unit);
    
    // Set debug logging to false by default for production performance
    // Can be enabled via environment variable or API call if needed
    bool debugEnabled = std::getenv("IBKR_DEBUG_LOGGING") != nullptr;
    mm->setDebugLogging(debugEnabled);
    
    std::lock_guard lg{m_mx};
    m_managers[symbol] = mm;
    return mm;
}

std::shared_ptr<ModelManager>
ModelManagerFactory::getModelManager(const std::string& symbol)
{
    logCurrentThread("ModelManagerFactory", "Looking for existing ModelManager for symbol: " + symbol);
    std::lock_guard lg{m_mx};
    auto it = m_managers.find(symbol);
    return it == m_managers.end() ? nullptr : it->second;
}

bool ModelManagerFactory::initModelFromJson(const std::string& symbol,
                                            const nlohmann::json& js,
                                            size_t window,
                                            TimeWindowUnit unit)
{
    logCurrentThread("ModelManagerFactory", "Initializing model from JSON for symbol: " + symbol);
    auto mm = getModelManager(symbol);
    if (!mm) mm = createModelManager(symbol, window, unit);
    return mm->initFromJson(js);
}

// removal / clear --------------------------------------------------------------
bool ModelManagerFactory::removeModel(const std::string& symbol){
    logCurrentThread("ModelManagerFactory", "Removing model for symbol: " + symbol);
    
    // 1) request thread stop (if any)
    app_state::AppState::getInstance()
        .requestThreadStop(symbol,"ModelManagerFactory::removeModel");

    // 2) erase object
    std::shared_ptr<ModelManager> mm;
    {
        std::lock_guard lg{m_mx};
        auto it = m_managers.find(symbol);
        if (it==m_managers.end()) return false;
        mm = it->second;
        m_managers.erase(it);
    }
    if (mm) mm->disconnectFromIBKR();
    return true;
}

void ModelManagerFactory::clearAll(){
    logCurrentThread("ModelManagerFactory", "Clearing all models");
    
    // stop threads first
    app_state::AppState::getInstance()
        .requestAllThreadsStop("ModelManagerFactory::clearAll");

    // disconnect + erase
    std::lock_guard lg{m_mx};
    for (auto& [_,mm] : m_managers) mm->disconnectFromIBKR();
    m_managers.clear();
}

// info helpers -----------------------------------------------------------------
bool ModelManagerFactory::hasModel(const std::string& sym){
    std::lock_guard lg{m_mx};
    return m_managers.count(sym);
}
std::vector<std::string> ModelManagerFactory::getAllSymbols() const{
    std::lock_guard lg{m_mx};
    std::vector<std::string> v; v.reserve(m_managers.size());
    for (auto& [s,_]:m_managers) v.push_back(s);
    return v;
}
std::map<std::string,std::shared_ptr<ModelManager>>
ModelManagerFactory::getAllModelManagers() const {
    std::lock_guard lg{m_mx}; return m_managers;
}

// ────────────────────────────────────────────────────────────────────────────────
// Back-compat helper – can optionally start a worker thread through AppState
// ────────────────────────────────────────────────────────────────────────────────
std::shared_ptr<ModelManager>
ModelManagerFactory::getModelManager(const std::string& symbol,
                                     bool startThread,
                                     const nlohmann::json& params)
{
    logCurrentThread("ModelManagerFactory", 
                    "Getting ModelManager for symbol: " + symbol + 
                    " (startThread=" + (startThread ? "true" : "false") + ")");
    
    auto mm = getModelManager(symbol);
    if (!mm) {
        // Use configurable constants instead of hardcoded values
        mm = createModelManager(symbol, DEFAULT_FACTORY_WINDOW_SIZE, DEFAULT_FACTORY_WINDOW_UNIT);
    }
    if (!params.is_null()) {
        if (params.contains(symbol)) {
            mm->initFromJson(params);
        } else {
            mm->initFromJson({{symbol, params}});
        }
    }

    if (startThread) { 
        logCurrentThread("ModelManagerFactory", 
                        "Requesting AppState to start worker thread for symbol: " + symbol);
        
        auto& as = app_state::AppState::getInstance();
        as.startThread(symbol, [mm, symbol](const app_state::StopToken& tok){
            // Log thread start from inside the worker thread
            logCurrentThread("MODEL-WORKER - " + symbol, 
                           "Starting worker thread");
            
            if (!mm->connectToIBKR()) {
                logCurrentThread("MODEL-WORKER - " + symbol, 
                               "Failed to connect to IBKR");
                std::this_thread::sleep_for(1s);
            } else {
                logCurrentThread("MODEL-WORKER - " + symbol, 
                               "Connected to IBKR");
            }
            
            // this is commented out, in case threadStart is set to true on accident we wont run 2 model manager threads for the same symbol
            // while (!tok.stop_requested()) { 
            //     mm->processQueueData();
            //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // }
            
            logCurrentThread("MODEL-WORKER - " + symbol, 
                           "Disconnecting from IBKR");
            mm->disconnectFromIBKR();
            
            logCurrentThread("MODEL-WORKER - " + symbol, 
                           "Worker thread ending");
        });
    }
    return mm;
}

} // namespace model_manager
