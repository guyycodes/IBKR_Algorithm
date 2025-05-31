// // app_state.cpp

#include "app_state.hpp"
#include "../../models/model_manager.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
namespace app_state {

// Initialize static members
std::unique_ptr<AppState> AppState::s_instance = nullptr;
std::mutex AppState::s_instanceMutex;

// Constructor initializes all member variables
AppState::AppState() 
    : m_emergencyShutdown(false)
    , m_shutdownInProgress(false)
    , m_shutdownInitiator("none")
{
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    std::cout << "[AppState] *create* [app_state_thread: " << threadIdStr.str() << "] " << std::endl;
}

// Get singleton instance with thread-safe initialization
AppState& AppState::getInstance() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance) {
        s_instance = std::unique_ptr<AppState>(new AppState());
    }
    return *s_instance;
}

// This function will run as the main loop for each ModelManager thread
// It continuously processes data for its assigned symbol until signaled to stop
void runModelManagerThread(std::shared_ptr<model_manager::ModelManager> manager, 
                          std::atomic<bool>& runFlag) {
    const std::string symbol = manager->getSymbol();
    
    // Get thread ID for logging
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    
    bool connected = manager->connectToIBKR();
    
    if (connected) {
        std::cout << "[app_state][ThreadID " << symbol << " is: " << threadIdStr.str() << "] Successfully connected to IBKR API" << std::endl;
    } else {
        std::cout << "[app_state][ThreadID" << symbol << " is: " << threadIdStr.str() << "] Warning: Failed to connect to IBKR API" << std::endl;
        std::cout << "[app_state][ThreadID" << symbol << " is: " << threadIdStr.str() << "] Will continue running but no live data will be received." << std::endl;
    }
    
    // Get reference to AppState for emergency shutdown check
    auto& appState = app_state::AppState::getInstance();
    
    // Main loop - run until signaled to stop
    while (runFlag.load() && !appState.isEmergencyShutdown()) {
        // Process a batch of ticks from the queue 
        // The processQueueData method handles filtering old data
        // and applying the trading logic
        // size_t processed = manager->processQueueData(20);
        
        // If we processed items, log it - now logged directly in processQueueData
        //if (processed > 0) {
        //    std::cout << "[Thread] Symbol " << symbol 
        //              << " processed " << processed << " queue items" << std::endl;
        //}
        
        // If no items were processed, sleep to avoid busy-waiting
        // if (processed == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // }
        
        // Check for emergency shutdown again
        if (appState.isEmergencyShutdown()) {
            std::cout << "[app_state][ThreadID: " << threadIdStr.str() << "] Emergency shutdown detected for symbol: " << symbol << std::endl;
            break;
        }
    }
    
    // Disconnect from IBKR API when thread is stopping
    if (connected) {
        std::cout << "[app_state][ThreadID: " << threadIdStr.str() << "] Disconnecting from IBKR API for symbol: " << symbol << std::endl;
        manager->disconnectFromIBKR();
    }
    
    std::cout << "[app_state][ThreadID: " << threadIdStr.str() << "] Stopped for symbol: " << symbol << std::endl;
}

// Register a new thread for a ModelManager and start it
void AppState::registerModelThread(const std::string& symbol, 
                                  std::shared_ptr<model_manager::ModelManager> manager) {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    // If a thread is already running for this symbol, stop it first
    if (m_modelThreads.find(symbol) != m_modelThreads.end()) {
        _stopThread(symbol);
    }
    
    // Create a new run flag and set it to true
    m_threadRunFlags[symbol] = true;
    
    // Start a new thread for this symbol
    m_modelThreads[symbol] = std::thread(
        runModelManagerThread,
        manager,
        std::ref(m_threadRunFlags[symbol])
    );
    
    // Set thread state to RUNNING
    m_threadStates[symbol] = ThreadState::RUNNING;
    std::stringstream threadIdStr;
    threadIdStr << std::this_thread::get_id();
    std::cout << "[AppState] Registered thread for symbol: "  << symbol<< " with threadID: " << threadIdStr.str() << std::endl;
}

// Internal implementation of single thread stop
void AppState::_stopThread(const std::string& symbol) {
    // WARNING: This method should ONLY be called when m_threadMutex is already locked!
    
    auto threadIt = m_modelThreads.find(symbol);
    auto flagIt = m_threadRunFlags.find(symbol);
    auto stateIt = m_threadStates.find(symbol);
    
    if (threadIt != m_modelThreads.end() && flagIt != m_threadRunFlags.end()) {
        // Update state to STOPPING
        if (stateIt != m_threadStates.end()) {
            stateIt->second = ThreadState::STOPPING;
        } else {
            m_threadStates[symbol] = ThreadState::STOPPING;
        }
        
        // Signal the thread to stop
        flagIt->second = false;
        
        // Wait for the thread to finish
        if (threadIt->second.joinable()) {
            threadIt->second.join();
        }
        
        // Remove from maps
        m_modelThreads.erase(threadIt);
        m_threadRunFlags.erase(flagIt);
        
        // Remove from states or mark as completed
        if (stateIt != m_threadStates.end()) {
            m_threadStates.erase(stateIt);
        }
        
        std::cout << "[app_state] Removed thread for symbol: " << symbol << std::endl;
    }
}

