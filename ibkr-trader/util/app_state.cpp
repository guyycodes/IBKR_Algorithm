// ───────────────────────────────────────────────────────────────────────────────
//  app_state.cpp
// ───────────────────────────────────────────────────────────────────────────────

#include "app_state.hpp"
#include <iostream>
#include <future>
#include <iomanip>
#include <sstream>
#include <thread>
#include <optional>

using namespace std::chrono_literals;
namespace app_state {

//───────────────────────────────────────────────────────────────────────────────
//  Singleton plumbing
//───────────────────────────────────────────────────────────────────────────────
namespace {
std::unique_ptr<AppState> s_instance;
std::mutex                s_instanceMx;

// ------------------------------------------------------------------
//  Never attempt to join the thread we are running in.
// ------------------------------------------------------------------
inline bool isCurrentThread(const ThreadWrapper& wrapper) {
    return wrapper.get_id() == std::this_thread::get_id();
}
}

AppState& AppState::getInstance() {
    std::lock_guard<std::mutex> lg{s_instanceMx};
    if (!s_instance) { s_instance.reset(new AppState); }
    return *s_instance;
}

AppState::AppState() {
    logCurrentThread("AppState", "AppState singleton constructed");
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
    
    std::cout << "[" << component << "][THREAD-ID:" << ss.str() << "]";
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

    // Stop an existing thread of the same name, if any
    if (auto it = m_threads.find(name); it != m_threads.end()) {
        // Inline stop logic to avoid deadlock - same pattern as requestThreadStop
        it->second.state = ThreadState::STOPPING;
        it->second.wrapper.request_stop();
        if (it->second.wrapper.joinable()) {
            if (isCurrentThread(it->second.wrapper)) {
                it->second.wrapper.detach();
            } else {
                it->second.wrapper.join();
            }
        }
        m_threads.erase(it);
    }

    ThreadEntry entry{
        ThreadWrapper([name, fn = std::move(task)](const StopToken& tok){
            fn(tok);
        }),
        ThreadState::RUNNING,
        std::chrono::steady_clock::now(),
        "Worker thread for: " + name
    };
    
    m_threads.emplace(std::move(name), std::move(entry));
    return true;
}

bool AppState::requestThreadStop(const std::string& name,
                                 const std::string& requestor)
{
    std::optional<ThreadWrapper> victim;   // <─ use optional to avoid default construction
    
    // ═══════════════════════════════════════════════════════════════════════════════
    // CRITICAL SECTION START - Hold m_mutex only for map operations
    // ═══════════════════════════════════════════════════════════════════════════════
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

        it->second.state = ThreadState::STOPPING;
        it->second.wrapper.request_stop();

        victim = std::move(it->second.wrapper);     // hand the thread object out
        m_threads.erase(it);                        // map no longer owns it
    }   
    // ═══════════════════════════════════════════════════════════════════════════════
    // CRITICAL SECTION END - m_mutex released, safe to block on thread operations
    // ═══════════════════════════════════════════════════════════════════════════════

    // Now we can block without risking a dead-lock
    if (victim && victim->joinable()) {
        if (isCurrentThread(*victim))
            victim->detach();
        else
            victim->join();
    }
    return true;
}

bool AppState::requestAllThreadsStop(const std::string& requestor)
{
    std::vector<ThreadWrapper> victims;     // <─ collect wrappers outside lock
    
    // ═══════════════════════════════════════════════════════════════════════════════
    // CRITICAL SECTION START - Hold m_mutex only for map operations
    // ═══════════════════════════════════════════════════════════════════════════════
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

        // Request stop and move all wrappers out while holding lock
        victims.reserve(m_threads.size());
        for (auto& [n, entry] : m_threads) {
            entry.state = ThreadState::STOPPING;
            entry.wrapper.request_stop();
            victims.emplace_back(std::move(entry.wrapper));
        }
        m_threads.clear();
    }   
    // ═══════════════════════════════════════════════════════════════════════════════
    // CRITICAL SECTION END - m_mutex released, safe to block on thread operations
    // ═══════════════════════════════════════════════════════════════════════════════

    // Now join/detach all victims without holding the lock
    for (auto& victim : victims) {
        if (victim.joinable()) {
            if (isCurrentThread(victim)) {
                victim.detach();
            } else {
                victim.join();
            }
        }
    }

    m_shutdownInProgress.store(false);
    return true;
}

bool AppState::requestEmergencyStop(int timeoutMs,
                                    const std::string& requestor)
{
    std::vector<std::pair<std::string, ThreadWrapper>> victims; // <─ collect wrappers outside lock
    
    // ═══════════════════════════════════════════════════════════════════════════════
    // CRITICAL SECTION START - Hold m_mutex only for map operations
    // ═══════════════════════════════════════════════════════════════════════════════
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

        // Request stop and move all wrappers out while holding lock
        victims.reserve(m_threads.size());
        for (auto& [name, entry] : m_threads) {
            entry.state = ThreadState::STOPPING;
            entry.wrapper.request_stop();
            victims.emplace_back(name, std::move(entry.wrapper));
        }
        m_threads.clear();
    }   
    // ═══════════════════════════════════════════════════════════════════════════════
    // CRITICAL SECTION END - m_mutex released, safe to block on thread operations
    // ═══════════════════════════════════════════════════════════════════════════════

    // Now join/detach all victims with deadline, without holding the lock
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (auto& [name, victim] : victims) {
        if (!victim.joinable()) continue;

        if (isCurrentThread(victim)) {
            // Never try to join ourselves - just detach
            std::cerr << "[AppState]   '" << name << "' is current thread — detach.\n";
            victim.detach();
            continue;
        }

        auto fut = std::async(std::launch::async, [&victim]{ victim.join(); });
        if (fut.wait_until(deadline) != std::future_status::ready) {
            std::cerr << "[AppState]   '" << name << "' not responding — detach.\n";
            victim.detach();
        }
    }

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
// NOTE: Old _stopThreadUnlocked, _stopAllThreadsUnlocked, and _emergencyStopUnlocked
// functions removed to avoid lock-order inversion deadlocks. The logic has been
// moved into the public methods above to ensure blocking operations happen
// outside the critical section.

} // namespace app_state
