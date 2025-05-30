// stk_q.cpp

#include "stk_q.hpp"
#include <iostream>

namespace stk_q
{
    // Data filtering interval - keeps only one tick per interval (in milliseconds)
    // 250ms = 4 ticks per second, 500ms = 2 ticks per second, 125ms = 8 ticks per second
    static constexpr int64_t TICK_INTERVAL_MS = 250;

    STK_Q::STK_Q() {
        // constructor
    }
    
    STK_Q::~STK_Q() {
        // destructor
    }
    
    void STK_Q::push(STK_Q_Data& data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int64_t originalTime = data.time;
        int64_t roundedTime  = (originalTime / TICK_INTERVAL_MS) * TICK_INTERVAL_MS;

        // Skip if we already have a tick for this interval
        if (m_orderedData.find(roundedTime) == m_orderedData.end()) {
            // Make a local copy so we don't mutate the caller's data
            STK_Q_Data copy = data;
            copy.time = roundedTime;
            m_orderedData.emplace(roundedTime, std::move(copy));

            // Log every 10th stored tick
            if (m_orderedData.size() % 10 == 0) {
                std::cout << "[STK_Q] Stored tick. original=" << originalTime
                          << "ms rounded=" << roundedTime
                          << "ms, size=" << m_orderedData.size() << std::endl;
            }
        }
    }
    
    void STK_Q::push(STK_Q_Data&& data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int64_t originalTime = data.time;
        int64_t roundedTime  = (originalTime / TICK_INTERVAL_MS) * TICK_INTERVAL_MS;

        if (m_orderedData.find(roundedTime) == m_orderedData.end()) {
            data.time = roundedTime;
            m_orderedData.emplace(roundedTime, std::move(data));
            if (m_orderedData.size() % 10 == 0) {
                std::cout << "[STK_Q] Stored tick. original=" << originalTime
                          << "ms rounded=" << roundedTime
                          << "ms, size=" << m_orderedData.size() << std::endl;
            }
        }
    }
    bool STK_Q::pop(STK_Q_Data& outData){
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_orderedData.empty()){
            return false;
        }

        // Get the OLDEST data (earliest timestamp) from beginning of map
        auto it = m_orderedData.begin();
        outData = it->second;
        m_orderedData.erase(it);
        return true;
    }

    // Peek at the OLDEST data (earliest timestamp) from beginning of map
    bool STK_Q::peek(STK_Q_Data& outData) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        if (m_orderedData.empty()) {
            return false;
        }
        outData = m_orderedData.begin()->second; // First element (earliest timestamp)
        return true;
    }

    // Peek at the NEWEST data (latest timestamp) from end of map 
    bool STK_Q::peekLatest(STK_Q_Data& outData) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        if (m_orderedData.empty()) {
            return false;
        }
        outData = m_orderedData.rbegin()->second; // Last element (latest timestamp)
        return true;
    }

    bool STK_Q::empty() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        return m_orderedData.empty();
    }

    size_t STK_Q::size() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        return m_orderedData.size();
    }
    
    void STK_Q::clear(){
        std::lock_guard<std::mutex> lock(m_mutex);
        m_orderedData.clear();
    }

    void STK_Q::print() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        // Print all data in chronological order with rich market data
        for (const auto& [timestamp, data] : m_orderedData) {
            std::cout << "Symbol: " << data.symbol 
                      << ", Time: " << data.time
                      << ", Last: " << data.last
                      << ", Bid: " << data.bid << "x" << data.bidSize
                      << ", Ask: " << data.ask << "x" << data.askSize
                      << ", Volume: " << data.volume
                      << ", VWAP: " << data.vwap
                      << ", Exchange: " << data.exchange 
                      << std::endl;
        }
    }
    
    void STK_Q::removeOlderThan(uint64_t cutoffTime) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // If map is empty, nothing to do
        if (m_orderedData.empty()) {
            return;
        }
        
        // Since map is sorted by timestamp, we can efficiently remove old entries
        // from the beginning until we reach the cutoff time
        auto it = m_orderedData.begin();
        while (it != m_orderedData.end() && static_cast<uint64_t>(it->first) < cutoffTime) {
            it = m_orderedData.erase(it);  // erase returns iterator to next element
        }
    }
}// namespace stk_q