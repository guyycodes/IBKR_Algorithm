// 1-Minute Hybrid Exponential Forgetting Kalman Filter
// Standalone implementation optimized for 1-minute horizon with responsive parameters
// Completely self-contained without dependencies on the original HEFKF

#ifndef ONE_MIN_HEFKF_HPP
#define ONE_MIN_HEFKF_HPP

#include <chrono>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <Eigen/Dense>

namespace hefkf_1min {

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

// ─────────────────────── Configuration Namespace ───────────────────────
namespace OneMinHKFConfig {

// Noise Configuration - UPDATED FOR RESPONSIVE VELOCITY TRACKING
// - R_PRICE: Measurement noise (lower = trust measurements more)
// - LAMBDA_MIN/MAX: Memory length (lower = shorter memory, faster adaptation)
// For uptrend detection: Increase BUCKET_WEIGHT and decrease R_PRICE
// For stability: Increase R_PRICE and decrease BUCKET_WEIGHT

// Buckets and Confidence
static constexpr double BUCKET_WEIGHT = 0.2;       // Good balance
static constexpr double FREQUENCY_DOMAIN_WEIGHT = 0.1;
static constexpr double CONFIDENCE_ALPHA = 2.0;    // Dirichlet sharpening
static constexpr double MIN_COHERENCE = 0.1;

// Process and Measurement Noise - UPDATED FOR VELOCITY TRACKING
static constexpr double Q_BASE_SCALE = 2e-4;       // INCREASED 4x for more process uncertainty
static constexpr double Q_MULTIPLIER = 1.0;        // Base multiplier
static constexpr double R_PRICE = 0.0001;          // Keep low for good price tracking
static constexpr double R_VELOCITY = 0.0001;       // REDUCED 10x to trust velocity measurements more
static constexpr double R_VOLUME = 0.1;
static constexpr double R_SPREAD = 0.001;
static constexpr double INITIAL_P_SCALE = 0.1;

// Lambda (Forgetting Factor) - UPDATED FOR EXPONENTIAL FORGETTING
static constexpr bool LAMBDA_ADAPTIVE = true;
static constexpr double LAMBDA_MIN = 0.70;         // REDUCED from 0.92 - 30% weight on new data
static constexpr double LAMBDA_MAX = 0.85;         // REDUCED from 0.98 - 15% weight on new data minimum
static constexpr double LAMBDA_FIXED = 0.75;       // REDUCED from 0.95 - if adaptive is false

// Adaptive Parameters
static constexpr bool ADAPTIVE_NOISE = true;
static constexpr double SCALE_MIN = 0.5;
static constexpr double SCALE_MAX = 3.0;
static constexpr double VOL_THRESHOLD = 0.002;

} // namespace OneMinHKFConfig

// ─────────────────────── 1-Minute Specialized Filter Class ───────────────────────────
class OneMinuteHEFKF {
public:
    // Constructor
    OneMinuteHEFKF();
    
    // Destructor
    ~OneMinuteHEFKF() = default;
    
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
    
    // Warm-up period tracking
    size_t update_count_ = 0;     // Number of updates processed
    static constexpr size_t WARMUP_PERIOD = 50;  // Updates before trusting velocity
    
    // Performance optimization constants
    static constexpr double SCALE_UPDATE_THRESHOLD = 0.05;  // 5% change threshold
    
    // Internal methods (1min-specific implementations)
    void setup_matrices(double dt);
    void setup_1min_noise_matrices();
    void update_noise_1min(double market_volatility);
    void predict_with_forgetting(double lambda, double control_input, double extra_noise);
    void update_measurement(const Eigen::VectorXd& measurement);
    void apply_1min_frequency_nudging(const FrequencyFeatures& freq_features, double price_velocity);
    
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

} // namespace hefkf_1min

#endif // ONE_MIN_HEFKF_HPP 