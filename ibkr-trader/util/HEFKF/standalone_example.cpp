// Standalone Timescale-Specific Filters Example
// Demonstrates completely decoupled 1min and 5min filters

#include "1min_HEFKF.hpp"
#include "5min_HEFKF.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

int main() {
    std::cout << "=== Standalone Timescale-Specific Filters Demo ===\n\n";
    
    // ─────────────────────── Create standalone filters ───────────────────────
    std::cout << "1. Creating standalone filters (no dependencies on original HEFKF)...\n";
    hefkf_1min::OneMinuteHEFKF filter_1min;      // Completely standalone 1min filter
    hefkf_5min::FiveMinuteHEFKF filter_5min;     // Completely standalone 5min filter
    
    std::cout << "   ✓ 1min filter created in namespace hefkf_1min\n";
    std::cout << "   ✓ 5min filter created in namespace hefkf_5min\n\n";
    
    // ─────────────────────── Show configuration differences ───────────────────────
    std::cout << "2. Configuration comparison:\n";
    auto config_1min = hefkf_1min::OneMinuteHEFKF::get_config_info();
    auto config_5min = hefkf_5min::FiveMinuteHEFKF::get_config_info();
    
    std::cout << "   1min - P_SCALE: " << config_1min.INITIAL_P_SCALE 
              << ", R_PRICE: " << config_1min.R_PRICE 
              << ", FREQ_WEIGHT: " << config_1min.FREQUENCY_DOMAIN_WEIGHT << "\n";
    
    std::cout << "   5min - P_SCALE: " << config_5min.INITIAL_P_SCALE 
              << ", R_PRICE: " << config_5min.R_PRICE 
              << ", FREQ_WEIGHT: " << config_5min.FREQUENCY_DOMAIN_WEIGHT << "\n\n";
    
    // ─────────────────────── Initialize filters ───────────────────────
    std::cout << "3. Initializing filters with the same market data...\n";
    
    hefkf_1min::MarketData initial_data_1min;
    initial_data_1min.price = 100.0;
    initial_data_1min.volume = 1000.0;
    initial_data_1min.spread = 0.02;
    initial_data_1min.timestamp = std::chrono::system_clock::now();
    
    hefkf_5min::MarketData initial_data_5min;
    initial_data_5min.price = 100.0;
    initial_data_5min.volume = 1000.0;
    initial_data_5min.spread = 0.02;
    initial_data_5min.timestamp = std::chrono::system_clock::now();
    
    // Both use dt=1.0 for 1-second data ingestion
    filter_1min.initialize(initial_data_1min, 1.0);
    filter_5min.initialize(initial_data_5min, 1.0);
    
    std::cout << "   ✓ Both filters initialized with dt=1.0\n";
    std::cout << "   ✓ Same price data but different behavioral parameters\n\n";
    
    // ─────────────────────── Process sample data ───────────────────────
    std::cout << "4. Processing sample data (showing behavioral differences):\n";
    std::cout << "   Input Price -> 1min_Output | 5min_Output\n";
    
    for (int i = 1; i <= 10; ++i) {
        // Create market data with some noise and trend
        double base_price = 100.0 + i * 0.05;
        double noise = 0.02 * std::sin(i * 0.5);
        double price = base_price + noise;
        
        hefkf_1min::MarketData data_1min;
        data_1min.price = price;
        data_1min.volume = 1000.0 + i * 25;
        data_1min.spread = 0.02 + 0.0005 * i;
        data_1min.timestamp = std::chrono::system_clock::now();
        
        hefkf_5min::MarketData data_5min;
        data_5min.price = price;
        data_5min.volume = 1000.0 + i * 25;
        data_5min.spread = 0.02 + 0.0005 * i;
        data_5min.timestamp = std::chrono::system_clock::now();
        
        auto output_1min = filter_1min.process(data_1min);
        auto output_5min = filter_5min.process(data_5min);
        
        std::cout << "   " << std::fixed << std::setprecision(4) 
                  << price << " -> " 
                  << output_1min.price_smoothed << " | " 
                  << output_5min.price_smoothed << "\n";
    }
    
    std::cout << "\n5. Key Observations:\n";
    std::cout << "   - 1min filter: More responsive to price changes (smaller smoothing)\n";
    std::cout << "   - 5min filter: More stable output (larger smoothing)\n";
    std::cout << "   - Both are completely independent of the original HEFKF\n";
    std::cout << "   - Each has its own namespace and data structures\n";
    std::cout << "   - No shared dependencies or cross-contamination\n\n";
    
    std::cout << "6. Memory footprint comparison:\n";
    std::cout << "   - Each filter contains its own complete implementation\n";
    std::cout << "   - All utilities and helper classes are self-contained\n";
    std::cout << "   - Zero dependency on hybrid_exp_forgetting_kalman_filter.hpp\n\n";
    
    std::cout << "=== Standalone Demo Complete ===\n";
    
    return 0;
} 