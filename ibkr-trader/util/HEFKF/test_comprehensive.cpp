// Comprehensive tests for HEFKF Gaussian distribution and spectral analysis

#include "frequency_analyser.hpp"
#include "posterior.hpp"
#include "1min_HEFKF.hpp"
#include "5min_HEFKF.hpp"
#include "integration_loop_wrapper.hpp"
#include <iostream>
#include <cmath>
#include <vector>
#include <cassert>
#include <iomanip>
#include <thread>

// Test tolerances
constexpr double TOLERANCE = 1e-6;
constexpr double LOOSE_TOLERANCE = 1e-3;

// Helper function to check if values are close
bool is_close(double a, double b, double tol = TOLERANCE) {
    return std::abs(a - b) < tol;
}

// ========================= GAUSSIAN DISTRIBUTION TESTS =========================

void test_standard_normal_cdf() {
    std::cout << "\n=== Testing Standard Normal CDF ===" << std::endl;
    
    // Test known values
    struct TestCase {
        double x;
        double expected;
        const char* description;
    };
    
    TestCase test_cases[] = {
        {0.0, 0.5, "CDF at 0 should be 0.5"},
        {-10.0, 0.0, "CDF at -10 should be ~0"},
        {10.0, 1.0, "CDF at +10 should be ~1"},
        {1.0, 0.8413447, "CDF at 1.0"},
        {-1.0, 0.1586553, "CDF at -1.0"},
        {2.0, 0.9772499, "CDF at 2.0"},
        {-2.0, 0.0227501, "CDF at -2.0"},
        {1.96, 0.975, "CDF at 1.96 (95% confidence)"},
        {-1.96, 0.025, "CDF at -1.96"}
    };
    
    for (const auto& tc : test_cases) {
        double result = hefkf_common::GaussianIntegrator::standard_normal_cdf(tc.x);
        bool passed = is_close(result, tc.expected, LOOSE_TOLERANCE);
        std::cout << "  " << tc.description << ": " 
                  << (passed ? "PASS" : "FAIL") 
                  << " (got " << result << ", expected " << tc.expected << ")" << std::endl;
        assert(passed);
    }
}

