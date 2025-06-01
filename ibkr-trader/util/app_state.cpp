#include "app_state.hpp"
#include <iostream>
#include <future>
#include <iomanip>
#include <sstream>
#include <thread>

// to implement model_manager_factory
// somewhere in ModelManagerFactory.cpp (or similar)
// auto& as = app_state::AppState::getInstance();
// as.startThread(symbol, [mgr = mySharedModelManager](const StopToken& tok){
//     while (!tok.stop_requested()) {
//         mgr->processOnce();
//     }
//     mgr->disconnect();
// });


using namespace std::chrono_literals;
namespace app_state {

//───────────────────────────────────────────────────────────────────────────────
//  Singleton plumbing
//───────────────────────────────────────────────────────────────────────────────
namespace {
std::unique_ptr<AppState> s_instance;
std::mutex                s_instanceMx;
}

AppState& AppState::getInstance() {
    std::lock_guard<std::mutex> lg{s_instanceMx};
    if (!s_instance) { s_instance.reset(new AppState); }
    return *s_instance;
}

AppState::AppState() {
    logCurrentThread("AppState", "CONSTRUCTOR");
}

AppState::~AppState() {
    logThreadEvent("DESTRUCTOR", "AppState", "Ensuring all threads stopped");
    // Ensure everything is dead before static-destruction order havoc
    requestEmergencyStop(/*timeout*/ 2'000, "AppState::~AppState");
}

//───────────────────────────────────────────────────────────────────────────────
//  Centralized Logging with Thread Awareness
//───────────────────────────────────────────────────────────────────────────────
void AppState::logThreadEvent(const std::string& event, const std::string& component, 
                             const std::string& details) const {
    auto tid = std::this_thread::get_id();
    std::ostringstream ss;
    ss << tid;
    
    std::cout << "[AppState][THREAD:" << ss.str() << "]";
    if (!component.empty()) std::cout << "[" << component << "]";
    std::cout << " " << event;
    if (!details.empty()) std::cout << " - " << details;
    std::cout << std::endl;
}

void AppState::logCurrentThread(const std::string& component, const std::string& action) const {
    auto tid = std::this_thread::get_id();
    std::ostringstream ss;
    ss << tid;
    
    std::cout << "[THREAD-ID:" << ss.str() << "][" << component << "]";
    if (!action.empty()) std::cout << " " << action;
    std::cout << std::endl;
}

//───────────────────────────────────────────────────────────────────────────────
//  Enhanced Public API with Logging
//───────────────────────────────────────────────────────────────────────────────
bool AppState::startThread(std::string name,
                           std::function<void(const StopToken&)> task)
{
    std::lock_guard<std::mutex> lg{m_mutex};

    logThreadEvent("THREAD-START-REQUEST", "AppState", "Starting thread: " + name);

    // Stop an existing thread of the same name, if any
    if (auto it = m_threads.find(name); it != m_threads.end()) {
        logThreadEvent("THREAD-RESTART", "AppState", "Stopping existing thread: " + name);
        _stopThreadUnlocked(name);
    }

    ThreadEntry entry{
        ThreadWrapper([name, fn = std::move(task)](const StopToken& tok){
            // Log thread start from inside the new thread
            auto tid = std::this_thread::get_id();
            std::ostringstream ss;
            ss << tid;
            std::cout << "[NEW-THREAD:" << ss.str() << "][" << name << "] THREAD STARTED" << std::endl;
            
            fn(tok);
            
            std::cout << "[THREAD:" << ss.str() << "][" << name << "] THREAD ENDING" << std::endl;
        }),
        ThreadState::RUNNING,
        std::chrono::steady_clock::now(),
        "Worker thread for: " + name
    };
    
    m_threads.emplace(std::move(name), std::move(entry));
    logThreadEvent("THREAD-STARTED", "AppState", "Successfully started thread: " + name);
    return true;
}

bool AppState::requestThreadStop(const std::string& name,
                                 const std::string& requestor)
{
    std::lock_guard<std::mutex> lg{m_mutex};
    auto it = m_threads.find(name);
    if (it == m_threads.end()) {
        logThreadEvent("THREAD-STOP-FAILED", "AppState", 
                      "No thread '" + name + "' (requested by " + requestor + ")");
        return false;
    }
    logThreadEvent("THREAD-STOP-REQUEST", "AppState", 
                  "Stopping '" + name + "' (requested by " + requestor + ")");
    _stopThreadUnlocked(name);
    return true;
}

bool AppState::requestAllThreadsStop(const std::string& requestor)
{
    std::lock_guard<std::mutex> lg{m_mutex};
    if (m_shutdownInProgress.exchange(true)) { 
        logThreadEvent("SHUTDOWN-ALREADY-IN-PROGRESS", "AppState", "Requested by: " + requestor);
        return false; 
    }

    m_shutdownInitiator = requestor;
    logThreadEvent("SHUTDOWN-ALL-THREADS", "AppState", 
                  "Stop-all requested by " + requestor + " (thread count: " + 
                  std::to_string(m_threads.size()) + ")");
    _stopAllThreadsUnlocked();
    m_shutdownInProgress.store(false);
    return true;
}

bool AppState::requestEmergencyStop(int timeoutMs,
                                    const std::string& requestor)
{
    std::unique_lock<std::mutex> ul{m_mutex};
    if (m_emergencyShutdown.exchange(true)) { 
        logThreadEvent("EMERGENCY-STOP-ALREADY-ACTIVE", "AppState", "Requested by: " + requestor);
        return false; 
    }

    m_shutdownInitiator = requestor;
    logThreadEvent("EMERGENCY-STOP", "AppState", 
                  "**EMERGENCY-STOP** (" + std::to_string(timeoutMs) + 
                  " ms) by " + requestor + " (thread count: " + 
                  std::to_string(m_threads.size()) + ")");

    _emergencyStopUnlocked(timeoutMs);
    m_shutdownInProgress.store(false);
    return true;
}

//───────────────────────────────────────────────────────────────────────────────
//  Read-only helpers
//───────────────────────────────────────────────────────────────────────────────
ThreadState AppState::getThreadState(const std::string& name) const {
    std::lock_guard<std::mutex> lg{m_mutex};
    if (auto it = m_threads.find(name); it != m_threads.end()) {
        return it->second.state;
    }
    return ThreadState::DETACHED;
}

bool AppState::hasRunningThread(const std::string& name) const {
    return getThreadState(name) == ThreadState::RUNNING;
}

std::vector<std::string> AppState::getRunningNames() const {
    std::lock_guard<std::mutex> lg{m_mutex};
    std::vector<std::string> out;
    out.reserve(m_threads.size());
    for (auto& [n, _] : m_threads) { out.push_back(n); }
    return out;
}

std::map<std::string,std::string> AppState::getThreadStateInfo() const {
    std::lock_guard<std::mutex> lg{m_mutex};
    std::map<std::string,std::string> out;
    for (auto& [name, entry] : m_threads) {
        std::string s;
        switch (entry.state) {
            case ThreadState::RUNNING : s = "RUNNING";  break;
            case ThreadState::STOPPING: s = "STOPPING"; break;
            case ThreadState::DETACHED: s = "DETACHED"; break;
        }
        out[name] = s;
    }
    return out;
}

//───────────────────────────────────────────────────────────────────────────────
//  Internal helpers
//───────────────────────────────────────────────────────────────────────────────
void AppState::_stopThreadUnlocked(const std::string& name) {
    auto it = m_threads.find(name);
    if (it == m_threads.end()) { return; }

    it->second.state = ThreadState::STOPPING;
    it->second.wrapper.request_stop();

    // join with tiny timeout to avoid long stalls here
    auto fut = std::async(std::launch::async, [&wrapper = it->second.wrapper] {
        if (wrapper.joinable()) wrapper.join();
    });
    if (fut.wait_for(50ms) != std::future_status::ready) {
        // still busy — detach and mark
        if (it->second.wrapper.joinable()) it->second.wrapper.detach();
        it->second.state = ThreadState::DETACHED;
    }
    m_threads.erase(it);
}

void AppState::_stopAllThreadsUnlocked() {
    for (auto& [n, entry] : m_threads) {
        entry.state = ThreadState::STOPPING;
        entry.wrapper.request_stop();
    }
    for (auto& [n, entry] : m_threads) {
        if (entry.wrapper.joinable()) entry.wrapper.join();
    }
    m_threads.clear();
}

void AppState::_emergencyStopUnlocked(int timeoutMs) {
    // 1) request stop on every thread
    for (auto& [n, entry] : m_threads) {
        entry.state = ThreadState::STOPPING;
        entry.wrapper.request_stop();
    }

    // 2) attempt to join each thread with aggregate timeout
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    for (auto it = m_threads.begin(); it != m_threads.end(); ) {
        auto& entry = it->second;
        if (!entry.wrapper.joinable()) { it = m_threads.erase(it); continue; }

        auto fut = std::async(std::launch::async,
                              [&wrapper = entry.wrapper]{ wrapper.join(); });
        if (fut.wait_until(deadline) == std::future_status::ready) {
            it = m_threads.erase(it);
        } else {
            std::cerr << "[AppState]   '" << it->first
                      << "' not responding — detach.\n";
            entry.wrapper.detach();
            entry.state = ThreadState::DETACHED;
            ++it;
        }
    }
}

} // namespace app_state
