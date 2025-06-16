// 5-Minute Hybrid Exponential Forgetting Kalman Filter Implementation
// Standalone implementation specialized for 5-minute horizon with smoother parameters

#include "5min_HEFKF.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace hefkf_5min {

// ─────────────────────── Bucket Confidence Implementation ───────────────────────
void BucketConfidence::normalize() {
    double total = up_001_002 + up_002_005 + up_005_010 + up_010_plus +
                   dn_001_002 + dn_002_005 + dn_005_010 + dn_010_plus;
    
    if (total > 1e-10) {
        up_001_002 /= total;
        up_002_005 /= total;
        up_005_010 /= total;
        up_010_plus /= total;
        dn_001_002 /= total;
        dn_002_005 /= total;
        dn_005_010 /= total;
        dn_010_plus /= total;
    }
}

bool BucketConfidence::is_valid() const {
    double total = up_001_002 + up_002_005 + up_005_010 + up_010_plus +
                   dn_001_002 + dn_002_005 + dn_005_010 + dn_010_plus;
    
    return total > 1e-10 && total <= 1.0 + 1e-6;
}

// ─────────────────────── Frequency Features Implementation ───────────────────────
FrequencyFeatures::FrequencyFeatures() {
    // Initialize band maps with default values
    coherence_price_volume_by_band["microstructure"] = 0.0;
    coherence_price_volume_by_band["short_term"] = 0.0;
    coherence_price_volume_by_band["medium_term"] = 0.0;
    coherence_price_volume_by_band["trend"] = 0.0;
    
    coherence_price_spread_by_band["microstructure"] = 0.0;
    coherence_price_spread_by_band["short_term"] = 0.0;
    coherence_price_spread_by_band["medium_term"] = 0.0;
    coherence_price_spread_by_band["trend"] = 0.0;
}

// ─────────────────────── Bucket Expectation Implementation ───────────────────────
const std::unordered_map<std::string, std::pair<double, double>> BucketExpectation::bucket_specs_ = {
    // Updated to 10x more granular for 1-5 minute horizons
    {"up_001_002", {+0.00015, std::pow(0.00003, 2)}},  // +0.015% (1.5bp), σ ≈ 0.3bp
    {"up_002_005", {+0.00035, std::pow(0.00009, 2)}},  // +0.035% (3.5bp), σ ≈ 0.9bp
    {"up_005_010", {+0.00075, std::pow(0.00012, 2)}},  // +0.075% (7.5bp), σ ≈ 1.2bp
    {"up_010_plus", {+0.00150, std::pow(0.00020, 2)}}, // +0.150% (15bp), σ ≈ 2.0bp
    {"dn_001_002", {-0.00015, std::pow(0.00003, 2)}},  // mirror image
    {"dn_002_005", {-0.00035, std::pow(0.00009, 2)}},
    {"dn_005_010", {-0.00075, std::pow(0.00012, 2)}},
    {"dn_010_plus", {-0.00150, std::pow(0.00020, 2)}},
};

BucketExpectation::ReturnStats BucketExpectation::compute_expectation(const BucketConfidence& conf) {
    ReturnStats stats;
    
    // Map bucket values to probabilities
    std::unordered_map<std::string, double> bucket_probs = {
        {"up_001_002", conf.up_001_002},
        {"up_002_005", conf.up_002_005},
        {"up_005_010", conf.up_005_010},
        {"up_010_plus", conf.up_010_plus},
        {"dn_001_002", conf.dn_001_002},
        {"dn_002_005", conf.dn_002_005},
        {"dn_005_010", conf.dn_005_010},
        {"dn_010_plus", conf.dn_010_plus},
    };
    
    // Compute expected return and variance using law of total expectation
    for (const auto& [bucket, prob] : bucket_probs) {
        if (bucket_specs_.count(bucket) && prob > 1e-10) {
            double mean_ret = bucket_specs_.at(bucket).first;
            double variance = bucket_specs_.at(bucket).second;
            
            stats.mean_return += prob * mean_ret;
            stats.variance += prob * (variance + mean_ret * mean_ret);  // law of total variance
        }
    }
    
    stats.variance -= stats.mean_return * stats.mean_return;  // subtract E[X]²
    stats.variance = std::max(stats.variance, 1e-10);  // numerical safeguard
    
    return stats;
}

