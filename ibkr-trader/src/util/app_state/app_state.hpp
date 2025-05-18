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
#include "../../models/model_manager.hpp"

namespace app_state {

// Singleton class to maintain application state
class AppState {
private:
    // Singleton pattern
    static std::unique_ptr<AppState> s_instance;
    static std::mutex s_instanceMutex;
    
    // Thread registry - maps symbol to its running thread
    std::map<std::string, std::thread> m_modelThreads;
    
    // Flag for each thread to indicate if it should continue running
    std::map<std::string, std::atomic<bool>> m_threadRunFlags;
    
    // Track whether we're in emergency shutdown mode
    std::atomic<bool> m_emergencyShutdown{false};
    
    // Mutex for thread operations
    mutable std::mutex m_threadMutex;
    
    // Private constructor for singleton
    AppState() = default;

public:
    // Delete copy/move constructors and assignments
    AppState(const AppState&) = delete;
    AppState(AppState&&) = delete;
    AppState& operator=(const AppState&) = delete;
    AppState& operator=(AppState&&) = delete;
    
    // Get singleton instance
    static AppState& getInstance();
    
    // Register a new thread for a ModelManager and start it
    void registerModelThread(const std::string& symbol, 
                            std::shared_ptr<model_manager::ModelManager> manager);
    
    // Stop and remove a thread for a symbol
    void removeModelThread(const std::string& symbol);
    
    // Stop all threads
    void stopAllThreads();
    
    // Emergency stop all threads, including forceful termination of hanging threads
    void emergencyStopAllThreads(int timeoutMs = 1000);
    
    // Check if a thread is running for a symbol
    bool hasRunningThread(const std::string& symbol) const;
    
    // Get a list of all symbols with running threads
    std::vector<std::string> getRunningSymbols() const;
    
    // Check if we're in emergency shutdown mode
    bool isEmergencyShutdown() const { return m_emergencyShutdown.load(); }
    
    // Destructor - ensure all threads are stopped
    ~AppState();
};

} // namespace app_state

#endif // APP_STATE_HPP

