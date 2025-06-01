// ────────────────────────────────────────────────────────────────────────────────
// input_manager.cpp – HTTP-only implementation (CLI code removed)
// ────────────────────────────────────────────────────────────────────────────────
#include "input_manager/input_manager.hpp"
#include "models/model_manager_factory.hpp"
#include "models/model_manager.hpp"
#include "../../util/app_state.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <sstream>

using namespace std::chrono_literals;
namespace input_manager {

// ────────────────────────────────────────────────────────────────────────────────
// Thread logging utility - enhanced for role clarity
// ────────────────────────────────────────────────────────────────────────────────
void logThreadRole(const std::string& role, const std::string& action) {
    auto tid = std::this_thread::get_id();
    std::ostringstream ss;
    ss << tid;
    std::cout << "[" << role << ":" << ss.str() << "] " << action << std::endl;
}

// ────────────────────────────────────────────────────────────────────────────────
// Ctor / Dtor
// ────────────────────────────────────────────────────────────────────────────────
InputManager::InputManager() {
    m_localApi = std::make_unique<local_api::LocalAPI>();
    log(0, "InputManager constructed");
}

InputManager::~InputManager() {
    stop();
    auto& as = app_state::AppState::getInstance();
    as.requestAllThreadsStop("InputManager::~InputManager");
    log(0, "InputManager destroyed");
}

// ────────────────────────────────────────────────────────────────────────────────
// Initialisation
// ────────────────────────────────────────────────────────────────────────────────
bool InputManager::initialize(const std::string& cfgPath) {
    // main thread calls this function
    logThreadRole("MAIN-THREAD", "InputManager initializing");
    log(1, "initialise");

    if (!cfgPath.empty() && !parseConfig(cfgPath)) { return false; }

    // bind LocalAPI callbacks
    auto self = shared_from_this();
    m_localApi->registerTradeCallback(
        [self](const nlohmann::json& data){ 
            // Only log on first callback to show HTTP worker thread
            static bool first_callback = true;
            if (first_callback) {
                logThreadRole("HTTP-WORKER", "Processing first API request");
                first_callback = false;
            }
            self->processApiRequest(data); 
        });
    m_localApi->registerErrorCallback(
        [self](const std::string& c,const std::string& m){ 
            self->log(2,"API-err:"+c+" "+m); 
        });

    return m_localApi->initialize(cfgPath);
}

// ────────────────────────────────────────────────────────────────────────────────
void InputManager::runApiServer(int port) {
    // main thread calls this function
    logThreadRole("MAIN-THREAD", "Starting API server on port " + std::to_string(port));
    m_activeSource = InputSource::API;
    if (!m_localApi->start(port)) {
        log(2, "LocalAPI failed to start");
        m_activeSource = InputSource::NONE;
        return;
    }
    std::cout << "[InputManager] HTTP server listening on :" << port
              << "  (press <Enter> to quit)\n";
    std::cin.get();                               // block until user stops
    logThreadRole("MAIN-THREAD", "User pressed Enter - stopping server");
    stop();
}

void InputManager::stop() {
    if (m_activeSource == InputSource::API) { m_localApi->stop(); }
    m_activeSource = InputSource::NONE;
}

// ────────────────────────────────────────────────────────────────────────────────
// REST endpoint helpers
// ────────────────────────────────────────────────────────────────────────────────
bool InputManager::processApiRequest(const nlohmann::json& req) {
    if (m_activeSource != InputSource::API) { 
        log(2, "Not in API mode");
        return false; 
    }
    
    // Check if required fields exist
    if (!req.contains("symbol")) {
        log(2, "Missing 'symbol' field in API request");
        return false;
    }
    
    if (!req.contains("params")) {
        log(2, "Missing 'params' field in API request");
        return false;
    }
    
    std::string symbol;
    try {
        symbol = req["symbol"].get<std::string>();
        std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
    } catch (const std::exception& e) {
        log(2, "Invalid symbol field: " + std::string(e.what()));
        return false;
    }

    m_outputJson[symbol] = req["params"];
    processOutput();
    return true;
}

bool InputManager::processBatchRequests(const std::vector<nlohmann::json>& batch){
    bool ok = true;
    for (const auto& j : batch) ok &= processApiRequest(j);
    return ok;
}

bool InputManager::queueApiRequest(const nlohmann::json& j){
    return m_activeSource==InputSource::API && m_localApi->queueTradingRequest(j);
}
bool InputManager::confirmQueuedRequests(){
    return m_activeSource==InputSource::API && m_localApi->confirmQueuedRequests();
}

// ────────────────────────────────────────────────────────────────────────────────
// Symbol lifecycle helpers
// ────────────────────────────────────────────────────────────────────────────────
bool InputManager::clearSymbol(const std::string& sym){
    auto& as      = app_state::AppState::getInstance();
    auto& factory = model_manager::ModelManagerFactory::getInstance();

    as.requestThreadStop(sym, "InputManager::clearSymbol");
    factory.removeModel(sym);

    return m_outputJson.erase(sym) > 0;
}

void InputManager::clearAllInputs() {
    auto& as      = app_state::AppState::getInstance();
    auto& factory = model_manager::ModelManagerFactory::getInstance();

    as.requestAllThreadsStop("InputManager::clearAllInputs");
    factory.clearAll();
    m_outputJson.clear();
}

// ────────────────────────────────────────────────────────────────────────────────
void InputManager::emergencyStop() {
    logThreadRole("EMERGENCY", "InputManager emergency stop initiated");
    stop();
    clearAllInputs();
    m_localApi->emergencyStop();
    app_state::AppState::getInstance()
        .requestEmergencyStop(1'000, "InputManager::emergencyStop");
}

// ────────────────────────────────────────────────────────────────────────────────
// Status / logging
// ────────────────────────────────────────────────────────────────────────────────
nlohmann::json InputManager::getStatus() const {
    return {
        { "source", (m_activeSource==InputSource::API)?"API":"NONE" },
        { "threads", app_state::AppState::getInstance().getRunningNames() },
        { "outputSize", m_outputJson.size() }
    };
}
nlohmann::json InputManager::getOutput() const { return m_outputJson; }
nlohmann::json InputManager::getPendingRequests() const {
    return m_activeSource==InputSource::API
         ? m_localApi->getPendingRequests()
         : nlohmann::json::object();
}

void InputManager::setLogLevel(int lvl){
    m_logLevel = lvl;
    m_localApi->setLogLevel(lvl);
}

void InputManager::log(int lvl, std::string_view msg) const {
    if (lvl <= m_logLevel) {
        static const char* tag[]{"INFO","DBG","ERR","LOG"};
        std::cout << "[InputManager][" << tag[lvl] << "] " << msg << '\n';
    }
}

// ────────────────────────────────────────────────────────────────────────────────
// Core "bottleneck": translate m_outputJson → ModelManager threads
// ────────────────────────────────────────────────────────────────────────────────
void InputManager::processOutput() {
    auto& factory = model_manager::ModelManagerFactory::getInstance();
    auto& as      = app_state::AppState::getInstance();

    for (auto& [symbol, params] : m_outputJson.items()) {
        if (as.hasRunningThread(symbol)) { 
            continue; 
        }

        // Only log when creating the first worker thread
        static bool first_worker = true;
        if (first_worker) {
            logThreadRole("HTTP-WORKER", "Creating first ModelManager worker thread for " + symbol);
            first_worker = false;
        }
        
        // create / fetch model
        auto mm = factory.getModelManager(symbol, /*startThread*/false, params);
        if (!mm) { log(2, "ModelManager create failed for "+symbol); continue; }

        // register thread in AppState; mm keeps its own copy
        as.startThread(symbol, [mm, symbol](const app_state::StopToken& tok){
            // Log once when worker thread starts
            logThreadRole("MODEL-WORKER", "Starting data processing for " + symbol);
            
            if (!mm->connectToIBKR()){ 
                std::this_thread::sleep_for(1s); 
                return; 
            }
            
            while (!tok.stop_requested()) {
                mm->processQueueData(/*batch*/20);
                std::this_thread::sleep_for(100ms);
            }
            
            mm->disconnectFromIBKR();
            logThreadRole("MODEL-WORKER", "Ending data processing for " + symbol);
        });
    }
}

// ────────────────────────────────────────────────────────────────────────────────
bool InputManager::parseConfig(const std::string& path){
    try {
        std::ifstream f(path);
        if (!f) { log(2,"cfg open fail"); return false; }
        f >> m_config;
        return true;
    } catch(std::exception const& e){
        log(2, e.what());
        return false;
    }
}

} // namespace input_manager