// ─────────────────────── Constructor ───────────────────────
FiveMinuteHEFKF::FiveMinuteHEFKF() {
    // Initialize matrices with correct dimensions
    x_ = Eigen::VectorXd::Zero(4);     // [price, velocity, volume, spread]
    P_ = Eigen::MatrixXd::Identity(4, 4);
    F_ = Eigen::MatrixXd::Identity(4, 4);
    H_ = Eigen::MatrixXd::Zero(4, 4);  // UPDATED: now 4x4 for velocity observation
    B_ = Eigen::VectorXd::Zero(4);     // control affects price directly
    Q_ = Eigen::MatrixXd::Identity(4, 4) * FiveMinHKFConfig::Q_BASE_SCALE;
    R_ = Eigen::MatrixXd::Identity(4, 4);  // UPDATED: now 4x4
    Q_base_ = Q_;
    R_base_ = R_;
    K_ = Eigen::MatrixXd::Zero(4, 4);  // UPDATED: Kalman gain now 4x4
}

// ─────────────────────── Matrix Setup ───────────────────────
void FiveMinuteHEFKF::setup_matrices(double dt) {
    dt_ = dt;
    
    // Transition matrix F: [price, velocity, volume, spread]
    F_ << 1, dt, 0, 0,
          0, 1,  0, 0,
          0, 0,  1, 0,
          0, 0,  0, 1;
    
    // UPDATED: Observation matrix H now observes [price, velocity, volume, spread]
    // This allows velocity to be corrected by measurements
    H_ << 1, 0, 0, 0,   // observe price
          0, 1, 0, 0,   // observe velocity (NEW!)
          0, 0, 1, 0,   // observe volume
          0, 0, 0, 1;   // observe spread
    
    // Control matrix B: control affects price directly
    B_ << 1, 0, 0, 0;
    
    // Setup 5min-specific noise matrices
    setup_5min_noise_matrices();
}

void FiveMinuteHEFKF::setup_5min_noise_matrices() {
    // 5-minute: Larger covariances for more smoothing
    P_ = Eigen::MatrixXd::Identity(4, 4) * FiveMinHKFConfig::INITIAL_P_SCALE;
    
    // UPDATED: Increase initial velocity uncertainty to handle unknown initial velocity
    // Velocity can range from -0.01 to +0.01 (±1% per tick), so variance should be ~0.0001
    P_(1, 1) = 0.0001;  // 10x larger than default for velocity component
    
    // Base measurement noise (optimized for 5min)
    R_base_ << FiveMinHKFConfig::R_PRICE, 0, 0, 0,
               0, FiveMinHKFConfig::R_VELOCITY, 0, 0,
               0, 0, FiveMinHKFConfig::R_VOLUME, 0,
               0, 0, 0, FiveMinHKFConfig::R_SPREAD;
    
    // Base process noise - UPDATED: Higher noise for velocity (but less than 1min)
    Q_base_ = Eigen::MatrixXd::Zero(4, 4);
    Q_base_(0, 0) = FiveMinHKFConfig::Q_BASE_SCALE;        // Price process noise
    Q_base_(1, 1) = FiveMinHKFConfig::Q_BASE_SCALE * 3.0;  // VELOCITY: 3x higher (vs 5x in 1min)
    Q_base_(2, 2) = FiveMinHKFConfig::Q_BASE_SCALE;        // Volume process noise
    Q_base_(3, 3) = FiveMinHKFConfig::Q_BASE_SCALE;        // Spread process noise
    
    // Initialize current noise matrices
    R_ = R_base_;
    Q_ = Q_base_;
}

// ─────────────────────── Noise Update (5min-specific) ───────────────────────
void FiveMinuteHEFKF::update_noise_5min(double market_volatility) {
    if (!FiveMinHKFConfig::ADAPTIVE_NOISE) return;
    
    double scale = std::clamp(market_volatility / FiveMinHKFConfig::VOL_THRESHOLD, 
                             FiveMinHKFConfig::SCALE_MIN, FiveMinHKFConfig::SCALE_MAX);
    
    // Only update matrices if scale has changed meaningfully (saves ~10 µs per tick)
    if (std::abs(scale - scale_prev_) > SCALE_UPDATE_THRESHOLD) {
        R_ = R_base_ * scale;
        Q_ = Q_base_ * scale * FiveMinHKFConfig::Q_MULTIPLIER;
        scale_prev_ = scale;
    }
}

// ─────────────────────── Prediction with Forgetting ───────────────────────
void FiveMinuteHEFKF::predict_with_forgetting(double lambda, double control_input, double extra_noise) {
    double lambda_inv = 1.0 / lambda;
    
    // Covariance prediction with forgetting: P = (1/λ) F P F^T + Q + extra_noise
    Eigen::MatrixXd P_pred = lambda_inv * (F_ * P_ * F_.transpose());
    
    // Add extra noise from bucket uncertainty
    P_pred(0, 0) += extra_noise;
    
    // Update covariance with process noise
    P_ = P_pred + Q_;
    
    // State prediction: x = F * x + B * u
    x_ = F_ * x_ + B_ * control_input;
}

