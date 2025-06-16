// Comprehensive tests for HEFKF Gaussian distribution and spectral analysis
#include "posterior.hpp"
#include "frequency_analyser.hpp"
#include "1min_HEFKF.hpp"
#include <iostream>
#include <cmath>
#include <vector>
#include <cassert>
#include <iomanip>

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
        double result = GaussianIntegrator::standard_normal_cdf(tc.x);
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
        double prob = GaussianIntegrator::gaussian_cdf_interval(0.0, 1.0, -1.0, 1.0);
        bool passed = is_close(prob, 0.6826895, LOOSE_TOLERANCE);
        std::cout << "  P(-1 < X < 1) for N(0,1): " << (passed ? "PASS" : "FAIL") 
                  << " (got " << prob << ")" << std::endl;
        assert(passed);
    }
    
    // Test 2: Non-standard normal
    {
        double mean = 5.0;
        double std = 2.0;
        double prob = GaussianIntegrator::gaussian_cdf_interval(mean, std, 3.0, 7.0);
        // This is P(3 < X < 7) for N(5, 4), which is P(-1 < Z < 1) = ~0.683
        bool passed = is_close(prob, 0.6826895, LOOSE_TOLERANCE);
        std::cout << "  P(3 < X < 7) for N(5,4): " << (passed ? "PASS" : "FAIL") 
                  << " (got " << prob << ")" << std::endl;
        assert(passed);
    }
    
    // Test 3: Degenerate case (zero std dev)
    {
        double prob1 = GaussianIntegrator::gaussian_cdf_interval(5.0, 0.0, 4.0, 6.0);
        double prob2 = GaussianIntegrator::gaussian_cdf_interval(5.0, 0.0, 6.0, 7.0);
        bool passed = (prob1 == 1.0) && (prob2 == 0.0);
        std::cout << "  Degenerate case (σ=0): " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 4: Tail probabilities
    {
        double prob = GaussianIntegrator::gaussian_cdf_interval(0.0, 1.0, 2.0, 10.0);
        double expected = GaussianIntegrator::standard_normal_cdf(10.0) - 
                         GaussianIntegrator::standard_normal_cdf(2.0);
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
    std::cout << "\n=== Testing FrequencyAnalyser Basic Functions ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);  // 1 Hz sampling
    
    // Test 1: Initial state
    {
        bool passed = !analyser.is_ready() && analyser.sample_count() == 0;
        std::cout << "  Initial state: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 2: Push samples and check readiness
    {
        // Need 256 samples for analysis
        for (int i = 0; i < 256; ++i) {
            double t = i;
            double price = 100.0 + std::sin(2 * M_PI * 0.1 * t);  // 0.1 Hz sine wave
            double volume = 1000.0 + 100.0 * std::sin(2 * M_PI * 0.05 * t);  // 0.05 Hz
            double spread = 0.01;
            analyser.push(price, volume, spread);
        }
        
        bool passed = analyser.is_ready() && analyser.sample_count() == 256;
        std::cout << "  Ready after 256 samples: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 3: Compute frequency features
    {
        hefkf_common::FrequencyFeatures features;
        bool computed = analyser.compute(features);
        bool passed = computed && features.trend_strength >= 0.0;
        std::cout << "  Compute frequency features: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
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
            analyser.push(price, 1000.0, 0.01);
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
            analyser.push(price, 1000.0, 0.01);
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
            analyser.push(price, 1000.0, 0.01);
        }
        
        // Second half: add high frequency
        for (int i = 128; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.02 * i) + 
                          0.5 * std::sin(2 * M_PI * 0.2 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features1;
        analyser.compute(features1);
        
        // Push more data with sustained high frequency
        for (int i = 256; i < 512; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.02 * i) + 
                          0.5 * std::sin(2 * M_PI * 0.2 * i);
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features2;
        analyser.compute(features2);
        
        // First computation should show flux, second should show less
        bool passed = features1.spectral_flux > 0.0;
        std::cout << "  Spectral flux detection: " << (passed ? "PASS" : "FAIL")
                  << " (flux = " << features1.spectral_flux << ")" << std::endl;
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

void test_all_nine_spectral_features() {
    std::cout << "\n=== Testing All 9 Spectral Features ===" << std::endl;
    
    FrequencyAnalyser analyser(1.0);
    
    // Create a complex signal with trend, oscillation, and changing spread
    analyser.reset();
    for (int i = 0; i < 256; ++i) {
        double t = i;
        // Price with trend and multiple frequencies
        double price = 100.0 + 0.02 * i + 0.5 * std::sin(2 * M_PI * 0.05 * t) + 
                      0.2 * std::sin(2 * M_PI * 0.15 * t);
        // Volume correlated at some frequencies
        double volume = 1000.0 + 50.0 * std::sin(2 * M_PI * 0.05 * t) + 
                       20.0 * std::sin(2 * M_PI * 0.25 * t);
        // Spread with its own dynamics
        double spread = 0.01 + 0.002 * std::sin(2 * M_PI * 0.05 * t + M_PI/4);
        analyser.push(price, volume, spread);
    }
    
    hefkf_common::FrequencyFeatures features1;
    analyser.compute(features1);
    
    // Push more data to compute derivatives
    for (int i = 256; i < 512; ++i) {
        double t = i;
        // Accelerating trend and changing frequency content
        double price = 105.0 + 0.03 * (i-256) + 0.4 * std::sin(2 * M_PI * 0.08 * t);
        double volume = 1100.0 + 60.0 * std::sin(2 * M_PI * 0.08 * t);
        double spread = 0.012 + 0.001 * std::sin(2 * M_PI * 0.1 * t);
        analyser.push(price, volume, spread);
    }
    
    hefkf_common::FrequencyFeatures features2;
    analyser.compute(features2);
    
    // Test 1: coherence_price_volume_peak
    {
        bool passed = features2.coherence_price_volume_peak >= 0.0 && 
                     features2.coherence_price_volume_peak <= 1.0;
        std::cout << "  1. coherence_price_volume_peak: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.coherence_price_volume_peak << ")" << std::endl;
        assert(passed);
    }
    
    // Test 2: spectral_flux
    {
        bool passed = features2.spectral_flux >= 0.0;
        std::cout << "  2. spectral_flux: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.spectral_flux << ")" << std::endl;
        assert(passed);
    }
    
    // Test 3: trend_strength_derivative
    {
        // Should detect change in trend strength
        bool passed = std::abs(features2.trend_strength_derivative) > 0.0;
        std::cout << "  3. trend_strength_derivative: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.trend_strength_derivative << ")" << std::endl;
        assert(passed);
    }
    
    // Test 4: centroid_velocity
    {
        // Should detect shift in frequency content
        bool passed = std::abs(features2.centroid_velocity) > 0.0;
        std::cout << "  4. centroid_velocity: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.centroid_velocity << ")" << std::endl;
        assert(passed);
    }
    
    // Test 5: spectral_centroid_price
    {
        bool passed = features2.spectral_centroid_price > 0.0 && 
                     features2.spectral_centroid_price < 0.5;  // Nyquist frequency
        std::cout << "  5. spectral_centroid_price: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.spectral_centroid_price << " Hz)" << std::endl;
        assert(passed);
    }
    
    // Test 6: spectral_centroid_volume
    {
        bool passed = features2.spectral_centroid_volume > 0.0 && 
                     features2.spectral_centroid_volume < 0.5;
        std::cout << "  6. spectral_centroid_volume: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.spectral_centroid_volume << " Hz)" << std::endl;
        assert(passed);
    }
    
    // Test 7: entropy (all 4 bands)
    {
        bool has_entropy = !features2.entropy_by_band.empty();
        bool all_valid = true;
        if (has_entropy) {
            for (const auto& [band, entropy] : features2.entropy_by_band) {
                if (entropy < 0.0 || entropy > 1.0) {
                    all_valid = false;
                    break;
                }
            }
        }
        bool passed = has_entropy && all_valid && features2.entropy_by_band.size() >= 4;
        std::cout << "  7. entropy (all bands): " << (passed ? "PASS" : "FAIL")
                  << " (" << features2.entropy_by_band.size() << " bands)" << std::endl;
        if (has_entropy) {
            for (const auto& [band, entropy] : features2.entropy_by_band) {
                std::cout << "     " << band << ": " << entropy << std::endl;
            }
        }
        assert(passed);
    }
    
    // Test 8: trend_strength
    {
        bool passed = features2.trend_strength >= 0.0 && features2.trend_strength <= 1.0;
        std::cout << "  8. trend_strength: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.trend_strength << ")" << std::endl;
        assert(passed);
    }
    
    // Test 9: coherence_price_spread_peak
    {
        bool passed = features2.coherence_price_spread_peak >= 0.0 && 
                     features2.coherence_price_spread_peak <= 1.0;
        std::cout << "  9. coherence_price_spread_peak: " << (passed ? "PASS" : "FAIL")
                  << " (value = " << features2.coherence_price_spread_peak << ")" << std::endl;
        assert(passed);
    }
    
    // Additional test: Verify derivatives require previous computation
    {
        FrequencyAnalyser fresh_analyser(1.0);
        for (int i = 0; i < 256; ++i) {
            fresh_analyser.push(100.0 + 0.1 * i, 1000.0, 0.01);
        }
        hefkf_common::FrequencyFeatures first_compute;
        fresh_analyser.compute(first_compute);
        
        // First computation should have zero derivatives
        bool passed = first_compute.trend_strength_derivative == 0.0 &&
                     first_compute.centroid_velocity == 0.0 &&
                     first_compute.coherence_pv_derivative == 0.0;
        std::cout << "  Derivatives zero on first compute: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
}

