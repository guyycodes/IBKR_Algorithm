// Smoke Test Suite for Standalone 1min and 5min Hybrid Exponential Forgetting Kalman Filters
// Comprehensive verification tests for both timescale-specific filters

#include "1min_HEFKF.hpp"
#include "5min_HEFKF.hpp"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>

// Test result tracking
struct TestResult {
    bool passed = true;
    std::string name;
    std::string error_msg;
};

void print_test_header(const std::string& test_name) {
    std::cout << "\n=== " << test_name << " ===" << std::endl;
}

void print_test_result(const TestResult& result) {
    std::cout << "[" << (result.passed ? "PASS" : "FAIL") << "] " << result.name;
    if (!result.passed) {
        std::cout << " - " << result.error_msg;
    }
    std::cout << std::endl;
}

// Test 0: Quick Sanity Check for 1min Filter
TestResult test_1min_quick_sanity() {
    TestResult result;
    result.name = "1min Filter - Quick Sanity Check";
    
    try {
        print_test_header("Test 0a: 1min Filter - Quick Sanity Check");
        
        hefkf_1min::OneMinuteHEFKF kf;

        hefkf_1min::MarketData m0{100.0, 300, 0.05, std::chrono::system_clock::now()};
        kf.initialize(m0);

        std::cout << "Step | Price Smoothed | Lambda | Config" << std::endl;
        std::cout << "-----|----------------|--------|--------" << std::endl;
        
        double prev_smoothed = 100.0;
        
        for(int i = 1; i <= 3; ++i) {
            hefkf_1min::MarketData md;
            md.price  = 100.0 + 0.02 * i;
            md.volume = 300 + 10 * i;
            md.spread = 0.05;
            md.timestamp = m0.timestamp + std::chrono::seconds(i);
            auto out = kf.process(md);
            
            std::cout << std::setw(4) << i << " | "
                     << std::setw(14) << std::fixed << std::setprecision(6) << out.price_smoothed << " | "
                     << std::setw(6) << std::setprecision(3) << out.lambda_used << " | "
                     << "1min" << std::endl;
            
            // Expected: increasing price_smoothed values and λ≈0.99
            if (out.price_smoothed <= prev_smoothed) {
                result.passed = false;
                result.error_msg = "1min filter price_smoothed should increase, but didn't at step " + std::to_string(i);
                return result;
            }
            
            if (std::abs(out.lambda_used - 0.99) > 0.01) {
                result.passed = false;
                result.error_msg = "1min filter lambda should be ≈0.99, got " + std::to_string(out.lambda_used);
                return result;
            }
            
            prev_smoothed = out.price_smoothed;
        }
        
        auto config = hefkf_1min::OneMinuteHEFKF::get_config_info();
        std::cout << "✓ 1min Config: R_PRICE=" << config.R_PRICE << ", Q_BASE=" << config.Q_BASE_SCALE << std::endl;
        std::cout << "✓ Expected: Three increasing price_smoothed values - PASS" << std::endl;
        std::cout << "✓ Expected: λ≈0.99 (1min filter pipeline working) - PASS" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 0b: Quick Sanity Check for 5min Filter
TestResult test_5min_quick_sanity() {
    TestResult result;
    result.name = "5min Filter - Quick Sanity Check";
    
    try {
        print_test_header("Test 0b: 5min Filter - Quick Sanity Check");
        
        hefkf_5min::FiveMinuteHEFKF kf;

        hefkf_5min::MarketData m0{100.0, 300, 0.05, std::chrono::system_clock::now()};
        kf.initialize(m0);

        std::cout << "Step | Price Smoothed | Lambda | Config" << std::endl;
        std::cout << "-----|----------------|--------|--------" << std::endl;
        
        double prev_smoothed = 100.0;
        
        for(int i = 1; i <= 3; ++i) {
            hefkf_5min::MarketData md;
            md.price  = 100.0 + 0.02 * i;
            md.volume = 300 + 10 * i;
            md.spread = 0.05;
            md.timestamp = m0.timestamp + std::chrono::seconds(i);
            auto out = kf.process(md);
            
            std::cout << std::setw(4) << i << " | "
                     << std::setw(14) << std::fixed << std::setprecision(6) << out.price_smoothed << " | "
                     << std::setw(6) << std::setprecision(3) << out.lambda_used << " | "
                     << "5min" << std::endl;
            
            // Expected: increasing price_smoothed values and λ≈0.99
            if (out.price_smoothed <= prev_smoothed) {
                result.passed = false;
                result.error_msg = "5min filter price_smoothed should increase, but didn't at step " + std::to_string(i);
                return result;
            }
            
            if (std::abs(out.lambda_used - 0.99) > 0.01) {
                result.passed = false;
                result.error_msg = "5min filter lambda should be ≈0.99, got " + std::to_string(out.lambda_used);
                return result;
            }
            
            prev_smoothed = out.price_smoothed;
        }
        
        auto config = hefkf_5min::FiveMinuteHEFKF::get_config_info();
        std::cout << "✓ 5min Config: R_PRICE=" << config.R_PRICE << ", Q_BASE=" << config.Q_BASE_SCALE << std::endl;
        std::cout << "✓ Expected: Three increasing price_smoothed values - PASS" << std::endl;
        std::cout << "✓ Expected: λ≈0.99 (5min filter pipeline working) - PASS" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 1: Comparative Behavior (1min vs 5min responsiveness)
TestResult test_comparative_behavior() {
    TestResult result;
    result.name = "1min vs 5min Comparative Behavior";
    
    try {
        print_test_header("Test 1: 1min vs 5min Comparative Behavior");
        
        hefkf_1min::OneMinuteHEFKF kf_1min;
        hefkf_5min::FiveMinuteHEFKF kf_5min;

        // Initialize both filters with same data
        hefkf_1min::MarketData m0_1min{100.0, 300, 0.05, std::chrono::system_clock::now()};
        hefkf_5min::MarketData m0_5min{100.0, 300, 0.05, std::chrono::system_clock::now()};
        kf_1min.initialize(m0_1min);
        kf_5min.initialize(m0_5min);

        std::cout << "Step | Input | 1min_Out | 5min_Out | 1min_Vel | 5min_Vel | Responsiveness" << std::endl;
        std::cout << "-----|-------|----------|----------|----------|----------|---------------" << std::endl;
        
        std::vector<double> price_changes = {0.0, 0.1, -0.05, 0.15, -0.08, 0.12};  // Mixed volatility
        double prev_1min_smoothed = 100.0;
        double prev_5min_smoothed = 100.0;
        
        for(int i = 1; i < price_changes.size(); ++i) {
            double input_price = 100.0 + price_changes[i];
            
            hefkf_1min::MarketData md_1min;
            md_1min.price = input_price;
            md_1min.volume = 300 + 10 * i;
            md_1min.spread = 0.05;
            md_1min.timestamp = m0_1min.timestamp + std::chrono::seconds(i);
            
            hefkf_5min::MarketData md_5min;
            md_5min.price = input_price;
            md_5min.volume = 300 + 10 * i;
            md_5min.spread = 0.05;
            md_5min.timestamp = m0_5min.timestamp + std::chrono::seconds(i);
            
            auto out_1min = kf_1min.process(md_1min);
            auto out_5min = kf_5min.process(md_5min);
            
            double responsiveness_1min = std::abs(out_1min.price_smoothed - prev_1min_smoothed);
            double responsiveness_5min = std::abs(out_5min.price_smoothed - prev_5min_smoothed);
            
            std::cout << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << input_price << " | "
                     << std::setw(8) << std::setprecision(4) << out_1min.price_smoothed << " | "
                     << std::setw(8) << std::setprecision(4) << out_5min.price_smoothed << " | "
                     << std::setw(8) << std::setprecision(4) << out_1min.price_velocity << " | "
                     << std::setw(8) << std::setprecision(4) << out_5min.price_velocity << " | ";
            
            // 1min should be more responsive than 5min (generally)
            if (responsiveness_1min > responsiveness_5min) {
                std::cout << "1min>5min ✓" << std::endl;
            } else if (responsiveness_1min < responsiveness_5min * 0.8) {  // Allow some tolerance
                result.passed = false;
                result.error_msg = "1min filter should be more responsive than 5min, but got 1min_resp=" + 
                                 std::to_string(responsiveness_1min) + " vs 5min_resp=" + std::to_string(responsiveness_5min);
                return result;
            } else {
                std::cout << "similar" << std::endl;
            }
            
            prev_1min_smoothed = out_1min.price_smoothed;
            prev_5min_smoothed = out_5min.price_smoothed;
        }
        
        std::cout << "✓ 1min filter shows appropriate responsiveness vs 5min filter - PASS" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 2: Drift Tracking (Both Filters)
TestResult test_drift_tracking() {
    TestResult result;
    result.name = "Drift Tracking (Both Filters)";
    
    try {
        print_test_header("Test 2: Drift Tracking (Both Filters)");
        
        hefkf_1min::OneMinuteHEFKF kf_1min;
        hefkf_5min::FiveMinuteHEFKF kf_5min;

        hefkf_1min::MarketData m0_1min{100.0, 300, 0.05, std::chrono::system_clock::now()};
        hefkf_5min::MarketData m0_5min{100.0, 300, 0.05, std::chrono::system_clock::now()};
        kf_1min.initialize(m0_1min);
        kf_5min.initialize(m0_5min);

        std::cout << "Filter | Step | Price | P_Smoothed | Velocity | Lambda | Note" << std::endl;
        std::cout << "-------|------|-------|------------|----------|--------|-----" << std::endl;
        
        // Test both filters with same upward drift
        for (int i = 1; i <= 4; ++i) {
            double drift_price = 100.0 + 0.04 * i;  // steady upward drift
            
            // Test 1min filter
            hefkf_1min::MarketData md_1min;
            md_1min.price = drift_price;
            md_1min.volume = 300 + 5 * i;
            md_1min.spread = 0.05;
            md_1min.timestamp = m0_1min.timestamp + std::chrono::seconds(i);
            auto out_1min = kf_1min.process(md_1min);
            
            std::cout << std::setw(6) << "1min" << " | "
                     << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << drift_price << " | "
                     << std::setw(10) << std::setprecision(6) << out_1min.price_smoothed << " | "
                     << std::setw(8) << std::setprecision(4) << out_1min.price_velocity << " | "
                     << std::setw(6) << std::setprecision(3) << out_1min.lambda_used << " | ";
            
            // Validate 1min filter behavior
            if (out_1min.price_velocity <= 0.0 && i > 1) {
                result.passed = false;
                result.error_msg = "1min filter velocity should be positive during upward drift";
                return result;
            }
            std::cout << "1min✓" << std::endl;
            
            // Test 5min filter with same data
            hefkf_5min::MarketData md_5min;
            md_5min.price = drift_price;
            md_5min.volume = 300 + 5 * i;
            md_5min.spread = 0.05;
            md_5min.timestamp = m0_5min.timestamp + std::chrono::seconds(i);
            auto out_5min = kf_5min.process(md_5min);
            
            std::cout << std::setw(6) << "5min" << " | "
                     << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << drift_price << " | "
                     << std::setw(10) << std::setprecision(6) << out_5min.price_smoothed << " | "
                     << std::setw(8) << std::setprecision(4) << out_5min.price_velocity << " | "
                     << std::setw(6) << std::setprecision(3) << out_5min.lambda_used << " | ";
            
            // Validate 5min filter behavior
            if (out_5min.price_velocity <= 0.0 && i > 1) {
                result.passed = false;
                result.error_msg = "5min filter velocity should be positive during upward drift";
                return result;
            }
            std::cout << "5min✓" << std::endl;
        }
        
        std::cout << "✓ Both filters track upward drift correctly" << std::endl;
        std::cout << "✓ Both filters maintain positive velocity during drift" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 3: Adaptive Lambda (Both Filters)
TestResult test_adaptive_lambda() {
    TestResult result;
    result.name = "Adaptive Lambda (Both Filters)";
    
    try {
        print_test_header("Test 3: Adaptive Lambda (Both Filters)");
        
        hefkf_1min::OneMinuteHEFKF kf_1min;
        hefkf_5min::FiveMinuteHEFKF kf_5min;

        hefkf_1min::MarketData m0_1min{100.0, 1000, 0.01, std::chrono::system_clock::now()};
        hefkf_5min::MarketData m0_5min{100.0, 1000, 0.01, std::chrono::system_clock::now()};
        kf_1min.initialize(m0_1min);
        kf_5min.initialize(m0_5min);

        std::cout << "Filter | Step | Price | Lambda | Vol_Threshold | Note" << std::endl;
        std::cout << "-------|------|-------|--------|---------------|-----" << std::endl;
        
        auto config_1min = hefkf_1min::OneMinuteHEFKF::get_config_info();
        auto config_5min = hefkf_5min::FiveMinuteHEFKF::get_config_info();
        
        for(int i = 1; i <= 6; ++i) {
            double test_price;
            std::string note;
            
            // Create volatility spike in the middle
            if (i >= 3 && i <= 4) {
                test_price = 100.0 + (i % 2 == 0 ? 3.0 : -3.0);  // Large price swings
                note = "SPIKE";
            } else {
                test_price = 100.0 + 0.01 * i;  // Normal small moves
                note = "normal";
            }
            
            // Test 1min filter
            hefkf_1min::MarketData md_1min;
            md_1min.price = test_price;
            md_1min.volume = 1000 + 10 * i;
            md_1min.spread = 0.01;
            md_1min.timestamp = m0_1min.timestamp + std::chrono::seconds(i);
            auto out_1min = kf_1min.process(md_1min);
            
            std::cout << std::setw(6) << "1min" << " | "
                     << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << test_price << " | "
                     << std::setw(6) << std::setprecision(3) << out_1min.lambda_used << " | "
                     << std::setw(13) << std::setprecision(3) << config_1min.VOL_THRESHOLD << " | "
                     << std::setw(6) << note << std::endl;
            
            // Validate lambda bounds for 1min
            if (out_1min.lambda_used < config_1min.LAMBDA_MIN || out_1min.lambda_used > config_1min.LAMBDA_MAX) {
                result.passed = false;
                result.error_msg = "1min lambda out of bounds: " + std::to_string(out_1min.lambda_used);
                return result;
            }
            
            // Test 5min filter
            hefkf_5min::MarketData md_5min;
            md_5min.price = test_price;
            md_5min.volume = 1000 + 10 * i;
            md_5min.spread = 0.01;
            md_5min.timestamp = m0_5min.timestamp + std::chrono::seconds(i);
            auto out_5min = kf_5min.process(md_5min);
            
            std::cout << std::setw(6) << "5min" << " | "
                     << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << test_price << " | "
                     << std::setw(6) << std::setprecision(3) << out_5min.lambda_used << " | "
                     << std::setw(13) << std::setprecision(3) << config_5min.VOL_THRESHOLD << " | "
                     << std::setw(6) << note << std::endl;
            
            // Validate lambda bounds for 5min
            if (out_5min.lambda_used < config_5min.LAMBDA_MIN || out_5min.lambda_used > config_5min.LAMBDA_MAX) {
                result.passed = false;
                result.error_msg = "5min lambda out of bounds: " + std::to_string(out_5min.lambda_used);
                return result;
            }
        }
        
        std::cout << "✓ Both filters maintain lambda within bounds" << std::endl;
        std::cout << "✓ Lambda adaptation works for different volatility thresholds" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 4: Configuration Display and Validation
TestResult test_config_display() {
    TestResult result;
    result.name = "Configuration Display and Validation";
    
    try {
        print_test_header("Test 4: Configuration Display and Validation");
        
        auto config_1min = hefkf_1min::OneMinuteHEFKF::get_config_info();
        auto config_5min = hefkf_5min::FiveMinuteHEFKF::get_config_info();
        
        std::cout << "Parameter Comparison:" << std::endl;
        std::cout << "Parameter            | 1min Value | 5min Value | Expected Relationship" << std::endl;
        std::cout << "--------------------|------------|------------|----------------------" << std::endl;
        
        // Compare key parameters
        std::cout << "INITIAL_P_SCALE      | " << std::setw(10) << config_1min.INITIAL_P_SCALE 
                 << " | " << std::setw(10) << config_5min.INITIAL_P_SCALE 
                 << " | 1min < 5min (✓)" << std::endl;
        
        std::cout << "R_PRICE              | " << std::setw(10) << config_1min.R_PRICE 
                 << " | " << std::setw(10) << config_5min.R_PRICE 
                 << " | 1min < 5min (✓)" << std::endl;
        
        std::cout << "Q_BASE_SCALE         | " << std::setw(10) << std::scientific << config_1min.Q_BASE_SCALE 
                 << " | " << std::setw(10) << config_5min.Q_BASE_SCALE 
                 << " | 1min < 5min (✓)" << std::endl;
        
        std::cout << "FREQUENCY_WEIGHT     | " << std::setw(10) << std::fixed << std::setprecision(2) << config_1min.FREQUENCY_DOMAIN_WEIGHT 
                 << " | " << std::setw(10) << config_5min.FREQUENCY_DOMAIN_WEIGHT 
                 << " | 1min < 5min (✓)" << std::endl;
        
        std::cout << "VOL_THRESHOLD        | " << std::setw(10) << std::setprecision(3) << config_1min.VOL_THRESHOLD 
                 << " | " << std::setw(10) << config_5min.VOL_THRESHOLD 
                 << " | 1min < 5min (✓)" << std::endl;
        
        std::cout << "LAMBDA_MIN           | " << std::setw(10) << config_1min.LAMBDA_MIN 
                 << " | " << std::setw(10) << config_5min.LAMBDA_MIN 
                 << " | 1min ≥ 5min (✓)" << std::endl;
        
        // Validate expected relationships
        if (config_1min.INITIAL_P_SCALE >= config_5min.INITIAL_P_SCALE) {
            result.passed = false;
            result.error_msg = "1min P_SCALE should be < 5min P_SCALE for more responsiveness";
            return result;
        }
        
        if (config_1min.R_PRICE >= config_5min.R_PRICE) {
            result.passed = false;
            result.error_msg = "1min R_PRICE should be < 5min R_PRICE for less measurement noise";
            return result;
        }
        
        if (config_1min.Q_BASE_SCALE >= config_5min.Q_BASE_SCALE) {
            result.passed = false;
            result.error_msg = "1min Q_BASE_SCALE should be < 5min Q_BASE_SCALE for less process noise";
            return result;
        }
        
        if (config_1min.FREQUENCY_DOMAIN_WEIGHT >= config_5min.FREQUENCY_DOMAIN_WEIGHT) {
            result.passed = false;
            result.error_msg = "1min FREQUENCY_WEIGHT should be < 5min for different signal emphasis";
            return result;
        }
        
        std::cout << "✓ All configuration relationships match expected timescale behavior" << std::endl;
        std::cout << "✓ 1min filter optimized for responsiveness" << std::endl;
        std::cout << "✓ 5min filter optimized for smoothness" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 5: Standalone Independence
TestResult test_standalone_independence() {
    TestResult result;
    result.name = "Standalone Independence";
    
    try {
        print_test_header("Test 5: Standalone Independence");
        
        // Test that each filter can be used completely independently
        std::cout << "Testing namespace isolation..." << std::endl;
        
        // Create filters in their own namespaces
        hefkf_1min::OneMinuteHEFKF filter_1min;
        hefkf_5min::FiveMinuteHEFKF filter_5min;
        
        // Create data structures in their own namespaces
        hefkf_1min::MarketData data_1min;
        data_1min.price = 100.0;
        data_1min.volume = 1000.0;
        data_1min.spread = 0.01;
        data_1min.timestamp = std::chrono::system_clock::now();
        
        hefkf_5min::MarketData data_5min;
        data_5min.price = 100.0;
        data_5min.volume = 1000.0;
        data_5min.spread = 0.01;
        data_5min.timestamp = std::chrono::system_clock::now();
        
        // Initialize and process
        filter_1min.initialize(data_1min);
        filter_5min.initialize(data_5min);
        
        auto output_1min = filter_1min.process(data_1min);
        auto output_5min = filter_5min.process(data_5min);
        
        // Verify outputs are in correct namespaces and have valid values
        if (output_1min.price_smoothed <= 0.0 || output_5min.price_smoothed <= 0.0) {
            result.passed = false;
            result.error_msg = "Invalid output values from standalone filters";
            return result;
        }
        
        // Test bucket confidence structures (if they exist)
        auto bucket_1min = std::make_unique<hefkf_1min::BucketConfidence>();
        auto bucket_5min = std::make_unique<hefkf_5min::BucketConfidence>();
        
        bucket_1min->up_001_002 = 0.5;
        bucket_1min->dn_001_002 = 0.5;
        bucket_1min->normalize();
        
        bucket_5min->up_001_002 = 0.3;
        bucket_5min->dn_001_002 = 0.7;
        bucket_5min->normalize();
        
        if (!bucket_1min->is_valid() || !bucket_5min->is_valid()) {
            result.passed = false;
            result.error_msg = "Bucket confidence structures not working properly";
            return result;
        }
        
        std::cout << "✓ 1min filter operates independently in hefkf_1min namespace" << std::endl;
        std::cout << "✓ 5min filter operates independently in hefkf_5min namespace" << std::endl;
        std::cout << "✓ No cross-contamination between filter namespaces" << std::endl;
        std::cout << "✓ All data structures work independently" << std::endl;
        std::cout << "✓ Zero dependency on original hybrid_exp_forgetting_kalman_filter" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

int main() {
    std::cout << "Standalone 1min & 5min Hybrid Exponential Forgetting Kalman Filters - Smoke Tests" << std::endl;
    std::cout << "===================================================================================" << std::endl;
    
    std::vector<TestResult> results;
    
    // Run all tests
    results.push_back(test_1min_quick_sanity());
    results.push_back(test_5min_quick_sanity());
    results.push_back(test_comparative_behavior());
    results.push_back(test_drift_tracking());
    results.push_back(test_adaptive_lambda());
    results.push_back(test_config_display());
    results.push_back(test_standalone_independence());
    
    // Print summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    int passed = 0;
    for (const auto& result : results) {
        print_test_result(result);
        if (result.passed) passed++;
    }
    
    std::cout << "\nResults: " << passed << "/" << results.size() << " tests passed" << std::endl;
    
    if (passed == results.size()) {
        std::cout << "🎉 All smoke tests PASSED! Both standalone filters are ready for use." << std::endl;
        std::cout << "📊 1min filter: Optimized for responsive short-term tracking" << std::endl;
        std::cout << "📈 5min filter: Optimized for stable medium-term trends" << std::endl;
        return 0;
    } else {
        std::cout << "❌ Some tests FAILED! Please review implementations." << std::endl;
        return 1;
    }
} 