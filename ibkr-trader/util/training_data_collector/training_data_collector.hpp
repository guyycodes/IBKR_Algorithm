#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// Training Data Collector - Optimized with gzip compression and file rotation
// ═══════════════════════════════════════════════════════════════════════════════

#include <string>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <zlib.h>
#include "../stk_q/stk_q.hpp"

class TrainingDataCollector {
public:
    explicit TrainingDataCollector(std::string symbol, 
                                 std::string baseDir = "/workspace/ibkr-trader/raw_data",
                                 std::size_t rowsPerFile = 200000);
    ~TrainingDataCollector();
    
    // Process and write tick data to file
    void processTick(const stk_q::STK_Q_Data& tick);
    
    // Control methods
    void start();
    void stop();
    bool isRunning() const;

private:
    static constexpr int ZLEVEL = 9;  // maximum gzip compression
    
    std::string m_symbol;
    std::string m_baseDir;
    std::size_t m_rowsPerFile;
    
    gzFile m_gzFile{nullptr};
    std::size_t m_currentRowCount{0};
    std::size_t m_fileSequence{0};
    std::string m_lastDay;  // YYYYMMDD for sequence reset
    
    std::atomic<bool> m_isRunning{false};
    std::mutex m_mutex;  // thread safety for all operations
    
    // Core methods from legacy writer
    void writeTickToFile(const stk_q::STK_Q_Data& data);
    void rotateFile();
    std::string generateFilename();
    static std::string formatTimestamp();
};