// ─────────────────────── Measurement Update ───────────────────────
void FiveMinuteHEFKF::update_measurement(const Eigen::VectorXd& measurement) {
    // Apply warm-up period logic for velocity measurements
    Eigen::MatrixXd R_adaptive = R_;
    if (update_count_ < WARMUP_PERIOD) {
        // During warm-up, increase velocity measurement noise to allow faster adaptation
        double warmup_factor = static_cast<double>(WARMUP_PERIOD - update_count_) / WARMUP_PERIOD;
        R_adaptive(1, 1) *= (1.0 + 9.0 * warmup_factor);  // Up to 10x noise during early warm-up
    }
    
    // Innovation: y = z - H * x
    Eigen::VectorXd y = measurement - H_ * x_;
    
    // Innovation covariance: S = H * P * H^T + R (using adaptive R)
    Eigen::MatrixXd S = H_ * P_ * H_.transpose() + R_adaptive;
    
    // Kalman gain: K = P * H^T * S^(-1)
    K_ = P_ * H_.transpose() * S.inverse();
    
    // State update: x = x + K * y
    x_ = x_ + K_ * y;
    
    // Joseph form covariance update: P = (I - K*H)*P*(I - K*H)^T + K*R*K^T
    Eigen::MatrixXd I = Eigen::MatrixXd::Identity(4, 4);
    Eigen::MatrixXd IKH = I - K_ * H_;
    P_ = IKH * P_ * IKH.transpose() + K_ * R_ * K_.transpose();
}

// ─────────────────────── 5min-Specific Frequency Nudging ───────────────────────
void FiveMinuteHEFKF::apply_5min_frequency_nudging(const FrequencyFeatures& freq_features, 
                                                   double price_velocity) {
    if (FiveMinHKFConfig::FREQUENCY_DOMAIN_WEIGHT <= 1e-10) return;
    
    // Base frequency components for 5min predictions
    double trend_bias = freq_features.trend_strength * price_velocity;
    double short_term_bias = 0.0;  // Placeholder for short_term SNR
    
    // Coherence-enhanced signal validation
    double pv_coherence = freq_features.coherence_price_volume_peak;
    double pv_micro = 0.0;
    double pv_short = 0.0;
    
    auto it_micro = freq_features.coherence_price_volume_by_band.find("microstructure");
    if (it_micro != freq_features.coherence_price_volume_by_band.end()) {
        pv_micro = it_micro->second;
    }
    
    auto it_short = freq_features.coherence_price_volume_by_band.find("short_term");
    if (it_short != freq_features.coherence_price_volume_by_band.end()) {
        pv_short = it_short->second;
    }
    
    // Signal quality: emphasize short-term coherence for 5min horizon
    double quality_factor = (pv_coherence + pv_short * 1.2 + pv_micro * 0.8) / 3.0;
    
    // Enhanced bias with coherence modulation (5min-specific multiplier)
    double base_bias = trend_bias + short_term_bias;
    double bias = base_bias * (0.6 + 0.8 * quality_factor);  // 0.6-1.4x multiplier
    
    if (std::abs(bias) > 1e-10) {
        x_(0) += FiveMinHKFConfig::FREQUENCY_DOMAIN_WEIGHT * bias;
        // Inflate covariance because we perturbed the state estimate directly
        P_ *= 1.05;
    }
}

// ─────────────────────── Utility Methods ───────────────────────
double FiveMinuteHEFKF::compute_control_input(const BucketConfidence& bucket_conf, 
                                              double price, 
                                              double& extra_noise) {
    double control_input = 0.0;
    extra_noise = 0.0;
    
    if (FiveMinHKFConfig::BUCKET_WEIGHT <= 1e-10) return control_input;
    
    auto stats = BucketExpectation::compute_expectation(bucket_conf);
    
    double mu_price = price * stats.mean_return;    // level move, not pct
    double sigma2_price = (price * price) * stats.variance;
    
    // Dial how much to trust: 0..1 (like a Kalman gain for the control channel)
    double u_weight = FiveMinHKFConfig::BUCKET_WEIGHT;
    
    control_input = u_weight * mu_price;           // deterministic drift
    extra_noise = u_weight * u_weight * sigma2_price;  // stochastic part
    
    return control_input;
}

double FiveMinuteHEFKF::compute_market_volatility(double price_velocity, double current_price) {
    return current_price > 1e-10 ? std::abs(price_velocity) / current_price : 0.0;
}