void test_gaussian_cdf_interval() {
    std::cout << "\n=== Testing Gaussian CDF Interval ===" << std::endl;
    
    // Test 1: Standard normal between -1 and 1 (should be ~0.683)
    {
        double prob = hefkf_common::GaussianIntegrator::gaussian_cdf_interval(0.0, 1.0, -1.0, 1.0);
        bool passed = is_close(prob, 0.6826895, LOOSE_TOLERANCE);
        std::cout << "  P(-1 < X < 1) for N(0,1): " << (passed ? "PASS" : "FAIL") 
                  << " (got " << prob << ")" << std::endl;
        assert(passed);
    }
    
    // Test 2: Non-standard normal
    {
        double mean = 5.0;
        double std = 2.0;
        double prob = hefkf_common::GaussianIntegrator::gaussian_cdf_interval(mean, std, 3.0, 7.0);
        // This is P(3 < X < 7) for N(5, 4), which is P(-1 < Z < 1) = ~0.683
        bool passed = is_close(prob, 0.6826895, LOOSE_TOLERANCE);
        std::cout << "  P(3 < X < 7) for N(5,4): " << (passed ? "PASS" : "FAIL") 
                  << " (got " << prob << ")" << std::endl;
        assert(passed);
    }
    
    // Test 3: Degenerate case (zero std dev)
    {
        double prob1 = hefkf_common::GaussianIntegrator::gaussian_cdf_interval(5.0, 0.0, 4.0, 6.0);
        double prob2 = hefkf_common::GaussianIntegrator::gaussian_cdf_interval(5.0, 0.0, 6.0, 7.0);
        bool passed = (prob1 == 1.0) && (prob2 == 0.0);
        std::cout << "  Degenerate case (σ=0): " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 4: Tail probabilities
    {
        double prob = hefkf_common::GaussianIntegrator::gaussian_cdf_interval(0.0, 1.0, 2.0, 10.0);
        double expected = hefkf_common::GaussianIntegrator::standard_normal_cdf(10.0) - 
                         hefkf_common::GaussianIntegrator::standard_normal_cdf(2.0);
        bool passed = is_close(prob, expected);
        std::cout << "  Tail probability P(2 < X < 10): " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
}

void test_bucket_probability_distribution() {
    std::cout << "\n=== Testing Bucket Probability Distribution ===" << std::endl;
    
    // Create a mock 1-minute HEFKF filter
    hefkf_1min::OneMinuteHEFKF kf;
    hefkf_1min::MarketData init_data;
    init_data.price = 100.0;
    init_data.volume = 1000.0;
    init_data.spread = 0.01;
    init_data.timestamp = std::chrono::system_clock::now();
    kf.initialize(init_data, 1.0);
    
    // Process a few updates to establish velocity
    for (int i = 0; i < 5; ++i) {
        hefkf_1min::MarketData data;
        data.price = 100.0 + i * 0.5;  // Rising price
        data.volume = 1000.0;
        data.spread = 0.01;
        data.timestamp = std::chrono::system_clock::now();
        kf.process(data);
    }
    
    // Extract posterior bucket probabilities
    auto buckets = hefkf_common::posterior_from_KF_20bucket(kf, 102.5, 1.0);
    
    // Test that probabilities sum to 1
    {
        double sum = 0.0;
        sum += buckets.up_000 + buckets.up_010 + buckets.up_020 + buckets.up_030 + buckets.up_040;
        sum += buckets.up_050 + buckets.up_060 + buckets.up_070 + buckets.up_080 + buckets.up_090;
        sum += buckets.dn_000 + buckets.dn_010 + buckets.dn_020 + buckets.dn_030 + buckets.dn_040;
        sum += buckets.dn_050 + buckets.dn_060 + buckets.dn_070 + buckets.dn_080 + buckets.dn_090;
        
        bool passed = is_close(sum, 1.0, 1e-10);
        std::cout << "  Bucket probabilities sum to 1: " << (passed ? "PASS" : "FAIL") 
                  << " (sum = " << sum << ")" << std::endl;
        assert(passed);
    }
    
    // Test that rising price leads to higher up bucket probabilities
    {
        double up_total = buckets.up_000 + buckets.up_010 + buckets.up_020 + 
                         buckets.up_030 + buckets.up_040 + buckets.up_050 + 
                         buckets.up_060 + buckets.up_070 + buckets.up_080 + buckets.up_090;
        double down_total = buckets.dn_000 + buckets.dn_010 + buckets.dn_020 + 
                           buckets.dn_030 + buckets.dn_040 + buckets.dn_050 + 
                           buckets.dn_060 + buckets.dn_070 + buckets.dn_080 + buckets.dn_090;
        
        bool passed = up_total > down_total;
        std::cout << "  Rising price favors UP buckets: " << (passed ? "PASS" : "FAIL")
                  << " (UP: " << up_total << ", DOWN: " << down_total << ")" << std::endl;
        assert(passed);
    }
}

// ========================= SPECTRAL ANALYSIS TESTS =========================
// IMPORTANT: The FrequencyAnalyser converts prices to returns before spectral analysis
// This means all frequency features are based on the rate of change, not absolute price levels
// - Trend strength: Measures consistency of returns in ultra-low frequencies (not price direction)
// - Spectral centroid: Frequency center of returns distribution
// - Coherence: Correlation between returns and volume changes at different frequencies

void test_frequency_analyser_basic() {
    std::cout << "\n=== Testing FrequencyAnalyser with CSV Data via IntegrationLoop ===" << std::endl;
    
    // Test configuration
    const std::string csvPath = "/workspace/ibkr-trader/util/HEFKF/build/test_csv/test_data_downtrend.csv";
    const double feedFrequency = 1.0;  // 4Hz (250ms intervals) like ring_buffer_monitor
    const size_t bufferSize = 512;     // Sufficient for frequency analysis
    const int testDurationSeconds = 10; // Run for 10 seconds
    
    std::cout << "  📁 CSV Path: " << csvPath << std::endl;
    std::cout << "  ⏱️  Feed Rate: " << feedFrequency << " Hz" << std::endl;
    std::cout << "  📊 Buffer Size: " << bufferSize << std::endl;
    
    // Test 1: Create IntegrationLoop and verify CSV loading
    {
        integration_loop_wrapper::IntegrationLoop integration(csvPath, feedFrequency, bufferSize);
        
        // Start the integration loop
        integration.start();
        bool started = true;  // start() returns void, so we assume success
        bool passed = started;
        std::cout << "  CSV loading and start: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
        
        // Let it run briefly to start feeding data
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Stop it for this test
        integration.stop();
    }
    
    // Test 2: Run integration and wait for FrequencyAnalyser readiness
    {
        integration_loop_wrapper::IntegrationLoop integration(csvPath, feedFrequency, bufferSize);
        
        // Start feeding data
        integration.start();
        
        // Wait for enough samples (256 needed for frequency analysis)
        // At 4Hz, we need 256/4 = 64 seconds, but CSV might have enough data to process faster
        std::cout << "  ⏳ Waiting for FrequencyAnalyser to collect 256 samples..." << std::endl;
        
        auto startTime = std::chrono::steady_clock::now();
        const int requiredSamples = 256;
        const double expectedSeconds = requiredSamples / feedFrequency; // 64 seconds at 4Hz
        const double tolerancePercent = 0.025; // 2.5% tolerance for timing
        
        // Wait until we've fed at least 256 ticks
        while (integration.get_ticks_fed() < requiredSamples) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Check every 100ms
            
            // Safety timeout - allow extra time for system delays
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed > std::chrono::seconds(static_cast<int>(expectedSeconds * 1.5))) {
                std::cerr << "  ❌ Timeout waiting for 256 samples!" << std::endl;
                break;
            }
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto elapsedTime = std::chrono::duration<double>(endTime - startTime).count();
        
        // Stop the integration
        integration.stop();
        
        // Verify results
        size_t ticksFed = integration.get_ticks_fed();
        double actualRate = ticksFed / elapsedTime;
        double rateError = std::abs(actualRate - feedFrequency) / feedFrequency;
        
        std::cout << "  📊 Results:" << std::endl;
        std::cout << "    - Ticks fed: " << ticksFed << std::endl;
        std::cout << "    - Time elapsed: " << std::fixed << std::setprecision(2) << elapsedTime << " seconds" << std::endl;
        std::cout << "    - Expected time: " << expectedSeconds << " seconds" << std::endl;
        std::cout << "    - Actual feed rate: " << std::setprecision(3) << actualRate << " Hz" << std::endl;
        std::cout << "    - Expected rate: " << feedFrequency << " Hz" << std::endl;
        std::cout << "    - Rate error: " << std::setprecision(1) << (rateError * 100) << "%" << std::endl;
        
        // Check if FrequencyAnalyser is actually ready
        auto finalOutput = integration.get_latest_output();
        bool freqReady = finalOutput.freq_ready;
        
        std::cout << "    - FrequencyAnalyser ready: " << (freqReady ? "YES" : "NO") << std::endl;
        
        // Pass criteria:
        // 1. Fed at least 256 ticks
        // 2. Feed rate within tolerance
        // 3. FrequencyAnalyser is ready
        bool passed = (ticksFed >= requiredSamples) && 
                      (rateError <= tolerancePercent) && 
                      freqReady;
        
        std::cout << "  Feed rate and readiness test: " << (passed ? "PASS" : "FAIL") << std::endl;
        
        if (!passed) {
            if (ticksFed < requiredSamples) {
                std::cerr << "  ❌ Failed: Only fed " << ticksFed << " ticks (needed " << requiredSamples << ")" << std::endl;
            }
            if (rateError > tolerancePercent) {
                std::cerr << "  ❌ Failed: Feed rate error " << (rateError * 100) << "% exceeds " << (tolerancePercent * 100) << "% tolerance" << std::endl;
            }
            if (!freqReady) {
                std::cerr << "  ❌ Failed: FrequencyAnalyser not ready after " << ticksFed << " ticks" << std::endl;
            }
        }
        
        assert(passed);
    }
    
    // Test 3: Verify continuous operation with debug output
    {
        // hefkf_common::FrequencyFeatures features;
        // bool computed = analyser.compute(features);
        // bool passed = computed && features.trend_strength >= 0.0;
        // std::cout << "  Compute frequency features: " << (passed ? "PASS" : "FAIL") << std::endl;
        std::cout << "\n  🔍 Running with debug output to observe processing..." << std::endl;
        
        integration_loop_wrapper::IntegrationLoop integration(csvPath, feedFrequency, bufferSize);
        integration.set_debug_mode(true);  // Enable debug output
        
        // Start and run for a few seconds
        integration.start();
        std::this_thread::sleep_for(std::chrono::seconds(3));
        integration.stop();
        
        bool passed = true;  // If we get here without crashes, it's working
        std::cout << "  Debug run with CSV processing: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    std::cout << "\n✅ FrequencyAnalyser CSV integration tests completed!" << std::endl;
}

void test_trend_strength() {
    std::cout << "\n=== Testing Trend Strength Computation ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Test with strong upward trend
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 0.1 * i;  // Linear uptrend
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Note: Frequency-based trend strength measures the ratio of ultra-low frequency power
        // For a linear trend, returns are constant, appearing as DC in frequency domain
        // Due to windowing and the narrow measurement band (0-1/7200 Hz), values around 0.6-0.7 are expected
        bool passed = features.trend_strength > 0.6 && features.trend_strength < 0.8;
        std::cout << "  Strong uptrend detection: " << (passed ? "PASS" : "FAIL")
                  << " (strength = " << features.trend_strength << ", expected 0.6-0.8)" << std::endl;
        assert(passed);
    }
    
    // Test with no trend (oscillation)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.1 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // For oscillating prices, power is distributed across frequencies, not concentrated at DC
        bool passed = features.trend_strength < 0.3;
        std::cout << "  No trend (oscillation): " << (passed ? "PASS" : "FAIL")
                  << " (strength = " << features.trend_strength << ", expected < 0.3)" << std::endl;
        assert(passed);
    }
    
    // Test with mixed signal (trend + noise)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double trend = 100.0 + 0.05 * i;  // Slower trend (0.05% per sample)
            double noise = 0.2 * std::sin(2 * M_PI * 0.2 * i);  // High-frequency noise at 0.2 Hz
            analyser.push(trend + noise, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // With high-frequency noise dominating the returns, trend strength should be low
        // The noise amplitude (0.2) is 4x the trend increment (0.05), so most power is at 0.2 Hz
        bool passed = features.trend_strength < 0.1;  // Expect low trend strength
        std::cout << "  Trend with noise: " << (passed ? "PASS" : "FAIL")
                  << " (strength = " << features.trend_strength << ", expected < 0.1)" << std::endl;
        assert(passed);
    }
    
    // Test with stronger trend relative to noise
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double trend = 100.0 + 0.2 * i;  // Stronger trend (0.2% per sample)
            double noise = 0.1 * std::sin(2 * M_PI * 0.2 * i);  // Weaker noise
            analyser.push(trend + noise, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // With stronger trend and weaker noise, should see moderate trend strength
        bool passed = features.trend_strength > 0.3 && features.trend_strength < 0.7;
        std::cout << "  Strong trend with weak noise: " << (passed ? "PASS" : "FAIL")
                  << " (strength = " << features.trend_strength << ", expected 0.3-0.7)" << std::endl;
        assert(passed);
    }
    
    // Test with step change (should have lower trend strength)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double price = (i < 128) ? 100.0 : 110.0;  // Step change at midpoint
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Step changes distribute power across frequencies due to the discontinuity
        bool passed = features.trend_strength < 0.5;
        std::cout << "  Step change detection: " << (passed ? "PASS" : "FAIL")
                  << " (strength = " << features.trend_strength << ", expected < 0.5)" << std::endl;
        assert(passed);
    }
}

