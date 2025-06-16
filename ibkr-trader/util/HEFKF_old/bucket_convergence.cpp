// Bucket Convergence Tracking Implementation
// Implements Kalman filter-based tracking of bucket probability evolution
//
// ============================================================================
// CONVERGENCE TUNING GUIDE
// ============================================================================
// To adjust convergence behavior, modify the values in ConvergenceCriteria
// located in bucket_convergence.hpp. Here's what each setting controls:
//
// For FASTER convergence detection (less stringent):
//   - Increase MAX_AVG_ENTROPY (e.g., 1.0 → 1.5)
//   - Decrease MIN_STABILITY_SCORE (e.g., 0.8 → 0.7)
//   - Decrease MIN_BIAS_CONSISTENCY (e.g., 0.7 → 0.5)
//   - Decrease MIN_TICKS_FOR_CONVERGENCE (e.g., 20 → 10)
//
// For SLOWER convergence detection (more stringent):
//   - Decrease MAX_AVG_ENTROPY (e.g., 1.0 → 0.7)
//   - Increase MIN_STABILITY_SCORE (e.g., 0.8 → 0.9)
//   - Increase MIN_BIAS_CONSISTENCY (e.g., 0.7 → 0.85)
//   - Increase MIN_TICKS_FOR_CONVERGENCE (e.g., 20 → 50)
//
// TARGET_ENTROPY controls when the system considers entropy "low enough"
// for convergence time estimation. Should be < MAX_AVG_ENTROPY.
// ============================================================================

#include "bucket_convergence.hpp"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <stdexcept>

