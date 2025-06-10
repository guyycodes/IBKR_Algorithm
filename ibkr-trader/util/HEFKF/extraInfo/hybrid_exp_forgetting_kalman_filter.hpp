// Hybrid Exponential Forgetting Kalman Filter
// This file contains the implementation of the Hybrid Exponential Forgetting Kalman Filter
// It is a Kalman filter that uses an exponential forgetting factor to weight the importance of past observations


#ifndef HYBRID_EXP_FORGETTING_KALMAN_FILTER_HPP
#define HYBRID_EXP_FORGETTING_KALMAN_FILTER_HPP

#include <chrono>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <Eigen/Dense>

namespace hefkf {

// ─────────────────────── Configuration Structure ───────────────────────────
struct HKFConfig {
    // Existing hybrid filter parameters
    double time_domain_weight = 0.7;        // KF vs. raw observation
    double frequency_domain_weight = 0.3;    // nudging strength
    bool adaptive_noise = true;              // scale R/Q on-the-fly
    bool preserve_breakouts = true;          // placeholder for future logic
    double bucket_weight = 0.5;              // 0=no use, 1=full use
    
    // New exponential forgetting parameters
    double lambda_fixed = 0.99;              // forgetting factor (100-tick memory)
    bool lambda_adaptive = true;             // switch on/off adaptive λ
    double lambda_min = 0.95;                // minimum λ (50-tick equivalent)
    double lambda_max = 0.995;               // maximum λ (200-tick equivalent)
    double vol_threshold = 0.002;            // 0.2% 1-sec volatility threshold
};

// ─────────────────────── Bucket Confidence Structure ───────────────────────
struct BucketConfidence {
    double up_001_002 = 0.0;
    double up_002_005 = 0.0;
    double up_005_010 = 0.0;
    double up_010_plus = 0.0;
    double dn_001_002 = 0.0;
    double dn_002_005 = 0.0;
    double dn_005_010 = 0.0;
    double dn_010_plus = 0.0;
    
    // Normalize probabilities to sum to 1.0
    void normalize();
    
    // Check if bucket data is valid
    bool is_valid() const;
};

// ─────────────────────── Frequency Features Structure ───────────────────────
struct FrequencyFeatures {
    double trend_strength = 0.0;
    double coherence_price_volume_peak = 0.0;
    double coherence_price_spread_peak = 0.0;
    
    // Coherence by frequency band
    std::unordered_map<std::string, double> coherence_price_volume_by_band;
    std::unordered_map<std::string, double> coherence_price_spread_by_band;
    
    // Default constructor initializes band maps
    FrequencyFeatures();
};

// ─────────────────────── Market Data Structure ───────────────────────────
struct MarketData {
    double price = 0.0;
    double volume = 0.0;
    double spread = 0.0;
    std::chrono::system_clock::time_point timestamp;
    
    // Optional bucket confidence data
    std::unique_ptr<BucketConfidence> bucket_conf;
    
    // Optional frequency features
    std::unique_ptr<FrequencyFeatures> freq_features;
};

// ─────────────────────── Filter Output Structure ───────────────────────────
struct FilterOutput {
    double price_smoothed = 0.0;
    double price_velocity = 0.0;
    double volume_denoised = 0.0;
    double spread_filtered = 0.0;
    double lambda_used = 0.0;
    std::chrono::system_clock::time_point timestamp;
};

// ─────────────────────── Adaptive Lambda Helper ───────────────────────────
class AdaptiveLambda {
public:
    static double compute_lambda(double volatility, 
                               double base_lambda = 0.99,
                               double lambda_min = 0.95,
                               double lambda_max = 0.995,
                               double vol_threshold = 0.002);
};

// ─────────────────────── Bucket Expectation Helper ───────────────────────────
class BucketExpectation {
public:
    struct ReturnStats {
        double mean_return = 0.0;
        double variance = 0.0;
    };
    
    static ReturnStats compute_expectation(const BucketConfidence& conf);
    
private:
    // Bucket specifications: (mean_return, variance)
    static const std::unordered_map<std::string, std::pair<double, double>> bucket_specs_;
};

// ─────────────────────── Main Filter Class ───────────────────────────────
class HybridExpForgettingKalmanFilter {
public:
    // Constructor
    explicit HybridExpForgettingKalmanFilter(const HKFConfig& config = HKFConfig{});
    
    // Destructor
    ~HybridExpForgettingKalmanFilter() = default;
    
    // Initialize filter with first measurement
    void initialize(const MarketData& initial_data, double dt = 1.0);
    
    // Process single market data point
    FilterOutput process(const MarketData& data);
    
    // Reset filter state
    void reset();
    
    // Get/Set configuration
    const HKFConfig& get_config() const { return config_; }
    void set_config(const HKFConfig& config);
    
    // Validate configuration parameters
    static void validate_config(const HKFConfig& config);
    
    // Get current state for debugging
    const Eigen::VectorXd& get_state() const { return x_; }
    const Eigen::MatrixXd& get_covariance() const { return P_; }
    
    // Check if filter is initialized
    bool is_initialized() const { return initialized_; }

private:
    // Configuration
    HKFConfig config_;
    
    // Filter state
    Eigen::VectorXd x_;           // State vector [price, velocity, volume, spread]
    Eigen::MatrixXd P_;           // Covariance matrix
    Eigen::MatrixXd F_;           // Transition matrix
    Eigen::MatrixXd H_;           // Observation matrix
    Eigen::MatrixXd B_;           // Control matrix
    Eigen::MatrixXd Q_;           // Process noise covariance
    Eigen::MatrixXd R_;           // Measurement noise covariance
    Eigen::MatrixXd Q_base_;      // Base process noise (for scaling)
    Eigen::MatrixXd R_base_;      // Base measurement noise (for scaling)
    Eigen::MatrixXd K_;           // Kalman gain (stored for Joseph form)
    
    // State tracking
    bool initialized_ = false;
    double dt_ = 1.0;             // Time step
    double last_price_ = 0.0;     // For velocity calculation
    double scale_prev_ = 1.0;     // Cached noise scale factor
    
    // Performance optimization constants
    static constexpr double SCALE_UPDATE_THRESHOLD = 0.05;  // 5% change threshold
    
    // Internal methods
    void setup_matrices(double dt);
    void update_noise(double market_volatility);
    void predict_with_forgetting(double lambda, double control_input, double extra_noise);
    void update_measurement(const Eigen::VectorXd& measurement);
    void apply_frequency_nudging(const FrequencyFeatures& freq_features, double price_velocity);
    
    // Utility methods
    double compute_control_input(const BucketConfidence& bucket_conf, double price, double& extra_noise);
    double compute_market_volatility(double price_velocity, double current_price);
};

// ─────────────────────── Utility Functions ───────────────────────────────
namespace utils {
    // Convert timestamp to seconds since epoch
    double timestamp_to_seconds(const std::chrono::system_clock::time_point& tp);
    
    // Create default frequency features
    FrequencyFeatures create_default_frequency_features();
    
    // Validate market data
    bool validate_market_data(const MarketData& data);
}

} // namespace hefkf

#endif // HYBRID_EXP_FORGETTING_KALMAN_FILTER_HPP