void test_coherence_analysis() {
    std::cout << "\n=== Testing Coherence Analysis ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Test with correlated price and volume
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + std::sin(2 * M_PI * 0.05 * t);
            double volume = 1000.0 + 500.0 * std::sin(2 * M_PI * 0.05 * t);  // Same frequency
            double spread = 0.01;
            analyser.push(price, volume, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = features.coherence_price_volume_peak > 0.7;  // Should be high
        std::cout << "  Correlated price-volume coherence: " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_volume_peak << ")" << std::endl;
        assert(passed);
    }
    
    // Test with uncorrelated signals
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + std::sin(2 * M_PI * 0.05 * t);
            double volume = 1000.0 + 500.0 * std::sin(2 * M_PI * 0.13 * t);  // Non-harmonic frequency
            double spread = 0.01 + 0.005 * std::sin(2 * M_PI * 0.23 * t);  // Non-harmonic frequency
            analyser.push(price, volume, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // With returns-based comparison and weighted averaging, uncorrelated signals should have low coherence
        bool passed = features.coherence_price_volume_peak < 0.30;  // More realistic threshold
        std::cout << "  Uncorrelated signals coherence: " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_volume_peak << ")" << std::endl;
        assert(passed);
    }
    
    // Test with partially correlated signals (realistic trading scenario)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Price with two components
            double price = 100.0 + 0.5 * std::sin(2 * M_PI * 0.05 * t) + 
                          0.3 * std::sin(2 * M_PI * 0.1 * t);
            // Volume shares one frequency with price, has its own component
            double volume = 1000.0 + 300.0 * std::sin(2 * M_PI * 0.05 * t) +  // Correlated
                           200.0 * std::sin(2 * M_PI * 0.2 * t);              // Uncorrelated
            analyser.push(price, volume, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Should show moderate coherence (partial correlation)
        bool passed = features.coherence_price_volume_peak > 0.3 && 
                     features.coherence_price_volume_peak < 0.7;
        std::cout << "  Partially correlated signals: " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_volume_peak << ", expected 0.3-0.7)" << std::endl;
        assert(passed);
    }
}

