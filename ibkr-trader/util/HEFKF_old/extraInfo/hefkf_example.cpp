// Example usage of the Hybrid Exponential Forgetting Kalman Filter
// This demonstrates basic usage patterns and configuration options

#include "hybrid_exp_forgetting_kalman_filter.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <iomanip>

using namespace hefkf;

// Generate synthetic market data for testing
std::vector<MarketData> generate_test_data(size_t num_points) {
    std::vector<MarketData> data;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> price_noise(0.0, 0.1);
    std::normal_distribution<> volume_noise(0.0, 100.0);
    std::normal_distribution<> spread_noise(0.0, 0.001);
    
    double base_price = 100.0;
    double base_volume = 1000.0;
    double base_spread = 0.01;
    
    for (size_t i = 0; i < num_points; ++i) {
        MarketData market_data;
        
        // Add some trend and noise
        double trend = 0.001 * std::sin(0.1 * i);  // Small sinusoidal trend
        market_data.price = base_price + trend + price_noise(gen);
        market_data.volume = base_volume + volume_noise(gen);
        market_data.spread = base_spread + spread_noise(gen);
        market_data.timestamp = std::chrono::system_clock::now() + std::chrono::seconds(i);
        
        // Add some bucket confidence data occasionally
        if (i % 10 == 0) {
            market_data.bucket_conf = std::make_unique<BucketConfidence>();
            market_data.bucket_conf->up_001_002 = 0.3;
            market_data.bucket_conf->up_002_005 = 0.2;
            market_data.bucket_conf->dn_001_002 = 0.2;
            market_data.bucket_conf->dn_002_005 = 0.1;
            // Leave others as 0.0 for simplicity
        }
        
        data.push_back(std::move(market_data));
    }
    
    return data;
}

void demonstrate_basic_usage() {
    std::cout << "=== Basic Usage Example ===" << std::endl;
    
    // Create filter with default configuration
    HKFConfig config;
    config.lambda_fixed = 0.99;           // 100-tick memory
    config.lambda_adaptive = false;       // Use fixed lambda for this example
    config.bucket_weight = 0.3;           // Moderate bucket influence
    
    HybridExpForgettingKalmanFilter filter(config);
    
    // Generate test data
    auto test_data = generate_test_data(50);
    
    // Initialize filter with first data point
    filter.initialize(test_data[0], 1.0);
    
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Step | Raw Price | Smoothed | Velocity | Lambda" << std::endl;
    std::cout << "-----|-----------|----------|----------|--------" << std::endl;
    
    // Process each data point
    for (size_t i = 1; i < std::min(test_data.size(), size_t(10)); ++i) {
        auto output = filter.process(test_data[i]);
        
        std::cout << std::setw(4) << i << " | "
                  << std::setw(9) << test_data[i].price << " | "
                  << std::setw(8) << output.price_smoothed << " | "
                  << std::setw(8) << output.price_velocity << " | "
                  << std::setw(6) << output.lambda_used << std::endl;
    }
    
    std::cout << std::endl;
}

void demonstrate_adaptive_lambda() {
    std::cout << "=== Adaptive Lambda Example ===" << std::endl;
    
    // Create filter with adaptive lambda
    HKFConfig config;
    config.lambda_fixed = 0.99;
    config.lambda_adaptive = true;        // Enable adaptive lambda
    config.lambda_min = 0.95;            // Fast adaptation in high volatility
    config.lambda_max = 0.995;           // Slow adaptation in low volatility
    config.bucket_weight = 0.0;          // Disable bucket influence for clarity
    
    HybridExpForgettingKalmanFilter filter(config);
    
    // Generate test data with some volatility spikes
    auto test_data = generate_test_data(20);
    
    // Add some volatility spikes
    for (size_t i = 5; i < 10; ++i) {
        test_data[i].price += (i % 2 == 0 ? 1.0 : -1.0);  // Add large moves
    }
    
    filter.initialize(test_data[0], 1.0);
    
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Step | Raw Price | Smoothed | Lambda | Volatility" << std::endl;
    std::cout << "-----|-----------|-----------|---------|-----------" << std::endl;
    
    double last_price = test_data[0].price;
    
    for (size_t i = 1; i < test_data.size(); ++i) {
        auto output = filter.process(test_data[i]);
        
        double volatility = std::abs(test_data[i].price - last_price) / last_price;
        last_price = test_data[i].price;
        
        std::cout << std::setw(4) << i << " | "
                  << std::setw(9) << test_data[i].price << " | "
                  << std::setw(8) << output.price_smoothed << " | "
                  << std::setw(6) << output.lambda_used << " | "
                  << std::setw(9) << volatility << std::endl;
    }
    
    std::cout << std::endl;
}