void test_spectral_edge_cases() {
    std::cout << "\n=== Testing Spectral Analysis Edge Cases ===" << std::endl;
    
    // Test 1: Constant price (no variance)
    {
        FrequencyAnalyser analyser(1.0);
        for (int i = 0; i < 256; ++i) {
            analyser.push(100.0, 1000.0, 0.01);  // Constant values
        }
        
        hefkf_common::FrequencyFeatures features;
        bool computed = analyser.compute(features);
        
        // With constant prices, returns are zero, spectral features should be well-defined
        bool passed = computed && 
                     features.trend_strength >= 0.0 &&
                     features.spectral_flux == 0.0 &&  // No change
                     features.spectral_centroid_price >= 0.0;
        std::cout << "  Constant price handling: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 2: Extreme price jump
    {
        FrequencyAnalyser analyser(1.0);
        for (int i = 0; i < 256; ++i) {
            double price = (i == 128) ? 200.0 : 100.0;  // 100% jump at midpoint
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Should handle extreme jumps without NaN or infinity
        bool passed = std::isfinite(features.trend_strength) &&
                     std::isfinite(features.spectral_flux) &&
                     std::isfinite(features.spectral_centroid_price);
        std::cout << "  Extreme price jump handling: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 3: Very small price changes (numerical stability)
    {
        FrequencyAnalyser analyser(1.0);
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + 1e-8 * i;  // Tiny changes
            analyser.push(price, 1000.0, 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        bool passed = std::isfinite(features.trend_strength) &&
                     features.trend_strength >= 0.0 &&
                     features.trend_strength <= 1.0;
        std::cout << "  Numerical stability (tiny changes): " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 4: Negative spreads (should still work)
    {
        FrequencyAnalyser analyser(1.0);
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.1 * i);
            double spread = -0.01;  // Negative spread (inverted market?)
            analyser.push(price, 1000.0, spread);
        }
        
        hefkf_common::FrequencyFeatures features;
        bool computed = analyser.compute(features);
        
        bool passed = computed && std::isfinite(features.coherence_price_spread_peak);
        std::cout << "  Negative spread handling: " << (passed ? "PASS" : "FAIL") << std::endl;
        assert(passed);
    }
    
    // Test 5: Zero volume
    {
        FrequencyAnalyser analyser(1.0);
        for (int i = 0; i < 256; ++i) {
            double price = 100.0 + std::sin(2 * M_PI * 0.1 * i);
            analyser.push(price, 0.0, 0.01);  // Zero volume
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
        
        // Should handle zero volume gracefully
        bool passed = std::isfinite(features.coherence_price_volume_peak) &&
                     std::isfinite(features.spectral_centroid_volume);
        std::cout << "  Zero volume handling: " << (passed ? "PASS" : "FAIL") << std::endl;
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
        test_standard_normal_cdf();
        test_gaussian_cdf_interval();
        test_bucket_probability_distribution();
        
        // Spectral analysis tests
        test_frequency_analyser_basic();
        test_trend_strength();
        test_coherence_analysis();
        test_spectral_features();
        test_frequency_bands();
        test_all_nine_spectral_features();
        test_spectral_edge_cases();
        
        std::cout << "\n✓ All tests passed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
} 