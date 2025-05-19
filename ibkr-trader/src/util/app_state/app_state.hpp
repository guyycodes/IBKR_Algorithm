// app_state.hpp
#ifndef APP_STATE_HPP
#define APP_STATE_HPP

#include <memory>
#include <mutex>
#include <map>
#include <thread>
#include <atomic>
#include <string>
#include <chrono>
#include <future>
#include <set>
#include <vector>
// Forward declaration instead of include
namespace model_manager {
    class ModelManager;
}

namespace app_state {

// Thread state tracking
enum class ThreadState {
    RUNNING,       // Thread is running normally
    STOPPING,      // Thread is in the process of stopping
    DETACHED       // Thread has been detached (couldn't join)
};

// Singleton class to maintain application state
class AppState {
private:
    // Singleton pattern
    static std::unique_ptr<AppState> s_instance;
    static std::mutex s_instanceMutex;
    
    // Thread registry - maps symbol to its running thread
    std::map<std::string, std::thread> m_modelThreads;
    
    // State tracking for threads
    std::map<std::string, ThreadState> m_threadStates;
    
    // Flag for each thread to indicate if it should continue running
    std::map<std::string, std::atomic<bool>> m_threadRunFlags;
    
    // Track whether we're in emergency shutdown mode
    std::atomic<bool> m_emergencyShutdown{false};
    
    // Track shutdown requests to prevent redundant operations
    std::atomic<bool> m_shutdownInProgress{false};
    
    // Track which component initiated shutdown
    std::string m_shutdownInitiator;
    
    // Mutex for thread operations
    mutable std::mutex m_threadMutex;
    
    // Private constructor for singleton
    AppState();
    
    // Internal implementations - only callable from public methods with proper synchronization
    void _stopThread(const std::string& symbol);
    void _stopAllThreads();
    void _emergencyStopThreads(int timeoutMs);

public:
    // Delete copy/move constructors and assignments
    AppState(const AppState&) = delete;
    AppState(AppState&&) = delete;
    AppState& operator=(const AppState&) = delete;
    AppState& operator=(AppState&&) = delete;
    
    // Get singleton instance
    static AppState& getInstance();
    
    // Thread registration and state management
    // ----------------------------------------
    
    // Register a new thread for a ModelManager and start it
    void registerModelThread(const std::string& symbol, 
                            std::shared_ptr<model_manager::ModelManager> manager);
    
    // Request to stop a single thread by symbol
    bool requestThreadStop(const std::string& symbol, const std::string& requestor);
    
    // Request to stop all threads
    bool requestAllThreadsStop(const std::string& requestor);
    
    // Request emergency stop of all threads with timeout for hanging threads
    bool requestEmergencyStop(int timeoutMs, const std::string& requestor);
    
    // Get current state of a thread
    ThreadState getThreadState(const std::string& symbol) const;
    
    // Check if a thread is in any active state (running or stopping)
    bool hasRunningThread(const std::string& symbol) const;
    
    // Get list of all symbols with threads in any state
    std::vector<std::string> getRunningSymbols() const;
    
    // Get detailed thread state information for logging/debugging
    std::map<std::string, std::string> getThreadStateInfo() const;
    
    // Check if shutdown is in progress
    bool isShutdownInProgress() const { return m_shutdownInProgress.load(); }
    
    // Check if we're in emergency shutdown mode
    bool isEmergencyShutdown() const { return m_emergencyShutdown.load(); }
    
    // Get the component that initiated shutdown
    std::string getShutdownInitiator() const { return m_shutdownInitiator; }
    
    // Legacy method names for compatibility - these now call the request versions
    // -------------------------------------------------------------------------
    void removeModelThread(const std::string& symbol) {
        requestThreadStop(symbol, "legacy_call");
    }
    
    void stopAllThreads() {
        requestAllThreadsStop("legacy_call");
    }
    
    void emergencyStopAllThreads(int timeoutMs) {
        requestEmergencyStop(timeoutMs, "legacy_call");
    }
    
    // Destructor - ensure all threads are stopped
    ~AppState();
};

} // namespace app_state

#endif // APP_STATE_HPP

