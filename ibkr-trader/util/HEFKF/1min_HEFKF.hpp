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

// ─────────────────────── 20-Bucket Confidence Structure ───────────────────────
struct BucketConfidence20 {
    // UP buckets (10 buckets)
    double up_000 = 0.0;  // 0.00% ± 0.125%
    double up_010 = 0.0;  // 0.25% ± 0.125%
    double up_020 = 0.0;  // 0.50% ± 0.125%
    double up_030 = 0.0;  // 0.75% ± 0.125%
    double up_040 = 0.0;  // 1.00% ± 0.125%
    double up_050 = 0.0;  // 1.25% ± 0.125%
    double up_060 = 0.0;  // 1.50% ± 0.125%
    double up_070 = 0.0;  // 1.75% ± 0.125%
    double up_080 = 0.0;  // 2.00% ± 0.125%
    double up_090 = 0.0;  // 2.25%+ (all ≥ 2.125%)
    
    // DOWN buckets (10 buckets)
    double dn_000 = 0.0;  // -0.00% ± 0.125%
    double dn_010 = 0.0;  // -0.25% ± 0.125%
    double dn_020 = 0.0;  // -0.50% ± 0.125%
    double dn_030 = 0.0;  // -0.75% ± 0.125%
    double dn_040 = 0.0;  // -1.00% ± 0.125%
    double dn_050 = 0.0;  // -1.25% ± 0.125%
    double dn_060 = 0.0;  // -1.50% ± 0.125%
    double dn_070 = 0.0;  // -1.75% ± 0.125%
    double dn_080 = 0.0;  // -2.00% ± 0.125%
    double dn_090 = 0.0;  // -2.25%- (all ≤ -2.125%)
    
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
    std::unique_ptr<BucketConfidence20> bucket_conf;
    
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

// ─────────────────────── 20-Bucket Expectation Helper ───────────────────────────
class BucketExpectation20 {
public:
    struct ReturnStats {
        double mean_return = 0.0;
        double variance = 0.0;
    };
    
    static ReturnStats compute_expectation(const BucketConfidence20& conf);
    
private:
    // 20-Bucket specifications with 2.5bp increments
    // Each bucket represents expected return at center ± variance
    static const std::unordered_map<int, std::pair<double, double>> m_bucket_specs_up;
    static const std::unordered_map<int, std::pair<double, double>> m_bucket_specs_dn;
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
    const Eigen::VectorXd& get_state() const { return m_x; }
    const Eigen::MatrixXd& get_covariance() const { return m_P; }
    
    // Check if filter is initialized
    bool is_initialized() const { return m_initialized; }

private:
    // Filter state
    Eigen::VectorXd m_x;           // State vector [price, velocity, volume, spread]
    Eigen::MatrixXd m_P;           // Covariance matrix
    Eigen::MatrixXd m_F;           // Transition matrix
    Eigen::MatrixXd m_H;           // Observation matrix
    Eigen::MatrixXd m_B;           // Control matrix
    Eigen::MatrixXd m_Q;           // Process noise covariance
    Eigen::MatrixXd m_R;           // Measurement noise covariance
    Eigen::MatrixXd m_Q_base;      // Base process noise (for scaling)
    Eigen::MatrixXd m_R_base;      // Base measurement noise (for scaling)
    Eigen::MatrixXd m_K;           // Kalman gain (stored for Joseph form)
    
    // State tracking
    bool m_initialized = false;
    double m_dt = 1.0;             // Time step
    double m_last_price = 0.0;     // For velocity calculation
    double m_scale_prev = 1.0;     // Cached noise scale factor
    
    // Warm-up period tracking
    size_t m_update_count = 0;     // Number of updates processed
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
    double compute_control_input(const BucketConfidence20& bucket_conf, double price, double& extra_noise);
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