// stk_q.cpp

#include "stk_q.hpp"
#include <iostream>

namespace stk_q
{
    STK_Q::STK_Q() {
        // constructor
    }
    
    STK_Q::~STK_Q() {
        // destructor
    }
    
    void STK_Q::push(STK_Q_Data& data) {
        // push data into the queue (copy version)
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(data);
    }
    
    void STK_Q::push(STK_Q_Data&& data) {
        // push data into the queue (move version for efficiency)
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(data));
    }
    
    bool STK_Q::pop(STK_Q_Data& outData){
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_queue.empty()){
            return false;
        }

        outData = m_queue.front();
        m_queue.pop();
        return true;
    }

    bool STK_Q::peek(STK_Q_Data& outData) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        if(m_queue.empty()){
            return false;
        }

        outData = m_queue.front();
        return true;
    }

    bool STK_Q::empty() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        return m_queue.empty();
    }

    size_t STK_Q::size() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        return m_queue.size();
    }
    
    void STK_Q::clear(){
        std::lock_guard<std::mutex> lock(m_mutex);
        std::queue<STK_Q_Data> empty;
        std::swap(m_queue, empty);
    }

    void STK_Q::print() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
        // Create a copy of the queue for printing
        std::queue<STK_Q_Data> temp = m_queue;
        while(!temp.empty()){
            STK_Q_Data data = temp.front();
            temp.pop();
            std::cout << "Symbol: " 
                    << data.symbol << ", Price: " 
                    << data.price << ", Time: " 
                    << data.time << ", Size: " 
                    << data.size << ", Exchange: " 
                    << data.exchange 
                    << std::endl;
        }
    }
    
    void STK_Q::removeOlderThan(uint64_t cutoffTime) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // If queue is empty, nothing to do
        if (m_queue.empty()) {
            return;
        }
        
        // Create a temporary queue to hold items newer than the cutoff time
        std::queue<STK_Q_Data> newQueue;
        
        // Process all elements from the current queue
        while (!m_queue.empty()) {
            STK_Q_Data data = m_queue.front();
            m_queue.pop();
            
            // Only keep data items that are newer than the cutoff time
            if (static_cast<uint64_t>(data.time) >= cutoffTime) {
                newQueue.push(std::move(data));
            }
        }
        
        // Replace the old queue with the filtered one
        m_queue = std::move(newQueue);
    }
}// namespace stk_q