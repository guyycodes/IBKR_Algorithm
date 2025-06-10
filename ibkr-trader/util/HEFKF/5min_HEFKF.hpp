// 5-Minute Hybrid Exponential Forgetting Kalman Filter
// Standalone implementation optimized for 5-minute horizon with smoother parameters
// Completely self-contained without dependencies on the original HEFKF

#ifndef FIVE_MIN_HEFKF_HPP
#define FIVE_MIN_HEFKF_HPP

#include <chrono>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <Eigen/Dense>

namespace hefkf_5min {

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

// ─────────────────────── 5-Minute Specialized Configuration ───────────────────────────
struct FiveMinHKFConfig {
    // 5-minute optimized parameters (hardcoded)
    // static constexpr double TIME_DOMAIN_WEIGHT = 0.65;
    // static constexpr double FREQUENCY_DOMAIN_WEIGHT = 0.35;
    // 5-minute optimized parameters (SMOOTH FOCUS)
    static constexpr double TIME_DOMAIN_WEIGHT = 0.6;      // Lower time domain for smoother response
    static constexpr double FREQUENCY_DOMAIN_WEIGHT = 0.4; // Higher frequency weight for trend analysis
    static constexpr bool ADAPTIVE_NOISE = true;
    // static constexpr bool PRESERVE_BREAKOUTS = true;
    // static constexpr double BUCKET_WEIGHT = 0.5;
    static constexpr bool PRESERVE_BREAKOUTS = false;      // Less aggressive breakout preservation
    static constexpr double BUCKET_WEIGHT = 0.4;          // Lower bucket weight for smoother transitions
    
    // Exponential forgetting parameters for 5min
    // static constexpr double LAMBDA_FIXED = 0.99;
    // Exponential forgetting parameters for 5min (SMOOTH)
    static constexpr double LAMBDA_FIXED = 0.995;      // Higher base forgetting for more smoothing
    static constexpr bool LAMBDA_ADAPTIVE = true;
    // static constexpr double LAMBDA_MIN = 0.94;    // Slightly lower for more smoothing
    // static constexpr double LAMBDA_MAX = 0.995;
    // static constexpr double VOL_THRESHOLD = 0.003;  // 0.3% 1-sec volatility threshold
    static constexpr double LAMBDA_MIN = 0.96;         // Much higher min for smoother response
    static constexpr double LAMBDA_MAX = 0.998;        // Higher max for strong smoothing
    static constexpr double VOL_THRESHOLD = 0.004;     // 0.4% - higher threshold for slower adaptation
    
    // Noise matrix scaling parameters for 5min
    // static constexpr double INITIAL_P_SCALE = 1.5;  // Larger initial covariance
    // static constexpr double R_PRICE = 0.15;         // Higher measurement noise
    // static constexpr double R_VOLUME = 12000.0;
    // static constexpr double R_SPREAD = 0.015;
    // static constexpr double Q_BASE_SCALE = 2e-4;    // Higher process noise
    // Noise matrix scaling parameters for 5min (HIGH NOISE = MORE SMOOTH)
    static constexpr double INITIAL_P_SCALE = 2.0;     // Higher initial uncertainty
    static constexpr double R_PRICE = 0.25;            // MUCH higher measurement noise (trust measurements less)
    static constexpr double R_VOLUME = 15000.0;        // Higher volume noise
    static constexpr double R_SPREAD = 0.025;          // Higher spread noise
    static constexpr double Q_BASE_SCALE = 3e-4;       // Higher process noise base
    
    // Adaptive noise scaling for 5min
    // static constexpr double SCALE_MIN = 0.4;        // Wider scale range
    // static constexpr double SCALE_MAX = 6.0;
    // static constexpr double Q_MULTIPLIER = 0.15;    // Higher process noise scaling
    // Adaptive noise scaling for 5min (CONSERVATIVE SCALING)
    static constexpr double SCALE_MIN = 0.6;           // Higher min to maintain smoothness
    static constexpr double SCALE_MAX = 8.0;           // Allow higher scaling for strong smoothing
    static constexpr double Q_MULTIPLIER = 0.2;        // Higher process noise multiplier for smoothness
};

// ─────────────────────── 5-Minute Specialized Filter Class ───────────────────────────
class FiveMinuteHEFKF {
public:
    // Constructor
    FiveMinuteHEFKF();
    
    // Destructor
    ~FiveMinuteHEFKF() = default;
    
    // Initialize filter with first measurement
    void initialize(const MarketData& initial_data, double dt = 1.0);
    
    // Process single market data point
    FilterOutput process(const MarketData& data);
    
    // Reset filter state
    void reset();
    
    // Get current state for debugging
    const Eigen::VectorXd& get_state() const { return x_; }
    const Eigen::MatrixXd& get_covariance() const { return P_; }
    
    // Check if filter is initialized
    bool is_initialized() const { return initialized_; }
    
    // Get configuration info
    static FiveMinHKFConfig get_config_info() { return FiveMinHKFConfig{}; }

private:
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
    
    // Internal methods (5min-specific implementations)
    void setup_matrices(double dt);
    void setup_5min_noise_matrices();
    void update_noise_5min(double market_volatility);
    void predict_with_forgetting(double lambda, double control_input, double extra_noise);
    void update_measurement(const Eigen::VectorXd& measurement);
    void apply_5min_frequency_nudging(const FrequencyFeatures& freq_features, double price_velocity);
    
    // Utility methods
    double compute_control_input(const BucketConfidence& bucket_conf, double price, double& extra_noise);
    double compute_market_volatility(double price_velocity, double current_price);
    double compute_adaptive_lambda(double volatility);
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

} // namespace hefkf_5min

#endif // FIVE_MIN_HEFKF_HPP 