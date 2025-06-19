#include "integration_loop_wrapper.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>

namespace integration_loop_wrapper
{

IntegrationLoop::IntegrationLoop(const std::string& csvPath,
                                 double feedFrequency,
                                 size_t bufferSize)
    : m_pipeline(bufferSize)
{
    // Calculate feed interval from frequency
    // e.g., 1 Hz = 1000ms, 2 Hz = 500ms, 4 Hz = 250ms
    m_feedInterval = std::chrono::milliseconds(
        static_cast<int>(1000.0 / feedFrequency)
    );
    
    // Load CSV data
    if (!loadCSVData(csvPath)) {
        std::cerr << "[IntegrationLoop] ⚠️ Failed to load CSV data from: " << csvPath << "\n";
    } else {
        std::cout << "[IntegrationLoop] ✅ Loaded " << m_testTicks.size() 
                  << " ticks from CSV\n";
    }
    
    std::cout << "[IntegrationLoop] Configured with feed frequency: " 
              << feedFrequency << " Hz (interval: " 
              << m_feedInterval.count() << " ms)\n";
}

IntegrationLoop::IntegrationLoop(const std::vector<KalmanTick>& ticks,
                                 double feedFrequency,
                                 size_t bufferSize)
    : m_pipeline(bufferSize)
    , m_testTicks(ticks)
{
    // Calculate feed interval from frequency
    m_feedInterval = std::chrono::milliseconds(
        static_cast<int>(1000.0 / feedFrequency)
    );
    
    std::cout << "[IntegrationLoop] Initialized with " << m_testTicks.size() 
              << " pre-loaded ticks\n";
    std::cout << "[IntegrationLoop] Feed frequency: " << feedFrequency 
              << " Hz (interval: " << m_feedInterval.count() << " ms)\n";
}

IntegrationLoop::~IntegrationLoop()
{
    stop();  // Ensure clean shutdown
}

void IntegrationLoop::start()
{
    if (m_running.exchange(true)) {
        std::cerr << "[IntegrationLoop] ⚠️ Already running\n";
        return;
    }
    
    // Start the ProcessingLoop
    m_pipeline.start();
    
    // Reset index for fresh start
    m_csvIndex = 0;
    m_ticks_fed.store(0);
    
    // Start our feeder thread
    m_feederThread = std::thread(&IntegrationLoop::feederLoop, this);
    
    std::cout << "[IntegrationLoop] 🚀 Started feeder thread\n";
}

void IntegrationLoop::stop()
{
    if (!m_running.exchange(false)) {
        // Already stopped
        return;
    }
    
    // Wake up the feeder thread
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_cv.notify_all();
    }
    
    // Join the thread
    if (m_feederThread.joinable()) {
        m_feederThread.join();
    }
    
    // Stop the ProcessingLoop
    m_pipeline.stop();
    
    std::cout << "[IntegrationLoop] ✅ Stopped (fed " 
              << m_ticks_fed.load() << " of " << m_testTicks.size() 
              << " ticks)\n";
}

bool IntegrationLoop::loadCSVData(const std::string& csvPath)
{
    std::ifstream fin(csvPath);
    if (!fin.is_open()) {
        return false;
    }
    
    std::string line;
    bool firstLine = true;
    
    while (std::getline(fin, line)) {
        if (firstLine) {
            firstLine = false;
            continue;  // Skip header row
        }
        
        if (!line.empty()) {
            try {
                KalmanTick tick = parseCSVLine(line);
                if (isValidTick(tick)) {
                    m_testTicks.push_back(tick);
                }
            } catch (const std::exception& e) {
                std::cerr << "[IntegrationLoop] Error parsing CSV line: " << e.what() << "\n";
            }
        }
    }
    
    return !m_testTicks.empty();
}

