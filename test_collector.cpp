#include "ibkr-trader/util/training_data_collector/training_data_collector.hpp"
#include "ibkr-trader/util/stk_q/stk_q.hpp"
#include <chrono>
#include <iostream>

int main() {
    try {
        // Test with smaller rotation limit for demo
        TrainingDataCollector collector("AAPL", "/tmp/test_data", 10);
        
        stk_q::STK_Q_Data tick{};
        tick.symbol = "AAPL";
        tick.exchange = "NASDAQ";
        tick.last = 182.56;
        tick.bid = 182.55;
        tick.ask = 182.57;
        tick.bidSize = 100;
        tick.askSize = 200;
        tick.volume = 1000;
        tick.time = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        
        collector.start();
        
        // Write 25 ticks to test rotation (should create 3 files: 10+10+5)
        for (int i = 0; i < 25; ++i) {
            tick.last += 0.01 * i;  // Vary the price
            tick.time += 1000;      // Add 1 second
            collector.processTick(tick);
        }
        
        collector.stop();
        
        std::cout << "Test completed successfully! Check /tmp/test_data for .csv.gz files" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
} 