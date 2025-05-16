// stk_q.hpp

#ifndef STK_Q_HPP
#define STK_Q_HPP

#include <queue>
#include <string>
#include <memory>
#include <mutex>
#include <iostream>

namespace stk_q
{
    struct STK_Q_Data
    {
        std::string symbol;
        double price;
        long time;
        int size;
        std::string exchange;
    };

    class STK_Q {
        private:
            std::queue<STK_Q_Data> m_queue;
            std::mutex m_mutex;

        public:
            STK_Q();
            ~STK_Q();
            
            // Queue operations
            void push(STK_Q_Data& data);
            void push(STK_Q_Data&& data);
            bool pop(STK_Q_Data& data);

            // Access operations
            bool peek(STK_Q_Data& data) const;
            bool empty() const;
            size_t size() const;

            // Utility operations
            void clear();
            void print() const;
            
            // Time-based pruning
            void removeOlderThan(uint64_t cutoffTime);
    };
}

#endif 