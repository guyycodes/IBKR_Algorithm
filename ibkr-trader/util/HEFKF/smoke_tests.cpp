// Smoke Test Suite for Hybrid Exponential Forgetting Kalman Filter
// Quick verification tests to ensure basic functionality works correctly

#include "hybrid_exp_forgetting_kalman_filter.hpp"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>

using namespace hefkf;

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

// Test 0: Quick Compile/Runtime Sanity Check
//If this prints three increasing price_smoothed values and λ≈0.99 (unless you spike volatility), the core pipeline is wired correctly.
TestResult test_quick_sanity() {
    TestResult result;
    result.name = "Quick Compile/Runtime Sanity";
    
    try {
        print_test_header("Test 0: Quick Compile/Runtime Sanity");
        
        HKFConfig cfg;
        hefkf::HybridExpForgettingKalmanFilter kf(cfg);

        hefkf::MarketData m0{100.0, 300, 0.05, std::chrono::system_clock::now()};
        kf.initialize(m0);

        std::cout << "Step | Price Smoothed | Lambda" << std::endl;
        std::cout << "-----|----------------|--------" << std::endl;
        
        double prev_smoothed = 100.0;
        
        for(int i = 1; i <= 3; ++i) {
            hefkf::MarketData md;
            md.price  = 100.0 + 0.02 * i;
            md.volume = 300 + 10 * i;
            md.spread = 0.05;
            md.timestamp = m0.timestamp + std::chrono::seconds(i);
            auto out = kf.process(md);
            
            std::cout << std::setw(4) << i << " | "
                     << std::setw(14) << std::fixed << std::setprecision(6) << out.price_smoothed << " | "
                     << std::setw(6) << std::setprecision(3) << out.lambda_used << std::endl;
            
            // Expected: three increasing price_smoothed values and λ≈0.99
            if (out.price_smoothed <= prev_smoothed) {
                result.passed = false;
                result.error_msg = "Price smoothed should increase, but didn't at step " + std::to_string(i);
                return result;
            }
            
            if (std::abs(out.lambda_used - 0.99) > 0.01) {
                result.passed = false;
                result.error_msg = "Lambda should be ≈0.99, got " + std::to_string(out.lambda_used);
                return result;
            }
            
            prev_smoothed = out.price_smoothed;
        }
        
        std::cout << "✓ Expected: Three increasing price_smoothed values - PASS" << std::endl;
        std::cout << "✓ Expected: λ≈0.99 (core pipeline wired correctly) - PASS" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 1: Drift Tracking with Velocity
// Expected: price_smoothed tracks upward drift, price_velocity stays positive ~(price-prev)/dt, λ≈0.99
TestResult test_drift_tracking() {
    TestResult result;
    result.name = "Drift Tracking with Velocity";
    
    try {
        print_test_header("Test 1: Drift Tracking with Velocity");
        
        hefkf::HKFConfig cfg;                     // default λ=0.99, bucket_weight=0.5
        hefkf::HybridExpForgettingKalmanFilter kf(cfg);

        hefkf::MarketData m0{100.0, 300, 0.05, std::chrono::system_clock::now()};
        kf.initialize(m0);                        // seed filter

        std::cout << "Step | Price | P_Smoothed | Velocity | Lambda | Note" << std::endl;
        std::cout << "-----|-------|------------|----------|--------|-----" << std::endl;
        
        double prev_smoothed = 100.0;
        double prev_price = 100.0;
        
        // three synthetic ticks
        for (int i = 1; i <= 3; ++i) {
            hefkf::MarketData md;
            md.price  = 100.0 + 0.04 * i;          // small drift
            md.volume = 300 + 5 * i;
            md.spread = 0.05;
            md.timestamp = m0.timestamp + std::chrono::seconds(i);
            hefkf::FilterOutput out = kf.process(md);
            
            double expected_vel = md.price - prev_price;  // rough (price-prev)/dt with dt=1
            
            std::cout << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << md.price << " | "
                     << std::setw(10) << std::setprecision(6) << out.price_smoothed << " | "
                     << std::setw(8) << std::setprecision(4) << out.price_velocity << " | "
                     << std::setw(6) << std::setprecision(3) << out.lambda_used << " | ";
            
            // Expected behavior validation
            
            // 1. price_smoothed tracks the upward drift
            if (out.price_smoothed <= prev_smoothed) {
                result.passed = false;
                result.error_msg = "price_smoothed should track upward drift, but didn't increase at step " + std::to_string(i);
                return result;
            }
            std::cout << "drift✓";
            
            // 2. price_velocity stays positive and roughly (md.price-prev)/dt
            if (out.price_velocity <= 0.0) {
                result.passed = false;
                result.error_msg = "price_velocity should be positive during upward drift, got " + std::to_string(out.price_velocity);
                return result;
            }
            
            // Velocity should be roughly the price change (allowing for smoothing)
            if (std::abs(out.price_velocity - expected_vel) > expected_vel * 2.0) {  // Allow 2x tolerance for smoothing
                result.passed = false;
                result.error_msg = "price_velocity (" + std::to_string(out.price_velocity) + 
                                 ") too far from expected (" + std::to_string(expected_vel) + ")";
                return result;
            }
            std::cout << " vel✓";
            
            // 3. λ ≈ 0.99 unless you inject a large volatility spike
            if (std::abs(out.lambda_used - 0.99) > 0.01) {
                result.passed = false;
                result.error_msg = "λ should be ≈0.99, got " + std::to_string(out.lambda_used);
                return result;
            }
            std::cout << " λ✓" << std::endl;
            
            prev_smoothed = out.price_smoothed;
            prev_price = md.price;
        }
        
        std::cout << "\n✓ Expected: price_smoothed tracks upward drift - PASS" << std::endl;
        std::cout << "✓ Expected: price_velocity stays positive ~(price-prev)/dt - PASS" << std::endl;
        std::cout << "✓ Expected: λ≈0.99 (no volatility spike) - PASS" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 2: Basic Functionality (User Provided)
TestResult test_basic_functionality() {
    TestResult result;
    result.name = "Basic Functionality";
    
    try {
        print_test_header("Test 1: Basic Functionality");
        
        HKFConfig cfg;
        HybridExpForgettingKalmanFilter kf(cfg);
        MarketData tick0{100.0, 250, 0.1, std::chrono::system_clock::now()};
        kf.initialize(tick0);

        std::cout << "Step | Price | Smoothed | Lambda" << std::endl;
        std::cout << "-----|-------|----------|--------" << std::endl;
        
        double prev_smoothed = 100.0;
        
        for(int i = 1; i <= 5; ++i) {
            MarketData md;
            md.price  = 100.0 + 0.01 * i;
            md.volume = 250 + 5 * i;
            md.spread = 0.1;
            md.timestamp = tick0.timestamp + std::chrono::seconds(i);

            auto out = kf.process(md);
            
            std::cout << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << md.price << " | "
                     << std::setw(8) << std::setprecision(4) << out.price_smoothed << " | "
                     << std::setw(6) << std::setprecision(3) << out.lambda_used << std::endl;
            
            // Verify expectations
            if (out.price_smoothed <= prev_smoothed) {
                result.passed = false;
                result.error_msg = "Price should rise gently, but smoothed price didn't increase at step " + std::to_string(i);
                return result;
            }
            
            if (std::abs(out.lambda_used - 0.99) > 0.01) {
                result.passed = false;
                result.error_msg = "Lambda should be near 0.99, got " + std::to_string(out.lambda_used) + " at step " + std::to_string(i);
                return result;
            }
            
            prev_smoothed = out.price_smoothed;
        }
        
        std::cout << "✓ Price smoothed rises gently as expected" << std::endl;
        std::cout << "✓ Lambda stays near 0.99 as expected" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 3: Adaptive Lambda with Volatility Spike
TestResult test_adaptive_lambda() {
    TestResult result;
    result.name = "Adaptive Lambda";
    
    try {
        print_test_header("Test 3: Adaptive Lambda with Volatility Spike");
        
        HKFConfig cfg;
        cfg.lambda_adaptive = true;  // Enable adaptive lambda
        cfg.lambda_min = 0.95;
        cfg.lambda_max = 0.995;
        
        HybridExpForgettingKalmanFilter kf(cfg);
        MarketData tick0{100.0, 1000, 0.01, std::chrono::system_clock::now()};
        kf.initialize(tick0);

        std::cout << "Step | Price | Lambda | Volatility | Note" << std::endl;
        std::cout << "-----|-------|--------|------------|-----" << std::endl;
        
        double prev_lambda = 0.99;
        
        for(int i = 1; i <= 7; ++i) {
            MarketData md;
            md.volume = 1000 + 10 * i;
            md.spread = 0.01;
            md.timestamp = tick0.timestamp + std::chrono::seconds(i);
            
            // Create volatility spike in the middle
            if (i >= 3 && i <= 5) {
                md.price = 100.0 + (i % 2 == 0 ? 2.0 : -2.0);  // Large price swings
            } else {
                md.price = 100.0 + 0.01 * i;  // Normal small moves
            }

            auto out = kf.process(md);
            
            double volatility = std::abs(md.price - (i > 1 ? (100.0 + 0.01 * (i-1)) : 100.0)) / 100.0;
            if (i >= 3 && i <= 5) {
                volatility = std::abs(md.price - 100.0) / 100.0;  // Approximate volatility for spikes
            }
            
            std::string note = (i >= 3 && i <= 5) ? "SPIKE" : "normal";
            
            std::cout << std::setw(4) << i << " | "
                     << std::setw(5) << std::fixed << std::setprecision(2) << md.price << " | "
                     << std::setw(6) << std::setprecision(3) << out.lambda_used << " | "
                     << std::setw(10) << std::setprecision(4) << volatility << " | "
                     << std::setw(6) << note << std::endl;
            
            // During volatility spike, lambda should decrease (faster adaptation)
            if (i >= 3 && i <= 5) {
                if (out.lambda_used >= prev_lambda) {
                    result.passed = false;
                    result.error_msg = "Lambda should decrease during volatility spike, but got " + 
                                     std::to_string(out.lambda_used) + " >= " + std::to_string(prev_lambda);
                    return result;
                }
            }
            
            // Verify lambda is within bounds
            if (out.lambda_used < cfg.lambda_min || out.lambda_used > cfg.lambda_max) {
                result.passed = false;
                result.error_msg = "Lambda out of bounds: " + std::to_string(out.lambda_used) + 
                                 " not in [" + std::to_string(cfg.lambda_min) + ", " + 
                                 std::to_string(cfg.lambda_max) + "]";
                return result;
            }
            
            if (i < 3) prev_lambda = out.lambda_used;  // Track before spike
        }
        
        std::cout << "✓ Lambda adapts to volatility spikes as expected" << std::endl;
        std::cout << "✓ Lambda stays within configured bounds" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Exception: ") + e.what();
    }
    
    return result;
}

// Test 4: Configuration Validation
TestResult test_config_validation() {
    TestResult result;
    result.name = "Configuration Validation";
    
    try {
        print_test_header("Test 4: Configuration Validation");
        
        // Test invalid lambda_fixed
        bool caught_exception = false;
        try {
            HKFConfig bad_cfg;
            bad_cfg.lambda_fixed = 1.5;  // Invalid: > 1.0
            HybridExpForgettingKalmanFilter kf(bad_cfg);
        } catch (const std::invalid_argument&) {
            caught_exception = true;
            std::cout << "✓ Caught invalid lambda_fixed > 1.0" << std::endl;
        }
        
        if (!caught_exception) {
            result.passed = false;
            result.error_msg = "Should have caught invalid lambda_fixed > 1.0";
            return result;
        }
        
        // Test invalid lambda bounds
        caught_exception = false;
        try {
            HKFConfig bad_cfg;
            bad_cfg.lambda_min = 0.97;
            bad_cfg.lambda_max = 0.95;  // Invalid: max < min
            HybridExpForgettingKalmanFilter kf(bad_cfg);
        } catch (const std::invalid_argument&) {
            caught_exception = true;
            std::cout << "✓ Caught invalid lambda_max < lambda_min" << std::endl;
        }
        
        if (!caught_exception) {
            result.passed = false;
            result.error_msg = "Should have caught lambda_max < lambda_min";
            return result;
        }
        
        // Test valid configuration works
        HKFConfig good_cfg;
        good_cfg.lambda_fixed = 0.99;
        good_cfg.lambda_min = 0.95;
        good_cfg.lambda_max = 0.995;
        
        HybridExpForgettingKalmanFilter kf(good_cfg);  // Should not throw
        std::cout << "✓ Valid configuration accepted" << std::endl;
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_msg = std::string("Unexpected exception: ") + e.what();
    }
    
    return result;
}

int main() {
    std::cout << "Hybrid Exponential Forgetting Kalman Filter - Smoke Tests" << std::endl;
    std::cout << "=========================================================" << std::endl;
    
    std::vector<TestResult> results;
    
    // Run all tests
    results.push_back(test_quick_sanity());
    results.push_back(test_drift_tracking());
    results.push_back(test_basic_functionality());
    results.push_back(test_adaptive_lambda());
    results.push_back(test_config_validation());
    
    // Print summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    int passed = 0;
    for (const auto& result : results) {
        print_test_result(result);
        if (result.passed) passed++;
    }
    
    std::cout << "\nResults: " << passed << "/" << results.size() << " tests passed" << std::endl;
    
    if (passed == results.size()) {
        std::cout << "🎉 All smoke tests PASSED! Filter is ready for use." << std::endl;
        return 0;
    } else {
        std::cout << "❌ Some tests FAILED! Please review implementation." << std::endl;
        return 1;
    }
} 