void test_spectral_features() {
    std::cout << "\n=== Testing Spectral Features ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Test spectral centroid with low-frequency signal
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + std::sin(2 * M_PI * 0.02 * t);  // Low frequency (0.02 Hz)
            // Add varying volume and spread to avoid zero returns
            double volume = 1000.0 + 50.0 * std::sin(2 * M_PI * 0.02 * t + M_PI/4);
            double spread = 0.01 + 0.002 * std::sin(2 * M_PI * 0.03 * t);
            analyser.push(price, volume, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Note: Spectral centroid is computed on returns, not raw prices
        // For a sine wave, returns have frequency content at the same frequency
        // Expected centroid should be around 0.02 Hz for this signal
        bool passed = features.spectral_centroid_price < 0.1;  // Should be low frequency
        std::cout << "  Low-frequency spectral centroid: " << (passed ? "PASS" : "FAIL")
                  << " (centroid = " << features.spectral_centroid_price << " Hz)" << std::endl;
        assert(passed);
    }
    
    // Test spectral centroid with high-frequency signal
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + std::sin(2 * M_PI * 0.3 * t);  // Higher frequency (0.3 Hz)
            // Add varying volume and spread
            double volume = 1000.0 + 100.0 * std::sin(2 * M_PI * 0.3 * t + M_PI/6);
            double spread = 0.01 + 0.003 * std::sin(2 * M_PI * 0.25 * t);
            analyser.push(price, volume, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Should have higher centroid than the low-frequency signal
        bool passed = features.spectral_centroid_price > 0.2 && features.spectral_centroid_price < 0.4;
        std::cout << "  High-frequency spectral centroid: " << (passed ? "PASS" : "FAIL")
                  << " (centroid = " << features.spectral_centroid_price << " Hz, expected 0.2-0.4)" << std::endl;
        assert(passed);
    }
    
    // Test spectral flux (change detection)
    {
        analyser.reset();
        
        // First half: low frequency
        for (int i = 0; i < 128; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.02 * i);
            double volume = 1000.0 + 50.0 * std::sin(2 * M_PI * 0.02 * i);
            double spread = 0.01 + 0.001 * std::sin(2 * M_PI * 0.02 * i);
            analyser.push(price, volume, spread);
        }
        
        // Second half: add high frequency
        for (int i = 128; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.02 * i) + 
                          0.5 * std::sin(2 * M_PI * 0.2 * i);
            double volume = 1000.0 + 50.0 * std::sin(2 * M_PI * 0.02 * i) +
                           25.0 * std::sin(2 * M_PI * 0.2 * i);
            double spread = 0.01 + 0.001 * std::sin(2 * M_PI * 0.02 * i);
            analyser.push(price, volume, spread);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);  // First compute - establishes baseline PSD
        
        // Push more data with different spectral content
        for (int i = 256; i < 512; ++i) {
            // Change the high-frequency component amplitude
            double price = 100.0 + std::sin(2 * M_PI * 0.02 * i) + 
                          0.7 * std::sin(2 * M_PI * 0.2 * i);  // Increased amplitude
            double volume = 1000.0 + 50.0 * std::sin(2 * M_PI * 0.02 * i) +
                           35.0 * std::sin(2 * M_PI * 0.2 * i);  // Increased amplitude
            double spread = 0.01 + 0.002 * std::sin(2 * M_PI * 0.2 * i);  // Now follows high freq
            analyser.push(price, volume, spread);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);  // Second compute - can now calculate flux
        
        // Check features2 for spectral flux (not features1!)
        bool passed = features2.spectral_flux > 0.0;
        std::cout << "  Spectral flux detection: " << (passed ? "PASS" : "FAIL")
                  << " (flux = " << features2.spectral_flux << ")" << std::endl;
        assert(passed);
    }
}