double FiveMinuteHEFKF::compute_adaptive_lambda(double volatility) {
    if (!FiveMinHKFConfig::LAMBDA_ADAPTIVE) {
        return FiveMinHKFConfig::LAMBDA_FIXED;
    }
    
    // Normalize volatility to [0, 2] range
    double vol_normalized = std::min(volatility / FiveMinHKFConfig::VOL_THRESHOLD, 2.0);
    
    // Compute adaptive lambda: higher volatility → lower lambda (shorter memory)
    double lambda = FiveMinHKFConfig::LAMBDA_MAX - 
                   (FiveMinHKFConfig::LAMBDA_MAX - FiveMinHKFConfig::LAMBDA_MIN) * (vol_normalized / 2.0);
    
    // Clamp result to valid bounds
    return std::clamp(lambda, FiveMinHKFConfig::LAMBDA_MIN, FiveMinHKFConfig::LAMBDA_MAX);
}

// ─────────────────────── Public Interface ───────────────────────
void FiveMinuteHEFKF::initialize(const MarketData& initial_data, double dt) {
    if (!utils::validate_market_data(initial_data)) {
        throw std::invalid_argument("Invalid initial market data");
    }
    
    setup_matrices(dt);
    
    // Initialize state with first measurement
    x_(0) = initial_data.price;    // price
    x_(1) = 0.0;                   // velocity (unknown initially)
    x_(2) = initial_data.volume;   // volume
    x_(3) = initial_data.spread;   // spread
    
    last_price_ = initial_data.price;
    initialized_ = true;
}

FilterOutput FiveMinuteHEFKF::process(const MarketData& data) {
    if (!initialized_) {
        throw std::runtime_error("Filter not initialized. Call initialize() first.");
    }
    
    if (!utils::validate_market_data(data)) {
        throw std::invalid_argument("Invalid market data");
    }
    
    // Calculate price velocity
    double price_velocity = (data.price - last_price_) / dt_;
    last_price_ = data.price;
    
    // Smart velocity initialization: Use first few price changes to estimate initial velocity
    if (update_count_ < 3 && update_count_ > 0) {
        // During first few updates, directly update velocity estimate with observed velocity
        x_(1) = (x_(1) * update_count_ + price_velocity) / (update_count_ + 1);
        // Also increase velocity uncertainty to reflect this is still an estimate
        P_(1, 1) = std::max(P_(1, 1), 0.0001);
    }
    
    // Compute market volatility for adaptive processing
    double market_volatility = compute_market_volatility(price_velocity, data.price);
    
    // Update noise matrices based on market conditions (5min-specific)
    update_noise_5min(market_volatility);
    
    // Compute adaptive lambda
    double lambda = compute_adaptive_lambda(market_volatility);
    
    // Bucket control input
    double control_input = 0.0;
    double extra_noise = 0.0;
    
    if (data.bucket_conf) {
        data.bucket_conf->normalize();
        if (data.bucket_conf->is_valid()) {
            control_input = compute_control_input(*data.bucket_conf, data.price, extra_noise);
        }
    }
    
    // Prediction step with forgetting
    predict_with_forgetting(lambda, control_input, extra_noise);
    
    // Update step - now including velocity in measurement
    Eigen::VectorXd measurement(4);
    measurement << data.price, price_velocity, data.volume, data.spread;
    update_measurement(measurement);
    
    // 5min-specific frequency domain nudging
    if (data.freq_features) {
        apply_5min_frequency_nudging(*data.freq_features, price_velocity);
    }
    
    // Increment update counter for warm-up tracking
    update_count_++;
    
    // Prepare output
    FilterOutput output;
    output.price_smoothed = x_(0);
    output.price_velocity = x_(1);
    output.volume_denoised = x_(2);
    output.spread_filtered = x_(3);
    output.lambda_used = lambda;
    output.timestamp = data.timestamp;
    
    return output;
}

void FiveMinuteHEFKF::reset() {
    initialized_ = false;
    x_.setZero();
    P_.setIdentity();
    last_price_ = 0.0;
    scale_prev_ = 1.0;
    update_count_ = 0;  // Reset warm-up counter
    setup_5min_noise_matrices();  // Restore base noise matrices
}

// ─────────────────────── Utility Functions Implementation ───────────────────────
namespace utils {

double timestamp_to_seconds(const std::chrono::system_clock::time_point& tp) {
    auto duration = tp.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
}

FrequencyFeatures create_default_frequency_features() {
    return FrequencyFeatures{};  // Constructor initializes default values
}

bool validate_market_data(const MarketData& data) {
    // Basic validation
    if (data.price <= 0.0 || data.volume < 0.0 || data.spread < 0.0) {
        return false;
    }
    
    // Check for reasonable ranges
    if (std::isnan(data.price) || std::isnan(data.volume) || std::isnan(data.spread)) {
        return false;
    }
    
    if (std::isinf(data.price) || std::isinf(data.volume) || std::isinf(data.spread)) {
        return false;
    }
    
    return true;
}

} // namespace utils

} // namespace hefkf_5min 