// Internal implementation of all threads stop
void AppState::_stopAllThreads() {
    // WARNING: This method should ONLY be called when m_threadMutex is already locked!
    
    // Signal all threads to stop and update states
    for (auto& pair : m_threadRunFlags) {
        pair.second = false;
        m_threadStates[pair.first] = ThreadState::STOPPING;
    }
    
    // Join all threads
    for (auto& pair : m_modelThreads) {
        if (pair.second.joinable()) {
            pair.second.join();
        }
    }
    
    // Clear maps
    m_modelThreads.clear();
    m_threadRunFlags.clear();
    m_threadStates.clear();
    
    std::cout << "[app_state] Stopped all threads" << std::endl;
}

// Internal implementation of emergency stop
void AppState::_emergencyStopThreads(int timeoutMs) {
    // WARNING: This method should ONLY be called when m_threadMutex is already locked!
    // the caller locks this already
    
    // Set emergency shutdown flag
    m_emergencyShutdown.store(true);
    
    // Get a list of symbols with active threads
    std::vector<std::string> activeSymbols;
    
    // Get list of active symbols and signal all threads to stop
    for (const auto& pair : m_modelThreads) {
        activeSymbols.push_back(pair.first);
        
        // Update state to STOPPING
        m_threadStates[pair.first] = ThreadState::STOPPING;
    }
     
     
    // Signal all threads to stop
    for (auto& pair : m_threadRunFlags) {
        pair.second.store(false);
    }
    
    // Unlock mutex while waiting for threads to join
    m_threadMutex.unlock();
    
    // Calculate timeout time
    auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    
    // Track which threads were joined successfully
    std::vector<std::string> joinedThreads;
    std::vector<std::string> detachedThreads;
    
    // Try to join each thread with timeout
    for (const auto& symbol : activeSymbols) {
        std::thread* threadPtr = nullptr;
        
        // Get a pointer to the thread (inside a lock)
        {
            std::lock_guard<std::mutex> lock(m_threadMutex);
            auto it = m_modelThreads.find(symbol);
            if (it != m_modelThreads.end() && it->second.joinable()) {
                threadPtr = &(it->second);
            }
        }
        
        // Skip if thread doesn't exist or isn't joinable
        if (!threadPtr) {
            continue;
        }
        
        // Create a future to join with timeout
        auto joinFuture = std::async(std::launch::async, [threadPtr]() {
            threadPtr->join();
        });
        
        // Wait for join with timeout
        if (joinFuture.wait_until(endTime) == std::future_status::ready) {
            // Thread joined successfully
            joinedThreads.push_back(symbol);
        } else {
            // Thread didn't join within timeout, detach it
            std::cout << "[app_state] WARNING: Thread for symbol " << symbol 
                      << " did not respond to stop signal within timeout. Detaching!" << std::endl;
            threadPtr->detach();
            detachedThreads.push_back(symbol);
        }
    }
    
    // Re-lock mutex for the clean up phase
    m_threadMutex.lock();
    
    // Update states and remove all joined threads
    for (const auto& symbol : joinedThreads) {
        auto threadIt = m_modelThreads.find(symbol);
        auto flagIt = m_threadRunFlags.find(symbol);
        
        if (threadIt != m_modelThreads.end()) {
            m_modelThreads.erase(threadIt);
        }
        
        if (flagIt != m_threadRunFlags.end()) {
            m_threadRunFlags.erase(flagIt);
        }
        
        // Remove from states
        m_threadStates.erase(symbol);
    }
    
    // Update states and remove all detached threads
    for (const auto& symbol : detachedThreads) {
        auto threadIt = m_modelThreads.find(symbol);
        auto flagIt = m_threadRunFlags.find(symbol);
        
        if (threadIt != m_modelThreads.end()) {
            m_modelThreads.erase(threadIt);
        }
        
        if (flagIt != m_threadRunFlags.end()) {
            m_threadRunFlags.erase(flagIt);
        }
        
        // Mark as DETACHED in states
        m_threadStates[symbol] = ThreadState::DETACHED;
    }
    
    std::cout << "[app_state] Emergency stop completed: " << joinedThreads.size() 
              << " threads stopped normally, " << detachedThreads.size() 
              << " threads detached due to timeout" << std::endl;
}

// Public API: Request to stop a single thread by symbol
bool AppState::requestThreadStop(const std::string& symbol, const std::string& requestor) {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    auto threadIt = m_modelThreads.find(symbol);
    auto stateIt = m_threadStates.find(symbol);
    
    // Check if thread exists
    if (threadIt == m_modelThreads.end()) {
        std::cout << "[app_state] Thread for symbol " << symbol 
                  << " not found (requested by " << requestor << ")" << std::endl;
        return false;
    }
    
    // Check if thread is already stopping
    if (stateIt != m_threadStates.end() && stateIt->second == ThreadState::STOPPING) {
        std::cout << "[app_state] Thread for symbol " << symbol 
                  << " is already stopping (new request by " << requestor << ")" << std::endl;
        return false;
    }
    
    std::cout << "[app_state] Stopping thread for symbol " << symbol 
              << " (requested by " << requestor << ")" << std::endl;
    
    // Stop the thread
    _stopThread(symbol);
    return true;
}

