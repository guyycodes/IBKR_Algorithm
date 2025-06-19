#ifndef INTEGRATION_LOOP_WRAPPER_HPP
#define INTEGRATION_LOOP_WRAPPER_HPP

#include "integration_loop.hpp"  // For ProcessingLoop
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <string>

namespace integration_loop_wrapper
{

/**
 * IntegrationLoop
 * ---------------
 * Self-contained CSV data loader and feeder for HEFKF testing.
 * Loads CSV market data and feeds it at a controlled rate into the ProcessingLoop.
 * This allows testing the HEFKF pipeline without external dependencies.
 * 
 * Data flow:
 * CSV File → IntegrationLoop → ProcessingLoop ring buffer → FilterPipeline
 */
class IntegrationLoop
{
public:
    /**
     * Constructor with CSV file path
     * @param csvPath Path to CSV file containing market data
     * @param feedFrequency How often to feed ticks to ProcessingLoop (Hz)
     * @param bufferSize Size of ProcessingLoop's internal buffer
     */
    IntegrationLoop(const std::string& csvPath,
                    double feedFrequency = 2.0,
                    size_t bufferSize = 4096);
    
    /**
     * Constructor with pre-loaded tick data (for testing)
     * @param ticks Vector of pre-loaded KalmanTick data
     * @param feedFrequency How often to feed ticks to ProcessingLoop (Hz)
     * @param bufferSize Size of ProcessingLoop's internal buffer
     */
    IntegrationLoop(const std::vector<KalmanTick>& ticks,
                    double feedFrequency = 2.0,
                    size_t bufferSize = 4096);
    
    /**
     * Destructor - ensures clean shutdown
     */
    ~IntegrationLoop();
    
    /**
     * Start the feeder thread
     */
    void start();
    
    /**
     * Stop the feeder thread
     */
    void stop();
    
    /**
     * Check if the loop is running
     */
    bool is_running() const { return m_running.load(); }
    
    /**
     * Get the ProcessingLoop for direct access
     */
    ProcessingLoop& get_processing_loop() { return m_pipeline; }
    const ProcessingLoop& get_processing_loop() const { return m_pipeline; }
    
    /**
     * Get latest pipeline output
     */
    const FilterPipeline::PipelineOutput& get_latest_output() const { 
        return m_pipeline.get_latest_output(); 
    }
    
    /**
     * Get statistics
     */
    size_t get_processed_count() const { return m_pipeline.get_processed_count(); }
    size_t get_ticks_fed() const { return m_ticks_fed.load(); }
    size_t get_total_ticks() const { return m_testTicks.size(); }
    bool is_data_exhausted() const { return m_csvIndex >= m_testTicks.size(); }
    
    /**
     * Enable/disable debug output
     */
    void set_debug_mode(bool enable) { 
        m_debug_mode = enable;
        m_pipeline.set_debug_mode(enable);
    }

private:
    /**
     * Load CSV data from file
     * @param csvPath Path to CSV file
     * @return true if successful
     */
    bool loadCSVData(const std::string& csvPath);
    
    /**
     * Parse a CSV line into KalmanTick
     * @param line CSV line to parse
     * @return Parsed KalmanTick
     */
    static KalmanTick parseCSVLine(const std::string& line);
    
    /**
     * Validate tick data
     * @param tick Tick to validate
     * @return true if valid
     */
    static bool isValidTick(const KalmanTick& tick);
    
    /**
     * Main feeder thread function
     */
    void feederLoop();

private:
    // Our processing pipeline
    ProcessingLoop m_pipeline;
    
    // CSV data storage
    std::vector<KalmanTick> m_testTicks;
    std::size_t m_csvIndex{0};
    
    // Thread management
    std::atomic<bool>       m_running{false};
    std::thread             m_feederThread;
    std::mutex              m_mtx;
    std::condition_variable m_cv;
    
    // Feed interval (e.g., 1Hz = 1000ms, 2Hz = 500ms, 4Hz = 250ms)
    std::chrono::milliseconds m_feedInterval{500};
    
    // Statistics
    std::atomic<size_t> m_ticks_fed{0};
    
    // Debug mode
    bool m_debug_mode{false};
};

} // namespace integration_loop_wrapper

#endif // INTEGRATION_LOOP_WRAPPER_HPP 