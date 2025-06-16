// Bucket Convergence Tracking for HEFKF Pipeline
// Replaces velocity tracking with more meaningful end-to-end metrics
// Tracks stability, entropy, directional bias, and calibration

#ifndef BUCKET_CONVERGENCE_HPP
#define BUCKET_CONVERGENCE_HPP

#include "posterior.hpp"
#include "incremental_regression.hpp"
#include <vector>
#include <cmath>
#include <deque>
#include <Eigen/Dense>
#include <iostream>

namespace bucket_convergence {

// ============================================================================
// CONVERGENCE CRITERIA CONFIGURATION
// Adjust these values to tune when the system declares convergence
// ============================================================================
struct ConvergenceCriteria {
    // ENTROPY THRESHOLD
    // Average entropy must be below this value to consider the filter confident
    // Range: [0, log(8)≈2.08]. Lower = more stringent
    // - 0.5: Very confident (probabilities highly concentrated)
    // - 1.0: Moderately confident (current default)
    // - 1.5: Less stringent (allows more uncertainty)
    static constexpr double MAX_AVG_ENTROPY = 1.0;
    
    // STABILITY THRESHOLD  
    // How stable predictions must be over time (based on KL divergence)
    // Range: [0, 1]. Higher = more stringent
    // - 0.9: Very stable (predictions barely change)
    // - 0.8: Stable (current default)
    // - 0.7: Moderately stable (allows more variation)
    static constexpr double MIN_STABILITY_SCORE = 0.8;
    
    // BIAS CONSISTENCY THRESHOLD
    // How consistent the directional bias (up/down) must be
    // Range: [0, 1]. Higher = more stringent
    // - 0.9: Very consistent direction (rarely changes)
    // - 0.7: Consistent (current default)
    // - 0.5: Allows some directional uncertainty
    static constexpr double MIN_BIAS_CONSISTENCY = 0.7;
    
    // RECENT CONVERGENCE CRITERIA (Alternative stricter check)
    // Used for checking recent values instead of averages
    static constexpr double MAX_RECENT_ENTROPY = 0.5;      // Stricter than average
    static constexpr double MIN_RECENT_STABILITY = 0.85;   // Stricter than average
    
    // TARGET ENTROPY FOR TIME ESTIMATION
    // Used to estimate how many ticks until convergence
    // Should be lower than MAX_AVG_ENTROPY
    static constexpr double TARGET_ENTROPY = 0.3;
    
    // MINIMUM DATA POINTS
    // Minimum number of observations before checking convergence
    static constexpr size_t MIN_TICKS_FOR_CONVERGENCE = 20;
    
    // Print current settings for debugging/tuning
    static void print_settings() {
        std::cout << "\n=== Current Convergence Criteria ===" << std::endl;
        std::cout << "MAX_AVG_ENTROPY: " << MAX_AVG_ENTROPY 
                  << " (Filter confidence threshold)" << std::endl;
        std::cout << "MIN_STABILITY_SCORE: " << MIN_STABILITY_SCORE 
                  << " (Prediction stability threshold)" << std::endl;
        std::cout << "MIN_BIAS_CONSISTENCY: " << MIN_BIAS_CONSISTENCY 
                  << " (Directional consistency threshold)" << std::endl;
        std::cout << "MAX_RECENT_ENTROPY: " << MAX_RECENT_ENTROPY 
                  << " (Recent confidence check)" << std::endl;
        std::cout << "MIN_RECENT_STABILITY: " << MIN_RECENT_STABILITY 
                  << " (Recent stability check)" << std::endl;
        std::cout << "TARGET_ENTROPY: " << TARGET_ENTROPY 
                  << " (Convergence target)" << std::endl;
        std::cout << "MIN_TICKS_FOR_CONVERGENCE: " << MIN_TICKS_FOR_CONVERGENCE 
                  << " (Minimum observations)" << std::endl;
        std::cout << "====================================\n" << std::endl;
    }
};

// Bucket probability distribution confidence tracking

// ─────────────────────── Bucket Metrics Structure ───────────────────────
struct BucketMetrics {
    // Entropy: Lower = more confident
    // H = -Σ p_i * log(p_i) for all 8 buckets
    double bucket_entropy = 0.0;
    
    // Stability: How much buckets change tick-to-tick (0=unstable, 1=stable)
    // Uses exponential moving average of KL divergence
    double bucket_stability = 0.0;
    
    // Directional bias: +1 = all up, -1 = all down, 0 = balanced
    double directional_bias = 0.0;
    
    // Concentration in extreme buckets (up_010_plus + dn_010_plus)
    double extreme_probability = 0.0;
    
    // Additional metrics for comprehensive tracking
    double confidence_score = 0.0;     // Overall confidence in predictions
    double calibration_error = 0.0;    // How well predicted probabilities match outcomes
    double regime_consistency = 0.0;   // How consistently we detect the same regime
    
    // Compute all metrics from bucket confidence
    void compute_from_bucket(const hefkf_common::BucketConfidence& bucket);
    