KalmanTick IntegrationLoop::parseCSVLine(const std::string& line)
{
    KalmanTick tick{};
    
    // CSV columns: Symbol, Timestamp, Last, Bid, Ask, BidSize, AskSize, Volume, 
    //             Open, High, Low, Close, Spread, VWAP, RSI, ...
    
    std::stringstream ss(line);
    std::string token;
    int colIndex = 0;
    
    while (std::getline(ss, token, ',')) {
        try {
            switch (colIndex) {
                case 1: // Timestamp
                    tick.ts = static_cast<uint64_t>(std::stoll(token));
                    break;
                case 2: // Last price
                    tick.px = std::stod(token);
                    break;
                case 3: // Bid
                    tick.bid = std::stod(token);
                    break;
                case 4: // Ask
                    tick.ask = std::stod(token);
                    break;
                case 7: // Volume
                    tick.volume = std::stod(token);
                    break;
                case 12: // Spread (if not calculated)
                    tick.spread = std::stod(token);
                    break;
                default:
                    // Skip other columns
                    break;
            }
        } catch (const std::exception&) {
            // Skip invalid values
        }
        ++colIndex;
    }
    
    // Calculate spread if not provided
    if (tick.spread == 0.0 && tick.ask > tick.bid) {
        tick.spread = tick.ask - tick.bid;
    }
    
    return tick;
}

bool IntegrationLoop::isValidTick(const KalmanTick& tick)
{
    // Use KalmanTick's built-in validation
    return tick.is_valid();
}

void IntegrationLoop::feederLoop()
{
    std::unique_lock<std::mutex> lock(m_mtx);
    
    std::cout << "[IntegrationLoop] 📁 Feeder thread started - " 
              << m_testTicks.size() << " ticks ready to feed at " 
              << (1000.0 / m_feedInterval.count()) << " Hz\n";
    
    auto lastFeedTime = std::chrono::steady_clock::now();
    
    while (m_running.load() && m_csvIndex < m_testTicks.size()) {
        // Calculate next feed time
        auto nextFeedTime = lastFeedTime + m_feedInterval;
        
        // Wait until next feed time or until signaled to stop
        m_cv.wait_until(lock, nextFeedTime, [this] {
            return !m_running.load();
        });
        
        if (!m_running.load()) break;
        
        // Check if we've waited long enough
        auto now = std::chrono::steady_clock::now();
        if (now < nextFeedTime) {
            continue;  // Not time yet
        }
        
        // Update last feed time
        lastFeedTime = now;
        
        // Unlock while feeding
        lock.unlock();
        
        // ─────────────────────────────────────────
        // Feed next tick from CSV into ProcessingLoop
        // ─────────────────────────────────────────
        if (m_csvIndex < m_testTicks.size()) {
            const KalmanTick& csvTick = m_testTicks[m_csvIndex++];
            
            // Add tick to ProcessingLoop's ring buffer
            m_pipeline.add_tick(csvTick);
            
            // Process it immediately
            bool processed = m_pipeline.process_latest();
            
            if (processed) {
                m_ticks_fed.fetch_add(1);
                
                if (m_debug_mode) {
                    if (m_ticks_fed % 10 == 0) {
                        std::cout << "\n[IntegrationLoop] Fed " << m_ticks_fed.load() 
                                  << " of " << m_testTicks.size() << " ticks\n";
                    }
                }
            } else if (m_debug_mode) {
                std::cerr << "[IntegrationLoop] Failed to process tick #" 
                          << m_csvIndex << "\n";
            }
            
            // Log when all data is exhausted
            if (m_csvIndex == m_testTicks.size()) {
                std::cout << "\n📊 [IntegrationLoop] All " << m_testTicks.size() 
                          << " CSV ticks have been fed into the system\n";
                std::cout << "   • Total fed: " << m_ticks_fed.load() << "\n";
                std::cout << "   • Total processed: " << m_pipeline.get_processed_count() << "\n";
            }
        }
        
        // Re-lock for next iteration
        lock.lock();
    }
    
    std::cout << "[IntegrationLoop] Feeder thread exiting\n";
}

} // namespace integration_loop_wrapper