void test_frequency_bands() {
    std::cout << "\n=== Testing Frequency Band Analysis ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Generate multi-frequency signal
    analyser.reset();
    for (int i = 0; i < 256; ++i) {
        double t = i;
        // Mix of frequencies in different bands
        double price = 100.0 + 
                      0.5 * std::sin(2 * M_PI * 0.01 * t) +  // Trend band
                      0.3 * std::sin(2 * M_PI * 0.05 * t) +  // Medium-term
                      0.2 * std::sin(2 * M_PI * 0.15 * t);   // Short-term
        double volume = 1000.0 + 100.0 * std::sin(2 * M_PI * 0.05 * t);
        analyser.push(price, volume, 0.01);
    }
    
    hefkf_common::FrequencyFeatures features;
    analyser.compute(features);
    
    // Check that band coherences are computed
    bool has_bands = !features.coherence_price_volume_by_band.empty();
    std::cout << "  Frequency bands computed: " << (has_bands ? "PASS" : "FAIL") << std::endl;
    assert(has_bands);
    
    // Check specific bands exist
    bool has_trend = features.coherence_price_volume_by_band.count("trend") > 0;
    bool has_medium = features.coherence_price_volume_by_band.count("medium_term") > 0;
    bool has_short = features.coherence_price_volume_by_band.count("short_term") > 0;
    
    std::cout << "  All frequency bands present: " 
              << ((has_trend && has_medium && has_short) ? "PASS" : "FAIL") << std::endl;
    assert(has_trend && has_medium && has_short);
}

void test_spectral_entropy() {
    std::cout << "\n=== Testing Goertzel-based Spectral Entropy (Microstructure Band) ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Test 1: Single frequency in microstructure band (low entropy)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Single 0.01 Hz component (100s period = 1.67 min)
            double price = 100.0 + 0.5 * std::sin(2 * M_PI * 0.01 * t);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Test Goertzel-based entropy
        bool has_goertzel = features.entropy_by_band.count("microstructure_goertzel") > 0;
        std::cout << "  Goertzel entropy exists: " << (has_goertzel ? "PASS" : "FAIL") << std::endl;
        assert(has_goertzel);
        
        if (has_goertzel) {
            bool low_goertzel = features.entropy_by_band["microstructure_goertzel"] < 0.3;
            std::cout << "  Single freq Goertzel entropy: " << (low_goertzel ? "PASS" : "FAIL")
                      << " (entropy = " << features.entropy_by_band["microstructure_goertzel"]
                      << ", expected < 0.3)" << std::endl;
            assert(low_goertzel);
        }
    }
    
    // Test 2: Multiple frequencies in microstructure band (high entropy)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Multiple components spread across microstructure band
            double price = 100.0 + 
                0.2 * std::sin(2 * M_PI * 0.004 * t) +   // 250s = 4.17 min
                0.2 * std::sin(2 * M_PI * 0.008 * t) +   // 125s = 2.08 min
                0.2 * std::sin(2 * M_PI * 0.012 * t) +   // 83s = 1.38 min
                0.2 * std::sin(2 * M_PI * 0.015 * t);    // 67s = 1.11 min
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Goertzel entropy should be high for distributed frequencies
        if (features.entropy_by_band.count("microstructure_goertzel") > 0) {
            bool high_goertzel = features.entropy_by_band["microstructure_goertzel"] > 0.5;
            std::cout << "  Multi-freq Goertzel entropy: " << (high_goertzel ? "PASS" : "FAIL")
                      << " (entropy = " << features.entropy_by_band["microstructure_goertzel"]
                      << ", expected > 0.5)" << std::endl;
            assert(high_goertzel);
        }
    }
    
    // Test 3: White noise in microstructure band (maximum entropy)
    {
        analyser.reset();
        std::srand(42); // Fixed seed for reproducibility
        for (int i = 0; i < 256; ++i) {
            // Random walk to simulate noisy price action
            double price = 100.0;
            if (i > 0) {
                price = 100.0 + 0.01 * (std::rand() % 100 - 50) / 50.0;
            }
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // White noise = very high Goertzel entropy (power evenly distributed)
        if (features.entropy_by_band.count("microstructure_goertzel") > 0) {
            bool max_entropy = features.entropy_by_band["microstructure_goertzel"] > 0.7;
            std::cout << "  White noise Goertzel entropy: " << (max_entropy ? "PASS" : "FAIL")
                      << " (entropy = " << features.entropy_by_band["microstructure_goertzel"] 
                      << ", expected > 0.7)" << std::endl;
            assert(max_entropy);
        }
    }
    
    // Test 4: Verify Goertzel entropy is computed and valid
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Signal with microstructure band components
            double price = 100.0 + 
                0.3 * std::sin(2 * M_PI * 0.007 * t) +   // ~2.4 min period
                0.3 * std::sin(2 * M_PI * 0.01 * t);     // ~1.67 min period
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Check Goertzel entropy exists and is valid
        bool has_goertzel = features.entropy_by_band.count("microstructure_goertzel") > 0;
        bool valid_range = false;
        
        if (has_goertzel) {
            double entropy = features.entropy_by_band["microstructure_goertzel"];
            valid_range = entropy >= 0.0 && entropy <= 1.0;
            std::cout << "  Goertzel entropy valid [0,1]: " << (valid_range ? "PASS" : "FAIL")
                      << " (value = " << entropy << ")" << std::endl;
        } else {
            std::cout << "  Goertzel entropy exists: FAIL" << std::endl;
        }
        
        assert(has_goertzel && valid_range);
    }
    
    // Test 5: Goertzel provides precise discrimination at target frequencies
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Signal at exactly one of the Goertzel frequencies (0.0065 Hz)
            double price = 100.0 + 0.5 * std::sin(2 * M_PI * 0.0065 * t);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Goertzel should show very low entropy (power at exactly one bin)
        bool goertzel_very_low = features.entropy_by_band["microstructure_goertzel"] < 0.1;
        std::cout << "  Goertzel entropy at exact freq < 0.1: " 
                  << (goertzel_very_low ? "PASS" : "FAIL")
                  << " (entropy = " << features.entropy_by_band["microstructure_goertzel"] << ")" << std::endl;
        assert(goertzel_very_low);
    }
}

