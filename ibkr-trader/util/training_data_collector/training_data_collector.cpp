// ═══════════════════════════════════════════════════════════════════════════════
// Training Data Collector - Optimized Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "training_data_collector.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <cstdio>
#include <cinttypes>

TrainingDataCollector::TrainingDataCollector(std::string symbol, 
                                           std::string baseDir, 
                                           std::size_t rowsPerFile)
    : m_symbol(std::move(symbol)),
      m_baseDir(std::move(baseDir)),
      m_rowsPerFile(rowsPerFile)
{
    if (m_symbol.empty())
        throw std::invalid_argument("symbol must not be empty");
    
    std::filesystem::create_directories(m_baseDir);
}

TrainingDataCollector::~TrainingDataCollector()
{
    stop();
    std::lock_guard<std::mutex> g(m_mutex);
    if (m_gzFile)
        gzclose(m_gzFile);
}

void TrainingDataCollector::processTick(const stk_q::STK_Q_Data& tick)
{
    if (!m_isRunning.load())
        return;
        
    std::lock_guard<std::mutex> g(m_mutex);
    
    if (!m_gzFile)
        throw std::runtime_error("writer not initialized (file closed)");
    
    if (m_currentRowCount >= m_rowsPerFile)
        rotateFile();
    
    writeTickToFile(tick);
    ++m_currentRowCount;
}

void TrainingDataCollector::start()
{
    std::lock_guard<std::mutex> g(m_mutex);
    m_isRunning = true;
    
    if (!m_gzFile)
        rotateFile();  // Open first file
    
    std::clog << "[TrainingDataCollector] Started for symbol: " << m_symbol << std::endl;
}

void TrainingDataCollector::stop()
{
    m_isRunning = false;
    std::clog << "[TrainingDataCollector] Stopped" << std::endl;
}

bool TrainingDataCollector::isRunning() const
{
    return m_isRunning.load();
}

void TrainingDataCollector::writeTickToFile(const stk_q::STK_Q_Data& d)
{
    char buffer[2048];
    int len = std::snprintf(
        buffer, sizeof(buffer),
        "%s,%" PRIu64 ",%s,%.10f,%.10f,%.10f,%d,%d,%d,%.10f,%.10f,%.10f,"
        "%.10f,%.10f,%.10f,%.10f,%.10f,%.10f,%.4f,%.10f,%.10f,%.10f,%.10f\n",
        d.symbol.c_str(),
        d.time,
        d.exchange.c_str(),
        d.last,
        d.bid,
        d.ask,
        d.bidSize,
        d.askSize,
        d.volume,
        d.open,
        d.high,
        d.low,
        d.close,
        d.mid,
        d.spread,
        d.spreadPercent,
        d.vwap,
        d.imbalance,
        d.rsi,
        d.ema9,
        d.ema26,
        d.alma,
        d.atr);

    if (len <= 0 || len >= static_cast<int>(sizeof(buffer)))
        throw std::runtime_error("CSV line length overflow");

    if (gzwrite(m_gzFile, buffer, len) != len)
        throw std::runtime_error("gzwrite failed (disk full?)");
}

void TrainingDataCollector::rotateFile()
{
    if (m_gzFile) {
        gzflush(m_gzFile, Z_SYNC_FLUSH);
        gzclose(m_gzFile);
        m_gzFile = nullptr;
    }

    const std::string filename = generateFilename();
    m_gzFile = gzopen(filename.c_str(), ("wb" + std::to_string(ZLEVEL)).c_str());
    if (!m_gzFile)
        throw std::runtime_error("unable to open " + filename);

    // Write header
    static constexpr char HEADER[] =
        "symbol,time,exchange,last,bid,ask,bidSize,askSize,volume,"
        "open,high,low,close,mid,spread,spreadPercent,vwap,imbalance,"
        "rsi,ema9,ema26,alma,atr\n";
    if (gzwrite(m_gzFile, HEADER, sizeof(HEADER) - 1) != sizeof(HEADER) - 1)
        throw std::runtime_error("failed to write CSV header");

    m_currentRowCount = 0;
}

std::string TrainingDataCollector::generateFilename()
{
    const std::string ts = formatTimestamp();  // YYYYMMDD_hhmmss

    const std::string day = ts.substr(0, 8);
    if (day != m_lastDay) {
        m_fileSequence = 0;
        m_lastDay = day;
    }

    std::ostringstream oss;
    oss << m_baseDir << '/'
        << m_symbol << '_' << ts << '_'
        << std::setw(4) << std::setfill('0') << m_fileSequence++
        << ".csv.gz";
    return oss.str();
}

std::string TrainingDataCollector::formatTimestamp()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t t = clock::to_time_t(now);

    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (tm_buf.tm_year + 1900)
        << std::setw(2) << (tm_buf.tm_mon + 1)
        << std::setw(2) << tm_buf.tm_mday << '_'
        << std::setw(2) << tm_buf.tm_hour
        << std::setw(2) << tm_buf.tm_min
        << std::setw(2) << tm_buf.tm_sec;
    return oss.str();
}