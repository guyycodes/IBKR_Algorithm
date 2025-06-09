// stk_q.hpp

#ifndef STK_Q_HPP
#define STK_Q_HPP

#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <iostream>
#include <vector>

namespace stk_q
{
    struct STK_Q_Data
    {
        // Core identification
        std::string symbol;
        uint64_t time;
        std::string exchange;

        // Core market data (from stock_data_tick::StockData)
        double bid = 0.0;
        double ask = 0.0;
        double last = 0.0;
        int bidSize = 0;
        int askSize = 0;
        int lastSize = 0;
        int volume = 0;

        // OHLC data
        double open = 0.0;
        double high = 0.0;
        double low = 0.0;
        double close = 0.0;

        // Derived metrics
        double mid = 0.0;
        double spread = 0.0;
        double spreadPercent = 0.0;
        double vwap = 0.0;
        double imbalance = 0.0;

        // Technical indicators
        double rsi = 0.0;
        double ema9 = 0.0;
        double ema26 = 0.0;
        double alma = 0.0;
        double atr = 0.0;


        // Backward compatibility - keep original fields
        double price = 0.0;  // Maps to 'last' for compatibility
        int size = 0;        // Maps to 'volume' for compatibility
    };

    class STK_Q {
        private:
            // Changed from std::queue to std::map for chronological ordering
            // Key = timestamp (rounded to 500ms intervals), Value = tick data
            // std::map automatically sorts by timestamp (chronological order)
            // FILTERING: Only stores 1 tick per 500ms interval (2 ticks per second max)
            std::map<long, STK_Q_Data> m_orderedData;
            std::mutex m_mutex;

        public:
            STK_Q();
            ~STK_Q();
            
            // Queue operations (modified for chronological ordering + 500ms filtering)
            void push(STK_Q_Data& data);    // Stores tick only if 500ms interval is empty
            void push(STK_Q_Data&& data);   // Stores tick only if 500ms interval is empty
            bool pop(STK_Q_Data& data);     // Pops OLDEST (earliest timestamp)

            // Access operations
            bool peek(STK_Q_Data& data) const;      // Peeks at OLDEST (earliest timestamp)
            bool peekLatest(STK_Q_Data& data) const; // Peeks at NEWEST (latest timestamp)
            bool empty() const;
            size_t size() const;

            // Utility operations
            void clear();
            void print() const;
            
            // Non-destructive data access
            std::vector<STK_Q_Data> getAllData() const;
            
            // Time-based pruning
            void removeOlderThan(uint64_t cutoffTime);
    };
}

#endif 