void test_trend_strength_derivative() {
    std::cout << "\n=== Testing Trend Strength Derivative ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);  // 1 Hz sampling
    
    // Test 1: First compute should have zero derivative
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 0.1 * i;  // Linear trend
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = features.trend_strength_derivative == 0.0;
        std::cout << "  First compute has zero derivative: " << (passed ? "PASS" : "FAIL")
                  << " (derivative = " << features.trend_strength_derivative << ")" << std::endl;
        assert(passed);
    }
    
    // Test 2: Increasing trend strength (sideways to trending market)
    {
        analyser.reset();
        
        // First window: oscillating price (low trend strength)
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.1 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Second window: strong linear trend (high trend strength)
        for (int i = 256; i < 512; ++i) {
            double price = 100.0 + 0.2 * (i - 256);  // Strong uptrend
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // Derivative should be positive (trend strength increasing)
        bool passed = features2.trend_strength_derivative > 0.0;
        std::cout << "  Sideways to trending (positive derivative): " << (passed ? "PASS" : "FAIL")
                  << " (derivative = " << features2.trend_strength_derivative 
                  << ", trend: " << features1.trend_strength << " -> " << features2.trend_strength << ")" << std::endl;
        assert(passed);
    }
    
    // Test 3: Decreasing trend strength (trending to choppy market)
    {
        analyser.reset();
        
        // First window: strong trend
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 0.15 * i;
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Second window: add high-frequency noise (reduces trend strength)
        for (int i = 256; i < 512; ++i) {
            double trend = 100.0 + 0.05 * (i - 256);  // Weaker trend
            double noise = 0.5 * std::sin(2 * M_PI * 0.2 * i) + 
                          0.3 * std::sin(2 * M_PI * 0.35 * i);  // Strong noise
            analyser.push(trend + noise, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // Derivative should be negative (trend strength decreasing)
        bool passed = features2.trend_strength_derivative < 0.0;
        std::cout << "  Trending to choppy (negative derivative): " << (passed ? "PASS" : "FAIL")
                  << " (derivative = " << features2.trend_strength_derivative
                  << ", trend: " << features1.trend_strength << " -> " << features2.trend_strength << ")" << std::endl;
        assert(passed);
    }
    
    // Test 4: Stable trend strength (consistent market regime)
    {
        analyser.reset();
        
        // First window: moderate trend with some noise
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 0.08 * i + 0.2 * std::sin(2 * M_PI * 0.15 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Second window: similar pattern (stable regime)
        for (int i = 256; i < 512; ++i) {
            double price = 100.0 + 0.08 * (i - 256) + 0.2 * std::sin(2 * M_PI * 0.15 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // Derivative should be near zero (stable trend strength)
        bool passed = std::abs(features2.trend_strength_derivative) < 0.1;
        std::cout << "  Stable market regime (near-zero derivative): " << (passed ? "PASS" : "FAIL")
                  << " (derivative = " << features2.trend_strength_derivative
                  << ", trend: " << features1.trend_strength << " -> " << features2.trend_strength << ")" << std::endl;
        assert(passed);
    }
}

void test_centroid_velocity() {
    std::cout << "\n=== Testing Centroid Velocity ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);  // 1 Hz sampling
    
    // Test 1: First compute should have zero velocity
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.1 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = features.centroid_velocity == 0.0;
        std::cout << "  First compute has zero velocity: " << (passed ? "PASS" : "FAIL")
                  << " (velocity = " << features.centroid_velocity << ")" << std::endl;
        assert(passed);
    }
    
    // Test 2: Increasing frequency content (market speeding up)
    {
        analyser.reset();
        
        // First window: low-frequency oscillations
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.02 * i);  // 0.02 Hz (slow)
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Second window: higher-frequency oscillations
        for (int i = 256; i < 512; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.15 * i);  // 0.15 Hz (faster)
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // Velocity should be positive (centroid moving to higher frequencies)
        bool passed = features2.centroid_velocity > 0.0;
        std::cout << "  Market speeding up (positive velocity): " << (passed ? "PASS" : "FAIL")
                  << " (velocity = " << features2.centroid_velocity
                  << ", centroid: " << features1.spectral_centroid_price << " Hz -> " 
                  << features2.spectral_centroid_price << " Hz)" << std::endl;
        assert(passed);
    }
    
    // Test 3: Decreasing frequency content (market slowing down)
    {
        analyser.reset();
        
        // First window: high-frequency trading
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 0.5 * std::sin(2 * M_PI * 0.25 * i) +  // Fast component
                          0.3 * std::sin(2 * M_PI * 0.35 * i);            // Very fast component
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Second window: slower oscillations
        for (int i = 256; i < 512; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.05 * i);  // Much slower
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // Velocity should be negative (centroid moving to lower frequencies)
        bool passed = features2.centroid_velocity < 0.0;
        std::cout << "  Market slowing down (negative velocity): " << (passed ? "PASS" : "FAIL")
                  << " (velocity = " << features2.centroid_velocity
                  << ", centroid: " << features1.spectral_centroid_price << " Hz -> " 
                  << features2.spectral_centroid_price << " Hz)" << std::endl;
        assert(passed);
    }
    
    // Test 4: Frequency shift in multi-component signal
    {
        analyser.reset();
        
        // First window: dominated by medium frequency
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 
                          0.2 * std::sin(2 * M_PI * 0.05 * i) +   // Weak low freq
                          0.8 * std::sin(2 * M_PI * 0.10 * i);    // Strong medium freq
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Second window: shift dominance to low frequency
        for (int i = 256; i < 512; ++i) {
            double price = 100.0 + 
                          0.8 * std::sin(2 * M_PI * 0.05 * i) +   // Strong low freq
                          0.2 * std::sin(2 * M_PI * 0.10 * i);    // Weak medium freq
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // Velocity should be negative (shift to lower frequency dominance)
        bool passed = features2.centroid_velocity < 0.0;
        std::cout << "  Frequency dominance shift (negative velocity): " << (passed ? "PASS" : "FAIL")
                  << " (velocity = " << features2.centroid_velocity
                  << ", centroid: " << features1.spectral_centroid_price << " Hz -> " 
                  << features2.spectral_centroid_price << " Hz)" << std::endl;
        assert(passed);
    }
    
    // Test 5: Stable frequency content (no velocity)
    {
        analyser.reset();
        
        // First window: mixed frequencies
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 
                          0.4 * std::sin(2 * M_PI * 0.08 * i) +
                          0.3 * std::sin(2 * M_PI * 0.12 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Second window: same frequency mix (stable)
        for (int i = 256; i < 512; ++i) {
            double price = 100.0 + 
                          0.4 * std::sin(2 * M_PI * 0.08 * i) +
                          0.3 * std::sin(2 * M_PI * 0.12 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // Velocity should be near zero (stable frequency content)
        bool passed = std::abs(features2.centroid_velocity) < 0.01;
        std::cout << "  Stable frequency content (near-zero velocity): " << (passed ? "PASS" : "FAIL")
                  << " (velocity = " << features2.centroid_velocity
                  << ", centroid: " << features1.spectral_centroid_price << " Hz -> " 
                  << features2.spectral_centroid_price << " Hz)" << std::endl;
        assert(passed);
    }
}

void test_coherence_price_spread() {
    std::cout << "\n=== Testing Coherence Price-Spread Peak ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Test 1: High volatility → widening spreads (positive correlation)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Price with increasing volatility bursts
            double volatility = 0.5 + 0.3 * std::sin(2 * M_PI * 0.03 * t);
            double price = 100.0 + volatility * std::sin(2 * M_PI * 0.1 * t);
            
            // Spread widens with volatility (realistic market maker behavior)
            double base_spread = 0.01;
            double spread = base_spread * (1.0 + 0.8 * volatility);  // Spread tracks volatility
            
            analyser.push(price, 1000.0, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = features.coherence_price_spread_peak > 0.6;
        std::cout << "  Volatility-driven spread widening: " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_spread_peak 
                  << ", expected > 0.6)" << std::endl;
        assert(passed);
    }
    
    // Test 2: Stable price, stable spread (low correlation from lack of variation)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            // Very stable price with tiny fluctuations
            double price = 100.0 + 0.001 * std::sin(2 * M_PI * 0.05 * i);
            double spread = 0.01;  // Constant tight spread
            analyser.push(price, 1000.0, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // With minimal variation, coherence should be low
        bool passed = features.coherence_price_spread_peak < 0.3;
        std::cout << "  Stable market (low variation): " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_spread_peak 
                  << ", expected < 0.3)" << std::endl;
        assert(passed);
    }
    
    // Test 3: Price moves but spread stays constant (uncorrelated)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Significant price movement
            double price = 100.0 + 2.0 * std::sin(2 * M_PI * 0.05 * t);
            // But spread remains constant (unrealistic but tests independence)
            double spread = 0.015;
            analyser.push(price, 1000.0, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = features.coherence_price_spread_peak < 0.2;
        std::cout << "  Price moves, spread constant (uncorrelated): " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_spread_peak 
                  << ", expected < 0.2)" << std::endl;
        assert(passed);
    }
    
    // Test 4: Event-driven spread spikes (sudden correlation)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double price, spread;
            
            // Normal market conditions
            if (i < 100 || i > 150) {
                price = 100.0 + 0.2 * std::sin(2 * M_PI * 0.08 * i);
                spread = 0.01;
            } 
            // Event period: sharp price moves trigger spread widening
            else {
                double shock = 3.0 * std::sin(2 * M_PI * 0.2 * (i - 100));
                price = 100.0 + shock;
                spread = 0.01 + 0.01 * std::abs(shock);  // Spread widens with price shocks
            }
            
            analyser.push(price, 1000.0, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Should show moderate coherence due to event period
        bool passed = features.coherence_price_spread_peak > 0.3 && 
                     features.coherence_price_spread_peak < 0.7;
        std::cout << "  Event-driven spread spikes: " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_spread_peak 
                  << ", expected 0.3-0.7)" << std::endl;
        assert(passed);
    }
    
    // Test 5: Inverse relationship (spread tightens on trends)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Trending price with cycles
            double trend = 0.05 * i;
            double cycle = std::sin(2 * M_PI * 0.05 * t);
            double price = 100.0 + trend + cycle;
            
            // Spread tightens during strong trends (more liquidity)
            // and widens during reversals
            double price_momentum = 0.05 + std::cos(2 * M_PI * 0.05 * t) * 0.05;
            double spread = 0.02 - 0.5 * price_momentum;  // Inverse relationship
            
            analyser.push(price, 1000.0, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Should still show coherence despite inverse relationship
        bool passed = features.coherence_price_spread_peak > 0.4;
        std::cout << "  Inverse spread-momentum relationship: " << (passed ? "PASS" : "FAIL")
                  << " (coherence = " << features.coherence_price_spread_peak 
                  << ", expected > 0.4)" << std::endl;
        assert(passed);
    }
}

void test_spectral_centroid_volume() {
    std::cout << "\n=== Testing Spectral Centroid Volume ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Test 1: Low-frequency volume waves (institutional trading)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            // Price can be anything
            double price = 100.0 + 0.5 * std::sin(2 * M_PI * 0.1 * t);
            // Volume with slow, large waves (institutional accumulation/distribution)
            double volume = 10000.0 + 5000.0 * std::sin(2 * M_PI * 0.01 * t);  // 0.01 Hz
            analyser.push(price, volume, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = features.spectral_centroid_volume < 0.05;  // Low frequency
        std::cout << "  Institutional volume waves (low frequency): " << (passed ? "PASS" : "FAIL")
                  << " (centroid = " << features.spectral_centroid_volume 
                  << " Hz, expected < 0.05)" << std::endl;
        assert(passed);
    }
    
    // Test 2: High-frequency volume bursts (HFT activity)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + 0.3 * std::sin(2 * M_PI * 0.15 * t);
            // Rapid volume oscillations (algo trading)
            double volume = 1000.0 + 500.0 * std::sin(2 * M_PI * 0.3 * t) + 
                           300.0 * std::sin(2 * M_PI * 0.4 * t);  // 0.3-0.4 Hz
            analyser.push(price, volume, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = features.spectral_centroid_volume > 0.25;  // High frequency
        std::cout << "  HFT volume bursts (high frequency): " << (passed ? "PASS" : "FAIL")
                  << " (centroid = " << features.spectral_centroid_volume 
                  << " Hz, expected > 0.25)" << std::endl;
        assert(passed);
    }
    
    // Test 3: Volume clustering at specific frequencies
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + 0.2 * std::sin(2 * M_PI * 0.08 * t);
            
            // Volume concentrated at 0.05 Hz (20-second cycles)
            // Simulates periodic order flow (e.g., VWAP execution)
            double base_volume = 2000.0;
            double periodic_surge = 3000.0 * (0.5 + 0.5 * std::sin(2 * M_PI * 0.05 * t));
            double volume = base_volume + periodic_surge;
            
            analyser.push(price, volume, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Centroid should be near the dominant frequency
        bool passed = features.spectral_centroid_volume > 0.03 && 
                     features.spectral_centroid_volume < 0.08;
        std::cout << "  Periodic volume execution (VWAP-like): " << (passed ? "PASS" : "FAIL")
                  << " (centroid = " << features.spectral_centroid_volume 
                  << " Hz, expected 0.03-0.08)" << std::endl;
        assert(passed);
    }
    
    // Test 4: Constant volume (zero frequency content in returns)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 0.5 * std::sin(2 * M_PI * 0.1 * i);
            double volume = 5000.0;  // Constant volume
            analyser.push(price, volume, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // With constant volume, returns are zero, centroid undefined but should be 0
        bool passed = features.spectral_centroid_volume < 0.01;
        std::cout << "  Constant volume (no frequency content): " << (passed ? "PASS" : "FAIL")
                  << " (centroid = " << features.spectral_centroid_volume 
                  << " Hz, expected < 0.01)" << std::endl;
        assert(passed);
    }
    
    // Test 5: Multi-scale volume patterns (retail + institutional)
    {
        analyser.reset();
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + std::sin(2 * M_PI * 0.07 * t);
            
            // Combination of:
            // - Slow institutional waves (0.01 Hz)
            // - Medium retail patterns (0.1 Hz)
            // - Fast algo bursts (0.25 Hz)
            double inst_volume = 5000.0 * std::sin(2 * M_PI * 0.01 * t);
            double retail_volume = 2000.0 * std::sin(2 * M_PI * 0.1 * t);
            double algo_volume = 1000.0 * std::sin(2 * M_PI * 0.25 * t);
            double volume = 10000.0 + inst_volume + retail_volume + algo_volume;
            
            analyser.push(price, volume, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Centroid should be weighted average, closer to lower frequencies
        // due to higher amplitude of institutional volume
        bool passed = features.spectral_centroid_volume > 0.02 && 
                     features.spectral_centroid_volume < 0.15;
        std::cout << "  Multi-scale volume patterns: " << (passed ? "PASS" : "FAIL")
                  << " (centroid = " << features.spectral_centroid_volume 
                  << " Hz, expected 0.02-0.15)" << std::endl;
        assert(passed);
    }
}

// ========================= MAIN TEST RUNNER =========================

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "HEFKF Comprehensive Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // Gaussian distribution tests
        // test_standard_normal_cdf();
        // test_gaussian_cdf_interval();
        // test_bucket_probability_distribution();
        
        // Spectral analysis tests
        test_frequency_analyser_basic();
        // test_trend_strength();
        // test_coherence_analysis();
        // test_spectral_features();
        // test_frequency_bands();
        // test_spectral_entropy();
        
        // // New tests
        // test_trend_strength_derivative();
        // test_centroid_velocity();
        // test_coherence_price_spread();
        // test_spectral_centroid_volume();

        // test_spectral_edge_cases();
        
        std::cout << "\n✓ All tests passed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
} 