    // Pretty print metrics
    void print(const std::string& label = "") const;
};

// ─────────────────────── Kalman Filter for Bucket State ───────────────────────
class BucketStateKalman {
public:
    // State vector: [entropy, directional_bias, stability, extreme_prob]
    static constexpr int STATE_DIM = 4;
    
    BucketStateKalman(double dt = 1.0);
    
    // Initialize with initial bucket metrics
    void initialize(const BucketMetrics& initial_metrics);
    
    // Process new bucket metrics and return filtered estimate
    BucketMetrics process(const BucketMetrics& observed_metrics);
    
    // Get current filtered state
    BucketMetrics get_filtered_state() const;
    
    // Reset filter
    void reset();
    
    // Check if initialized
    bool is_initialized() const { return initialized_; }
    
private:
    // State transition matrix (4x4)
    Eigen::Matrix<double, STATE_DIM, STATE_DIM> F_;
    
    // Measurement matrix (4x4 identity)
    Eigen::Matrix<double, STATE_DIM, STATE_DIM> H_;
    
    // Process noise covariance
    Eigen::Matrix<double, STATE_DIM, STATE_DIM> Q_;
    
    // Measurement noise covariance
    Eigen::Matrix<double, STATE_DIM, STATE_DIM> R_;
    
    // State estimate
    Eigen::Vector<double, STATE_DIM> x_;
    
    // Error covariance
    Eigen::Matrix<double, STATE_DIM, STATE_DIM> P_;
    
    // Time step
    double dt_;
    
    // Initialization flag
    bool initialized_ = false;
    
    // Convert state vector to BucketMetrics
    BucketMetrics state_to_metrics() const;
    
    // Convert BucketMetrics to state vector
    Eigen::Vector<double, STATE_DIM> metrics_to_state(const BucketMetrics& metrics) const;
};

// ─────────────────────── Convergence Tracker ───────────────────────
class ConvergenceTracker {
public:
    ConvergenceTracker(size_t history_size = 100);
    
    // Track new bucket confidence and return metrics
    BucketMetrics track(const hefkf_common::BucketConfidence& bucket, 
                       double actual_price_change = 0.0,
                       bool update_calibration = false);
    
    // Get convergence statistics
    struct ConvergenceStats {
        double avg_entropy;           // Average entropy over window
        double entropy_trend;         // Slope of entropy (negative = converging)
        double stability_score;       // Average stability
        double bias_consistency;      // How consistent is directional bias
        double time_to_convergence;   // Estimated ticks to convergence
        bool is_converged;           // True if metrics indicate convergence
        size_t converged_at_tick;    // Tick number when convergence was achieved (0 if not converged)
        
        void print() const;
    };
    
    ConvergenceStats get_convergence_stats() const;
    
    // Check specific convergence criteria
    bool is_converged_uptrend() const;
    bool is_converged_downtrend() const;
    bool is_converged_sideways() const;
    
    // Get filtered metrics using Kalman filter
    BucketMetrics get_filtered_metrics() const;
    
    // Reset tracker
    void reset();
    
private:
    // History of bucket confidences for stability calculation
    std::deque<hefkf_common::BucketConfidence> bucket_history_;
    
    // History of metrics for trend analysis (reduced size for recent samples only)
    std::deque<BucketMetrics> metrics_history_;
    
    // Calibration tracking
    struct CalibrationData {
        hefkf_common::BucketConfidence predicted;
        double actual_return;
    };
    std::deque<CalibrationData> calibration_history_;
    
    // Kalman filter for smooth metric tracking
    BucketStateKalman kalman_filter_;
    
    // Incremental regression for entropy trend (memory efficient)
    IncrementalLinearRegression entropy_regression_;
    
    // Running statistics (no need to store all history)
    double sum_entropy_ = 0.0;
    double sum_stability_ = 0.0;
    double sum_bias_ = 0.0;
    double sum_bias_squared_ = 0.0;
    size_t tick_count_ = 0;
    
    // Track when convergence was first detected
    size_t convergence_tick_ = 0;
    double last_entropy_ = 0.0;
    
    // Configuration
    size_t max_history_size_;
    
    // Helper functions
    double compute_kl_divergence(const hefkf_common::BucketConfidence& p,
                                 const hefkf_common::BucketConfidence& q) const;
    double compute_calibration_error() const;
};

// ─────────────────────── Utility Functions ───────────────────────
namespace utils {
    // Compute Shannon entropy of bucket distribution
    double compute_entropy(const hefkf_common::BucketConfidence& bucket);
    
    // Compute directional bias (-1 to +1)
    double compute_directional_bias(const hefkf_common::BucketConfidence& bucket);
    
    // Compute probability mass in extreme buckets
    double compute_extreme_probability(const hefkf_common::BucketConfidence& bucket);
    
    // Check if bucket indicates strong directional conviction
    bool has_directional_conviction(const hefkf_common::BucketConfidence& bucket,
                                   double threshold = 0.7);
    
    // Compute expected return from bucket distribution
    double compute_expected_return(const hefkf_common::BucketConfidence& bucket);
}

} // namespace bucket_convergence

#endif // BUCKET_CONVERGENCE_HPP 