namespace bucket_convergence {

// ─────────────────────── BucketMetrics Implementation ───────────────────────
void BucketMetrics::compute_from_bucket(const hefkf_common::BucketConfidence& bucket) {
    // Compute entropy
    bucket_entropy = utils::compute_entropy(bucket);
    
    // Compute directional bias
    directional_bias = utils::compute_directional_bias(bucket);
    
    // Compute extreme probability
    extreme_probability = utils::compute_extreme_probability(bucket);
    
    // Confidence score based on entropy (lower entropy = higher confidence)
    // Map entropy [0, log(8)] to confidence [1, 0]
    double max_entropy = std::log(8.0);  // Maximum entropy for 8 buckets
    confidence_score = 1.0 - (bucket_entropy / max_entropy);
}

void BucketMetrics::print(const std::string& label) const {
    if (!label.empty()) {
        std::cout << label << ": ";
    }
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Entropy=" << bucket_entropy 
              << ", Stability=" << bucket_stability
              << ", Bias=" << std::showpos << directional_bias << std::noshowpos
              << ", Extreme=" << extreme_probability
              << ", Confidence=" << confidence_score;
    
    if (calibration_error > 0) {
        std::cout << ", CalibErr=" << calibration_error;
    }
    std::cout << std::endl;
}

// ─────────────────────── BucketStateKalman Implementation ───────────────────────
BucketStateKalman::BucketStateKalman(double dt) : dt_(dt) {
    // Initialize state transition matrix
    // Simple model: state evolves with some drift and noise
    F_ = Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity();
    F_(0, 0) = 0.99;  // Entropy slowly decays
    F_(1, 1) = 0.98;  // Directional bias persists
    F_(2, 2) = 0.95;  // Stability changes more quickly
    F_(3, 3) = 0.97;  // Extreme probability moderately persistent
    
    // Measurement matrix (direct observation)
    H_ = Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity();
    
    // Process noise covariance
    Q_ = Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity();
    Q_(0, 0) = 0.001;  // Entropy process noise
    Q_(1, 1) = 0.005;  // Bias can change more
    Q_(2, 2) = 0.01;   // Stability has higher noise
    Q_(3, 3) = 0.003;  // Extreme probability noise
    
    // Measurement noise covariance
    R_ = Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity();
    R_(0, 0) = 0.01;   // Entropy measurement noise
    R_(1, 1) = 0.02;   // Bias measurement noise
    R_(2, 2) = 0.05;   // Stability measurement is noisier
    R_(3, 3) = 0.02;   // Extreme probability measurement noise
    
    // Initialize state and covariance
    x_ = Eigen::Vector<double, STATE_DIM>::Zero();
    P_ = Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity();
}

void BucketStateKalman::initialize(const BucketMetrics& initial_metrics) {
    x_ = metrics_to_state(initial_metrics);
    P_ = Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity() * 0.1;
    initialized_ = true;
}

BucketMetrics BucketStateKalman::process(const BucketMetrics& observed_metrics) {
    if (!initialized_) {
        initialize(observed_metrics);
        return observed_metrics;
    }
    
    // Predict step
    x_ = F_ * x_;
    P_ = F_ * P_ * F_.transpose() + Q_;
    
    // Update step
    Eigen::Vector<double, STATE_DIM> z = metrics_to_state(observed_metrics);
    Eigen::Vector<double, STATE_DIM> y = z - H_ * x_;  // Innovation
    Eigen::Matrix<double, STATE_DIM, STATE_DIM> S = H_ * P_ * H_.transpose() + R_;  // Innovation covariance
    Eigen::Matrix<double, STATE_DIM, STATE_DIM> K = P_ * H_.transpose() * S.inverse();  // Kalman gain
    
    x_ = x_ + K * y;
    P_ = (Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity() - K * H_) * P_;
    
    // Ensure state remains in valid ranges
    x_(0) = std::max(0.0, x_(0));  // Entropy >= 0
    x_(1) = std::clamp(x_(1), -1.0, 1.0);  // Bias in [-1, 1]
    x_(2) = std::clamp(x_(2), 0.0, 1.0);   // Stability in [0, 1]
    x_(3) = std::clamp(x_(3), 0.0, 1.0);   // Extreme prob in [0, 1]
    
    return state_to_metrics();
}

BucketMetrics BucketStateKalman::get_filtered_state() const {
    return state_to_metrics();
}

void BucketStateKalman::reset() {
    x_ = Eigen::Vector<double, STATE_DIM>::Zero();
    P_ = Eigen::Matrix<double, STATE_DIM, STATE_DIM>::Identity();
    initialized_ = false;
}

BucketMetrics BucketStateKalman::state_to_metrics() const {
    BucketMetrics metrics;
    metrics.bucket_entropy = x_(0);
    metrics.directional_bias = x_(1);
    metrics.bucket_stability = x_(2);
    metrics.extreme_probability = x_(3);
    
    // Derive confidence from entropy
    double max_entropy = std::log(8.0);
    metrics.confidence_score = 1.0 - std::min(x_(0) / max_entropy, 1.0);
    
    return metrics;
}

Eigen::Vector<double, BucketStateKalman::STATE_DIM> 
BucketStateKalman::metrics_to_state(const BucketMetrics& metrics) const {
    Eigen::Vector<double, STATE_DIM> state;
    state(0) = metrics.bucket_entropy;
    state(1) = metrics.directional_bias;
    state(2) = metrics.bucket_stability;
    state(3) = metrics.extreme_probability;
    return state;
}

// ─────────────────────── ConvergenceTracker Implementation ───────────────────────
ConvergenceTracker::ConvergenceTracker(size_t history_size) 
    : max_history_size_(history_size), kalman_filter_(1.0) {
}

BucketMetrics ConvergenceTracker::track(const hefkf_common::BucketConfidence& bucket,
                                       double actual_price_change,
                                       bool update_calibration) {
    // Compute raw metrics
    BucketMetrics raw_metrics;
    raw_metrics.compute_from_bucket(bucket);
    
    // Compute stability if we have history
    if (!bucket_history_.empty()) {
        double kl_div = compute_kl_divergence(bucket_history_.back(), bucket);
        // Convert KL divergence to stability score (lower KL = higher stability)
        // KL of 0.1 maps to stability ~0.9, KL of 1.0 maps to stability ~0.37
        raw_metrics.bucket_stability = std::exp(-kl_div);
    } else {
        raw_metrics.bucket_stability = 0.5;  // Neutral initial value
    }
    
    // Update calibration if requested
    if (update_calibration && !bucket_history_.empty()) {
        CalibrationData cal_data;
        cal_data.predicted = bucket_history_.back();
        cal_data.actual_return = actual_price_change;
        calibration_history_.push_back(cal_data);
        
        if (calibration_history_.size() > max_history_size_) {
            calibration_history_.pop_front();
        }
        
        raw_metrics.calibration_error = compute_calibration_error();
    }
    
    // Process through Kalman filter for smooth tracking
    BucketMetrics filtered_metrics = kalman_filter_.process(raw_metrics);
    
    // Update incremental statistics
    tick_count_++;
    
    // Add point to entropy regression (x = tick number, y = entropy)
    entropy_regression_.add_point(static_cast<double>(tick_count_), filtered_metrics.bucket_entropy);
    
    // Update running sums for average calculations
    sum_entropy_ += filtered_metrics.bucket_entropy;
    sum_stability_ += filtered_metrics.bucket_stability;
    sum_bias_ += filtered_metrics.directional_bias;
    sum_bias_squared_ += filtered_metrics.directional_bias * filtered_metrics.directional_bias;
    
    // Check for convergence crossing
    if (convergence_tick_ == 0 && last_entropy_ > ConvergenceCriteria::TARGET_ENTROPY && 
        filtered_metrics.bucket_entropy <= ConvergenceCriteria::TARGET_ENTROPY) {
        convergence_tick_ = tick_count_;
    }
    last_entropy_ = filtered_metrics.bucket_entropy;
    
    // Update bucket history (keep only recent for stability calculation)
    bucket_history_.push_back(bucket);
    if (bucket_history_.size() > 10) {  // Only need recent buckets for stability
        bucket_history_.pop_front();
    }
    
    // Keep limited metrics history for recent analysis
    metrics_history_.push_back(filtered_metrics);
    if (metrics_history_.size() > 50) {  // Keep last 50 for recent checks
        metrics_history_.pop_front();
    }
    
    return filtered_metrics;
}

ConvergenceTracker::ConvergenceStats ConvergenceTracker::get_convergence_stats() const {
    ConvergenceStats stats{};
    stats.converged_at_tick = 0;  // Initialize to 0 (not converged)
    
    if (tick_count_ == 0) {
        return stats;
    }
    
    // Calculate averages from running sums
    stats.avg_entropy = sum_entropy_ / tick_count_;
    stats.stability_score = sum_stability_ / tick_count_;
    
    // Bias consistency: low variance of bias = high consistency
    double bias_mean = sum_bias_ / tick_count_;
    double bias_variance = (sum_bias_squared_ / tick_count_) - (bias_mean * bias_mean);
    stats.bias_consistency = 1.0 - std::sqrt(std::max(0.0, bias_variance));
    
    // Get entropy trend from incremental regression
    if (entropy_regression_.size() >= 10) {
        stats.entropy_trend = entropy_regression_.get_slope();
        
        // Estimate time to convergence using current entropy and trend
        double current_entropy = last_entropy_;
        double target_entropy = ConvergenceCriteria::TARGET_ENTROPY;
        
        if (current_entropy <= target_entropy) {
            stats.time_to_convergence = 0.0;  // Already converged
            stats.converged_at_tick = convergence_tick_ > 0 ? convergence_tick_ : tick_count_;
        } else if (stats.entropy_trend < -0.0001) {  // Converging
            double ticks_to_target = (target_entropy - current_entropy) / stats.entropy_trend;
            stats.time_to_convergence = std::max(0.0, ticks_to_target);
            stats.converged_at_tick = 0;  // Not yet converged
        } else {
            stats.time_to_convergence = 999.0;  // Not converging
            stats.converged_at_tick = 0;
        }
    } else {
        // Not enough data points
        stats.entropy_trend = 0.0;
        stats.time_to_convergence = 999.0;
        stats.converged_at_tick = 0;
    }
    
    // Check convergence criteria using configurable thresholds
    stats.is_converged = (stats.avg_entropy < ConvergenceCriteria::MAX_AVG_ENTROPY && 
                         stats.stability_score > ConvergenceCriteria::MIN_STABILITY_SCORE &&
                         stats.bias_consistency > ConvergenceCriteria::MIN_BIAS_CONSISTENCY);
    
    // Additional check using recent metrics (if available)
    if (!metrics_history_.empty() && tick_count_ >= ConvergenceCriteria::MIN_TICKS_FOR_CONVERGENCE) {
        double recent_entropy = metrics_history_.back().bucket_entropy;
        double recent_stability = metrics_history_.back().bucket_stability;
        
        // More stringent convergence check based on recent values
        bool recently_converged = (recent_entropy < ConvergenceCriteria::MAX_RECENT_ENTROPY && 
                                  recent_stability > ConvergenceCriteria::MIN_RECENT_STABILITY);
        
        if (recently_converged) {
            stats.is_converged = true;
            if (stats.converged_at_tick == 0 && convergence_tick_ > 0) {
                stats.converged_at_tick = convergence_tick_;
            }
        }
    }
    
    return stats;
}

bool ConvergenceTracker::is_converged_uptrend() const {
    if (metrics_history_.size() < ConvergenceCriteria::MIN_TICKS_FOR_CONVERGENCE) return false;
    
    const auto& recent_metrics = metrics_history_.back();
    auto stats = get_convergence_stats();
    
    return stats.is_converged && 
           recent_metrics.directional_bias > 0.5 &&
           stats.bias_consistency > 0.8;
}

bool ConvergenceTracker::is_converged_downtrend() const {
    if (metrics_history_.size() < ConvergenceCriteria::MIN_TICKS_FOR_CONVERGENCE) return false;
    
    const auto& recent_metrics = metrics_history_.back();
    auto stats = get_convergence_stats();
    
    return stats.is_converged && 
           recent_metrics.directional_bias < -0.5 &&
           stats.bias_consistency > 0.8;
}

bool ConvergenceTracker::is_converged_sideways() const {
    if (metrics_history_.size() < ConvergenceCriteria::MIN_TICKS_FOR_CONVERGENCE) return false;
    
    const auto& recent_metrics = metrics_history_.back();
    auto stats = get_convergence_stats();
    
    return stats.is_converged && 
           std::abs(recent_metrics.directional_bias) < 0.2 &&
           recent_metrics.extreme_probability < 0.1;
}

BucketMetrics ConvergenceTracker::get_filtered_metrics() const {
    if (!kalman_filter_.is_initialized()) {
        return BucketMetrics{};
    }
    return kalman_filter_.get_filtered_state();
}

void ConvergenceTracker::reset() {
    bucket_history_.clear();
    metrics_history_.clear();
    calibration_history_.clear();
    kalman_filter_.reset();
    
    // Reset incremental statistics
    entropy_regression_.reset();
    sum_entropy_ = 0.0;
    sum_stability_ = 0.0;
    sum_bias_ = 0.0;
    sum_bias_squared_ = 0.0;
    tick_count_ = 0;
    convergence_tick_ = 0;
    last_entropy_ = 0.0;
}

double ConvergenceTracker::compute_kl_divergence(const hefkf_common::BucketConfidence& p,
                                                const hefkf_common::BucketConfidence& q) const {
    // KL(P||Q) = Σ p_i * log(p_i / q_i)
    double kl = 0.0;
    const double epsilon = 1e-10;  // Prevent log(0)
    
    auto kl_term = [epsilon](double pi, double qi) {
        if (pi < epsilon) return 0.0;
        qi = std::max(qi, epsilon);
        return pi * std::log(pi / qi);
    };
    
    kl += kl_term(p.up_001_002, q.up_001_002);
    kl += kl_term(p.up_002_005, q.up_002_005);
    kl += kl_term(p.up_005_010, q.up_005_010);
    kl += kl_term(p.up_010_plus, q.up_010_plus);
    kl += kl_term(p.dn_001_002, q.dn_001_002);
    kl += kl_term(p.dn_002_005, q.dn_002_005);
    kl += kl_term(p.dn_005_010, q.dn_005_010);
    kl += kl_term(p.dn_010_plus, q.dn_010_plus);
    
    return kl;
}

double ConvergenceTracker::compute_calibration_error() const {
    if (calibration_history_.size() < 10) return 0.0;
    
    // Group outcomes by predicted bucket and compute calibration
    // This is simplified - a full implementation would bin by probability ranges
    double total_error = 0.0;
    int count = 0;
    
    for (const auto& cal_data : calibration_history_) {
        double expected_return = utils::compute_expected_return(cal_data.predicted);
        double error = std::abs(expected_return - cal_data.actual_return);
        total_error += error;
        count++;
    }
    
    return count > 0 ? total_error / count : 0.0;
}

void ConvergenceTracker::ConvergenceStats::print() const {
    std::cout << "=== Convergence Statistics ===" << std::endl;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Average Entropy: " << avg_entropy << std::endl;
    std::cout << std::setprecision(6);  // More precision for entropy trend
    std::cout << "Entropy Trend: " << entropy_trend << " (negative = converging)" << std::endl;
    std::cout << std::setprecision(3);  // Back to 3 for other values
    std::cout << "Stability Score: " << stability_score << std::endl;
    std::cout << "Bias Consistency: " << bias_consistency << std::endl;
    std::cout << "Time to Convergence: " << time_to_convergence << " ticks" << std::endl;
    std::cout << "Is Converged: " << (is_converged ? "YES" : "NO") << std::endl;
    if (converged_at_tick > 0) {
        std::cout << "Converged at Tick: " << converged_at_tick << std::endl;
    }
}

// ─────────────────────── Utility Functions Implementation ───────────────────────
namespace utils {

double compute_entropy(const hefkf_common::BucketConfidence& bucket) {
    double entropy = 0.0;
    const double epsilon = 1e-10;
    
    auto add_entropy = [&entropy, epsilon](double p) {
        if (p > epsilon) {
            entropy -= p * std::log(p);
        }
    };
    
    add_entropy(bucket.up_001_002);
    add_entropy(bucket.up_002_005);
    add_entropy(bucket.up_005_010);
    add_entropy(bucket.up_010_plus);
    add_entropy(bucket.dn_001_002);
    add_entropy(bucket.dn_002_005);
    add_entropy(bucket.dn_005_010);
    add_entropy(bucket.dn_010_plus);
    
    return entropy;
}

double compute_directional_bias(const hefkf_common::BucketConfidence& bucket) {
    double up_total = bucket.up_001_002 + bucket.up_002_005 + 
                     bucket.up_005_010 + bucket.up_010_plus;
    double down_total = bucket.dn_001_002 + bucket.dn_002_005 + 
                       bucket.dn_005_010 + bucket.dn_010_plus;
    
    // Map [0,1] to [-1,1]
    return up_total - down_total;
}

double compute_extreme_probability(const hefkf_common::BucketConfidence& bucket) {
    return bucket.up_010_plus + bucket.dn_010_plus;
}

bool has_directional_conviction(const hefkf_common::BucketConfidence& bucket,
                               double threshold) {
    double bias = std::abs(compute_directional_bias(bucket));
    return bias > threshold;
}

double compute_expected_return(const hefkf_common::BucketConfidence& bucket) {
    // Updated to 10x more granular for 1-5 minute horizons
    return bucket.up_001_002 * 0.00015 +    // 0.015% (1.5bp)
           bucket.up_002_005 * 0.00035 +    // 0.035% (3.5bp)
           bucket.up_005_010 * 0.00075 +    // 0.075% (7.5bp)
           bucket.up_010_plus * 0.00150 +   // 0.150% (15bp)
           bucket.dn_001_002 * (-0.00015) + // -0.015% (-1.5bp)
           bucket.dn_002_005 * (-0.00035) + // -0.035% (-3.5bp)
           bucket.dn_005_010 * (-0.00075) + // -0.075% (-7.5bp)
           bucket.dn_010_plus * (-0.00150);  // -0.150% (-15bp)
}

} // namespace utils
} // namespace bucket_convergence 