void demonstrate_bucket_control() {
    std::cout << "=== Bucket Control Example ===" << std::endl;
    
    // Create filter with strong bucket influence
    HKFConfig config;
    config.lambda_fixed = 0.99;
    config.lambda_adaptive = false;
    config.bucket_weight = 0.8;           // Strong bucket influence
    
    HybridExpForgettingKalmanFilter filter(config);
    
    // Generate test data
    auto test_data = generate_test_data(10);
    
    // Add strong bucket signals
    for (size_t i = 0; i < test_data.size(); ++i) {
        test_data[i].bucket_conf = std::make_unique<BucketConfidence>();
        
        if (i < 5) {
            // Strong upward signal
            test_data[i].bucket_conf->up_002_005 = 0.6;
            test_data[i].bucket_conf->up_001_002 = 0.3;
            test_data[i].bucket_conf->dn_001_002 = 0.1;
        } else {
            // Strong downward signal
            test_data[i].bucket_conf->dn_002_005 = 0.6;
            test_data[i].bucket_conf->dn_001_002 = 0.3;
            test_data[i].bucket_conf->up_001_002 = 0.1;
        }
    }
    
    filter.initialize(test_data[0], 1.0);
    
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Step | Raw Price | Smoothed | Velocity | Bucket Signal" << std::endl;
    std::cout << "-----|-----------|-----------|-----------|--------------" << std::endl;
    
    for (size_t i = 1; i < test_data.size(); ++i) {
        auto output = filter.process(test_data[i]);
        
        std::string signal = (i < 5) ? "UP" : "DOWN";
        
        std::cout << std::setw(4) << i << " | "
                  << std::setw(9) << test_data[i].price << " | "
                  << std::setw(8) << output.price_smoothed << " | "
                  << std::setw(8) << output.price_velocity << " | "
                  << std::setw(12) << signal << std::endl;
    }
    
    std::cout << std::endl;
}

void demonstrate_configuration_comparison() {
    std::cout << "=== Configuration Comparison ===" << std::endl;
    
    // Generate test data once
    auto test_data = generate_test_data(20);
    
    // Configuration 1: Conservative (long memory)
    HKFConfig config1;
    config1.lambda_fixed = 0.995;        // Long memory (200-tick equivalent)
    config1.lambda_adaptive = false;
    config1.bucket_weight = 0.2;         // Low bucket influence
    
    // Configuration 2: Aggressive (short memory)
    HKFConfig config2;
    config2.lambda_fixed = 0.95;         // Short memory (20-tick equivalent)
    config2.lambda_adaptive = false;
    config2.bucket_weight = 0.7;         // High bucket influence
    
    HybridExpForgettingKalmanFilter filter1(config1);
    HybridExpForgettingKalmanFilter filter2(config2);
    
    filter1.initialize(test_data[0], 1.0);
    filter2.initialize(test_data[0], 1.0);
    
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Step | Raw Price | Conservative | Aggressive" << std::endl;
    std::cout << "-----|-----------|--------------|-----------" << std::endl;
    
    for (size_t i = 1; i < std::min(test_data.size(), size_t(10)); ++i) {
        auto output1 = filter1.process(test_data[i]);
        auto output2 = filter2.process(test_data[i]);
        
        std::cout << std::setw(4) << i << " | "
                  << std::setw(9) << test_data[i].price << " | "
                  << std::setw(12) << output1.price_smoothed << " | "
                  << std::setw(10) << output2.price_smoothed << std::endl;
    }
    
    std::cout << std::endl;
}

int main() {
    std::cout << "Hybrid Exponential Forgetting Kalman Filter Examples" << std::endl;
    std::cout << "=====================================================" << std::endl << std::endl;
    
    try {
        demonstrate_basic_usage();
        demonstrate_adaptive_lambda();
        demonstrate_bucket_control();
        demonstrate_configuration_comparison();
        
        std::cout << "All examples completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 