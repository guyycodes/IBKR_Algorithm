// // app_state.cpp

#include "app_state.hpp"
#include <iostream>

namespace app_state {

// Initialize static members
std::unique_ptr<AppState> AppState::s_instance = nullptr;
std::mutex AppState::s_instanceMutex;

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
    std::cout << "[Thread] Started for symbol: " << symbol << std::endl;
    
    // Connect to IBKR API 
    std::cout << "[Thread] Connecting to IBKR API for symbol: " << symbol << std::endl;
    bool connected = manager->connectToIBKR();
    
    if (connected) {
        std::cout << "[Thread] Successfully connected to IBKR API for symbol: " << symbol << std::endl;
    } else {
        std::cout << "[Thread] Warning: Failed to connect to IBKR API for symbol: " << symbol << std::endl;
        std::cout << "[Thread] Will continue running but no live data will be received." << std::endl;
    }
    
    // Get reference to AppState for emergency shutdown check
    auto& appState = app_state::AppState::getInstance();
    
    // Main loop - run until signaled to stop
    while (runFlag.load() && !appState.isEmergencyShutdown()) {
        // Process a batch of ticks from the queue 
        // The processQueueData method handles filtering old data
        // and applying the trading logic
        size_t processed = manager->processQueueData(20);
        
        // If we processed items, log it
        if (processed > 0) {
            std::cout << "[Thread] Symbol " << symbol 
                      << " processed " << processed << " queue items" << std::endl;
        }
        
        // If no items were processed, sleep to avoid busy-waiting
        if (processed == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Check for emergency shutdown again
        if (appState.isEmergencyShutdown()) {
            std::cout << "[Thread] Emergency shutdown detected for symbol: " << symbol << std::endl;
            break;
        }
    }
    
    // Disconnect from IBKR API when thread is stopping
    if (connected) {
        std::cout << "[Thread] Disconnecting from IBKR API for symbol: " << symbol << std::endl;
        manager->disconnectFromIBKR();
    }
    
    std::cout << "[Thread] Stopped for symbol: " << symbol << std::endl;
}

// Register a new thread for a ModelManager and start it
void AppState::registerModelThread(const std::string& symbol, 
                                  std::shared_ptr<model_manager::ModelManager> manager) {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    // If a thread is already running for this symbol, stop it first
    if (m_modelThreads.find(symbol) != m_modelThreads.end()) {
        removeModelThread(symbol);
    }
    
    // Create a new run flag and set it to true
    m_threadRunFlags[symbol] = true;
    
    // Start a new thread for this symbol
    m_modelThreads[symbol] = std::thread(
        runModelManagerThread,
        manager,
        std::ref(m_threadRunFlags[symbol])
    );
    
    std::cout << "[AppState] Registered thread for symbol: " << symbol << std::endl;
}

// Stop and remove a thread for a symbol
void AppState::removeModelThread(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    auto threadIt = m_modelThreads.find(symbol);
    auto flagIt = m_threadRunFlags.find(symbol);
    
    if (threadIt != m_modelThreads.end() && flagIt != m_threadRunFlags.end()) {
        // Signal the thread to stop
        flagIt->second = false;
        
        // Wait for the thread to finish
        if (threadIt->second.joinable()) {
            threadIt->second.join();
        }
        
        // Remove from maps
        m_modelThreads.erase(threadIt);
        m_threadRunFlags.erase(flagIt);
        
        std::cout << "[AppState] Removed thread for symbol: " << symbol << std::endl;
    }
}

// Stop all threads
void AppState::stopAllThreads() {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    // Signal all threads to stop
    for (auto& pair : m_threadRunFlags) {
        pair.second = false;
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
    
    std::cout << "[AppState] Stopped all threads" << std::endl;
}

// Check if a thread is running for a symbol
bool AppState::hasRunningThread(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    return m_modelThreads.find(symbol) != m_modelThreads.end();
}

// Get a list of all symbols with running threads
std::vector<std::string> AppState::getRunningSymbols() const {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    
    std::vector<std::string> symbols;
    symbols.reserve(m_modelThreads.size());
    
    for (const auto& pair : m_modelThreads) {
        symbols.push_back(pair.first);
    }
    
    return symbols;
}

// Implement emergency stop for all threads with forceful termination of hanging threads
void AppState::emergencyStopAllThreads(int timeoutMs) {
    std::cout << "[AppState] EMERGENCY STOP of all threads with " << timeoutMs << "ms timeout" << std::endl;
    
    // Set emergency shutdown flag
    m_emergencyShutdown.store(true);
    
    // Get a list of symbols with active threads
    std::vector<std::string> activeSymbols;
    
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        
        // Get list of active symbols
        for (const auto& pair : m_modelThreads) {
            activeSymbols.push_back(pair.first);
        }
        
        // Signal all threads to stop
        for (auto& pair : m_threadRunFlags) {
            pair.second.store(false);
        }
    }
    
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
            std::cout << "[AppState] WARNING: Thread for symbol " << symbol 
                      << " did not respond to stop signal within timeout. Detaching!" << std::endl;
            threadPtr->detach();
            detachedThreads.push_back(symbol);
        }
    }
    
    // Clean up the maps
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        
        // Remove all joined threads
        for (const auto& symbol : joinedThreads) {
            m_modelThreads.erase(symbol);
            m_threadRunFlags.erase(symbol);
        }
        
        // Remove all detached threads
        for (const auto& symbol : detachedThreads) {
            m_modelThreads.erase(symbol);
            m_threadRunFlags.erase(symbol);
        }
    }
    
    std::cout << "[AppState] Emergency stop completed: " << joinedThreads.size() 
              << " threads stopped normally, " << detachedThreads.size() 
              << " threads detached due to timeout" << std::endl;
}

// Destructor - ensure all threads are stopped
AppState::~AppState() {
    // Use emergency stop to ensure all threads are stopped, even hanging ones
    emergencyStopAllThreads(2000); // 2 second timeout
}

} // namespace app_state