// Public API: Request to stop all threads
bool AppState::requestAllThreadsStop(const std::string& requestor) {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    // Check if shutdown is already in progress
    if (m_shutdownInProgress.load()) {
        std::cout << "[app_state] Shutdown already in progress (initiated by " 
                  << m_shutdownInitiator << ", new request by " << requestor << ")" << std::endl;
        return false;
    }
    
    // Set shutdown flag and initiator
    m_shutdownInProgress.store(true);
    m_shutdownInitiator = requestor;
    
    std::cout << "[app_state] Stopping all threads (requested by " << requestor << ")" << std::endl;
    
    // Stop all threads
    _stopAllThreads();
    
    // Reset shutdown flag
    m_shutdownInProgress.store(false);
    
    return true;
}

// Public API: Request emergency stop of all threads
bool AppState::requestEmergencyStop(int timeoutMs, const std::string& requestor) {
    std::unique_lock<std::mutex> lock(m_threadMutex);
    
    // Check if emergency shutdown is already in progress
    if (m_emergencyShutdown.load()) {
        std::cout << "[AppState] Emergency shutdown already in progress (initiated by " 
                  << m_shutdownInitiator << ", new request by " << requestor << ")" << std::endl;
        return false;
    }
    
    // Set shutdown flags and initiator
    m_shutdownInProgress.store(true);
    m_shutdownInitiator = requestor;
    
    std::cout << "[app_state] EMERGENCY STOP of all threads with " << timeoutMs << "ms timeout "
              << "(requested by " << requestor << ")" << std::endl;
    
    // Perform emergency stop (this will handle the mutex unlock/lock internally)
    _emergencyStopThreads(timeoutMs);
    
    // Reset shutdown flag but keep emergency flag set for polling threads
    m_shutdownInProgress.store(false);
    
    return true;
}

// Get current state of a thread
ThreadState AppState::getThreadState(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    auto stateIt = m_threadStates.find(symbol);
    if (stateIt != m_threadStates.end()) {
        return stateIt->second;
    }
    
    // If not found in states but exists in threads, consider it RUNNING
    if (m_modelThreads.find(symbol) != m_modelThreads.end()) {
        return ThreadState::RUNNING;
    }
    
    // Default - not found
    return ThreadState::DETACHED;
}

// Check if a thread is in any active state
bool AppState::hasRunningThread(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    // First check if it's in the thread map
    if (m_modelThreads.find(symbol) != m_modelThreads.end()) {
        return true;
    }
    
    // Then check if it's in the states map as DETACHED
    auto stateIt = m_threadStates.find(symbol);
    if (stateIt != m_threadStates.end() && stateIt->second == ThreadState::DETACHED) {
        return true;
    }
    
    return false;
}

// Get list of all symbols with threads in any state
std::vector<std::string> AppState::getRunningSymbols() const {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    std::vector<std::string> symbols;
    
    // First add all active threads
    for (const auto& pair : m_modelThreads) {
        symbols.push_back(pair.first);
    }
    
    // Then add any detached threads
    for (const auto& pair : m_threadStates) {
        if (std::find(symbols.begin(), symbols.end(), pair.first) == symbols.end()) {
            symbols.push_back(pair.first);
        }
    }
    
    return symbols;
}

// Get detailed thread state information for logging/debugging
std::map<std::string, std::string> AppState::getThreadStateInfo() const {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    std::map<std::string, std::string> info;
    
    // Process all registered symbols
    for (const auto& pair : m_threadStates) {
        const std::string& symbol = pair.first;
        ThreadState state = pair.second;
        
        std::string stateStr;
        switch (state) {
            case ThreadState::RUNNING:  stateStr = "RUNNING"; break;
            case ThreadState::STOPPING: stateStr = "STOPPING"; break;
            case ThreadState::DETACHED: stateStr = "DETACHED"; break;
            default:                    stateStr = "UNKNOWN"; break;
        }
        
        // Check if thread is in thread map
        bool inThreadMap = (m_modelThreads.find(symbol) != m_modelThreads.end());
        
        // Check if runflag exists and its value
        bool hasRunFlag = (m_threadRunFlags.find(symbol) != m_threadRunFlags.end());
        bool runFlagValue = hasRunFlag ? m_threadRunFlags.at(symbol).load() : false;
        
        // Format the info string
        std::string infoStr = "State: " + stateStr + 
                             ", Has Thread: " + std::string(inThreadMap ? "YES" : "NO") +
                             ", RunFlag: " + (hasRunFlag ? (runFlagValue ? "TRUE" : "FALSE") : "NONE");
        
        info[symbol] = infoStr;
    }
    
    return info;
}

// Destructor - ensure all threads are stopped
AppState::~AppState() {
    // Use emergency stop to ensure all threads are stopped, even hanging ones
    requestEmergencyStop(2000, "destructor");
}

} // namespace app_state

