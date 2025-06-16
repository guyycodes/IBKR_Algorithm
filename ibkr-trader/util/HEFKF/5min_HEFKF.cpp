// 5-Minute Hybrid Exponential Forgetting Kalman Filter Implementation
// Standalone implementation specialized for 5-minute horizon with smoother parameters

#include "5min_HEFKF.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace hefkf_5min {

// ─────────────────────── 20-Bucket Confidence Implementation ───────────────────────
void BucketConfidence20::normalize() {
    double total = up_000 + up_010 + up_020 + up_030 + up_040 + 
                   up_050 + up_060 + up_070 + up_080 + up_090 +
                   dn_000 + dn_010 + dn_020 + dn_030 + dn_040 + 
                   dn_050 + dn_060 + dn_070 + dn_080 + dn_090;
    
    if (total > 1e-10) {
        up_000 /= total;
        up_010 /= total;
        up_020 /= total;
        up_030 /= total;
        up_040 /= total;
        up_050 /= total;
        up_060 /= total;
        up_070 /= total;
        up_080 /= total;
        up_090 /= total;
        
        dn_000 /= total;
        dn_010 /= total;
        dn_020 /= total;
        dn_030 /= total;
        dn_040 /= total;
        dn_050 /= total;
        dn_060 /= total;
        dn_070 /= total;
        dn_080 /= total;
        dn_090 /= total;
    }
}

bool BucketConfidence20::is_valid() const {
    double total = up_000 + up_010 + up_020 + up_030 + up_040 + 
                   up_050 + up_060 + up_070 + up_080 + up_090 +
                   dn_000 + dn_010 + dn_020 + dn_030 + dn_040 + 
                   dn_050 + dn_060 + dn_070 + dn_080 + dn_090;
    
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

// ─────────────────────── 20-Bucket Expectation Implementation ───────────────────────
// UP bucket specifications: center return and variance (25bp increments)
const std::unordered_map<int, std::pair<double, double>> BucketExpectation20::m_bucket_specs_up = {
    {0, {+0.00000, std::pow(0.00002, 2)}},  // 0.00% (neutral), σ ≈ 0.2bp
    {1, {+0.00250, std::pow(0.00050, 2)}},  // 0.25% (25bp), σ ≈ 5bp
    {2, {+0.00500, std::pow(0.00080, 2)}},  // 0.50% (50bp), σ ≈ 8bp
    {3, {+0.00750, std::pow(0.00100, 2)}},  // 0.75% (75bp), σ ≈ 10bp
    {4, {+0.01000, std::pow(0.00120, 2)}},  // 1.00% (100bp), σ ≈ 12bp
    {5, {+0.01250, std::pow(0.00150, 2)}},  // 1.25% (125bp), σ ≈ 15bp
    {6, {+0.01500, std::pow(0.00180, 2)}},  // 1.50% (150bp), σ ≈ 18bp
    {7, {+0.01750, std::pow(0.00200, 2)}},  // 1.75% (175bp), σ ≈ 20bp
    {8, {+0.02000, std::pow(0.00230, 2)}},  // 2.00% (200bp), σ ≈ 23bp
    {9, {+0.02250, std::pow(0.00300, 2)}},  // 2.25%+ (225bp+), σ ≈ 30bp
};

// DOWN bucket specifications (mirror of UP)
const std::unordered_map<int, std::pair<double, double>> BucketExpectation20::m_bucket_specs_dn = {
    {0, {-0.00000, std::pow(0.00002, 2)}},  // -0.00% (neutral), σ ≈ 0.2bp
    {1, {-0.00250, std::pow(0.00050, 2)}},  // -0.25% (-25bp), σ ≈ 5bp
    {2, {-0.00500, std::pow(0.00080, 2)}},  // -0.50% (-50bp), σ ≈ 8bp
    {3, {-0.00750, std::pow(0.00100, 2)}},  // -0.75% (-75bp), σ ≈ 10bp
    {4, {-0.01000, std::pow(0.00120, 2)}},  // -1.00% (-100bp), σ ≈ 12bp
    {5, {-0.01250, std::pow(0.00150, 2)}},  // -1.25% (-125bp), σ ≈ 15bp
    {6, {-0.01500, std::pow(0.00180, 2)}},  // -1.50% (-150bp), σ ≈ 18bp
    {7, {-0.01750, std::pow(0.00200, 2)}},  // -1.75% (-175bp), σ ≈ 20bp
    {8, {-0.02000, std::pow(0.00230, 2)}},  // -2.00% (-200bp), σ ≈ 23bp
    {9, {-0.02250, std::pow(0.00300, 2)}},  // -2.25%- (-225bp-), σ ≈ 30bp
};

BucketExpectation20::ReturnStats BucketExpectation20::compute_expectation(const BucketConfidence20& conf) {
    ReturnStats stats;
    
    // Process UP buckets
    double up_probs[] = {conf.up_000, conf.up_010, conf.up_020, conf.up_030, conf.up_040,
                         conf.up_050, conf.up_060, conf.up_070, conf.up_080, conf.up_090};
    
    for (int i = 0; i < 10; ++i) {
        if (up_probs[i] > 1e-10 && m_bucket_specs_up.count(i)) {
            double mean_ret = m_bucket_specs_up.at(i).first;
            double variance = m_bucket_specs_up.at(i).second;
            
            stats.mean_return += up_probs[i] * mean_ret;
            stats.variance += up_probs[i] * (variance + mean_ret * mean_ret);
        }
    }
    
    // Process DOWN buckets
    double dn_probs[] = {conf.dn_000, conf.dn_010, conf.dn_020, conf.dn_030, conf.dn_040,
                         conf.dn_050, conf.dn_060, conf.dn_070, conf.dn_080, conf.dn_090};
    
    for (int i = 0; i < 10; ++i) {
        if (dn_probs[i] > 1e-10 && m_bucket_specs_dn.count(i)) {
            double mean_ret = m_bucket_specs_dn.at(i).first;
            double variance = m_bucket_specs_dn.at(i).second;
            
            stats.mean_return += dn_probs[i] * mean_ret;
            stats.variance += dn_probs[i] * (variance + mean_ret * mean_ret);
        }
    }
    
    stats.variance -= stats.mean_return * stats.mean_return;  // subtract E[X]²
    stats.variance = std::max(stats.variance, 1e-10);  // numerical safeguard
    
    return stats;
}

// ─────────────────────── Constructor ───────────────────────
FiveMinuteHEFKF::FiveMinuteHEFKF() {
    // Initialize matrices with correct dimensions
    m_x = Eigen::VectorXd::Zero(4);     // [price, velocity, volume, spread]
    m_P = Eigen::MatrixXd::Identity(4, 4);
    m_F = Eigen::MatrixXd::Identity(4, 4);
    m_H = Eigen::MatrixXd::Zero(4, 4);  // UPDATED: now 4x4 for velocity observation
    m_B = Eigen::VectorXd::Zero(4);     // control affects price directly
    m_Q = Eigen::MatrixXd::Identity(4, 4) * FiveMinHKFConfig::Q_BASE_SCALE;
    m_R = Eigen::MatrixXd::Identity(4, 4);  // UPDATED: now 4x4
    m_Q_base = m_Q;
    m_R_base = m_R;
    m_K = Eigen::MatrixXd::Zero(4, 4);  // UPDATED: Kalman gain now 4x4
}

// ─────────────────────── Matrix Setup ───────────────────────
void FiveMinuteHEFKF::setup_matrices(double dt) {
    m_dt = dt;
    
    // Transition matrix F: [price, velocity, volume, spread]
    m_F << 1, dt, 0, 0,
          0, 1,  0, 0,
          0, 0,  1, 0,
          0, 0,  0, 1;
    
    // UPDATED: Observation matrix H now observes [price, velocity, volume, spread]
    // This allows velocity to be corrected by measurements
    m_H << 1, 0, 0, 0,   // observe price
          0, 1, 0, 0,   // observe velocity (NEW!)
          0, 0, 1, 0,   // observe volume
          0, 0, 0, 1;   // observe spread
    
    // Control matrix B: control affects price directly
    m_B << 1, 0, 0, 0;
    
    // Setup 5min-specific noise matrices
    setup_5min_noise_matrices();
}

void FiveMinuteHEFKF::setup_5min_noise_matrices() {
    // 5-minute: Larger covariances for more smoothing
    m_P = Eigen::MatrixXd::Identity(4, 4) * FiveMinHKFConfig::INITIAL_P_SCALE;
    
    // UPDATED: Increase initial velocity uncertainty to handle unknown initial velocity
    // Velocity can range from -0.01 to +0.01 (±1% per tick), so variance should be ~0.0001
    m_P(1, 1) = 0.0001;  // 10x larger than default for velocity component
    
    // Base measurement noise (optimized for 5min)
    m_R_base << FiveMinHKFConfig::R_PRICE, 0, 0, 0,
               0, FiveMinHKFConfig::R_VELOCITY, 0, 0,
               0, 0, FiveMinHKFConfig::R_VOLUME, 0,
               0, 0, 0, FiveMinHKFConfig::R_SPREAD;
    
    // Base process noise - UPDATED: Higher noise for velocity (but less than 1min)
    m_Q_base = Eigen::MatrixXd::Zero(4, 4);
    m_Q_base(0, 0) = FiveMinHKFConfig::Q_BASE_SCALE;        // Price process noise
    m_Q_base(1, 1) = FiveMinHKFConfig::Q_BASE_SCALE * 3.0;  // VELOCITY: 3x higher (vs 5x in 1min)
    m_Q_base(2, 2) = FiveMinHKFConfig::Q_BASE_SCALE;        // Volume process noise
    m_Q_base(3, 3) = FiveMinHKFConfig::Q_BASE_SCALE;        // Spread process noise
    
    // Initialize current noise matrices
    m_R = m_R_base;
    m_Q = m_Q_base;
}

// ─────────────────────── Noise Update (5min-specific) ───────────────────────
void FiveMinuteHEFKF::update_noise_5min(double market_volatility) {
    if (!FiveMinHKFConfig::ADAPTIVE_NOISE) return;
    
    double scale = std::clamp(market_volatility / FiveMinHKFConfig::VOL_THRESHOLD, 
                             FiveMinHKFConfig::SCALE_MIN, FiveMinHKFConfig::SCALE_MAX);
    
    // Only update matrices if scale has changed meaningfully (saves ~10 µs per tick)
    if (std::abs(scale - m_scale_prev) > SCALE_UPDATE_THRESHOLD) {
        m_R = m_R_base * scale;
        m_Q = m_Q_base * scale * FiveMinHKFConfig::Q_MULTIPLIER;
        m_scale_prev = scale;
    }
}

// ─────────────────────── Prediction with Forgetting ───────────────────────
void FiveMinuteHEFKF::predict_with_forgetting(double lambda, double control_input, double extra_noise) {
    double lambda_inv = 1.0 / lambda;
    
    // Covariance prediction with forgetting: P = (1/λ) F P F^T + Q + extra_noise
    Eigen::MatrixXd P_pred = lambda_inv * (m_F * m_P * m_F.transpose());
    
    // Add extra noise from bucket uncertainty
    P_pred(0, 0) += extra_noise;
    
    // Update covariance with process noise
    m_P = P_pred + m_Q;
    
    // State prediction: x = F * x + B * u
    m_x = m_F * m_x + m_B * control_input;
}

// ─────────────────────── Measurement Update ───────────────────────
void FiveMinuteHEFKF::update_measurement(const Eigen::VectorXd& measurement) {
    // Apply warm-up period logic for velocity measurements
    Eigen::MatrixXd R_adaptive = m_R;
    if (m_update_count < WARMUP_PERIOD) {
        // During warm-up, increase velocity measurement noise to allow faster adaptation
        double warmup_factor = static_cast<double>(WARMUP_PERIOD - m_update_count) / WARMUP_PERIOD;
        R_adaptive(1, 1) *= (1.0 + 9.0 * warmup_factor);  // Up to 10x noise during early warm-up
    }
    
    // Innovation: y = z - H * x
    Eigen::VectorXd y = measurement - m_H * m_x;
    
    // Innovation covariance: S = H * P * H^T + R (using adaptive R)
    Eigen::MatrixXd S = m_H * m_P * m_H.transpose() + R_adaptive;
    
    // Kalman gain: K = P * H^T * S^(-1)
    m_K = m_P * m_H.transpose() * S.inverse();
    
    // State update: x = x + K * y
    m_x = m_x + m_K * y;
    
    // Joseph form covariance update: P = (I - K*H)*P*(I - K*H)^T + K*R*K^T
    Eigen::MatrixXd I = Eigen::MatrixXd::Identity(4, 4);
    Eigen::MatrixXd IKH = I - m_K * m_H;
    m_P = IKH * m_P * IKH.transpose() + m_K * m_R * m_K.transpose();
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
        m_x(0) += FiveMinHKFConfig::FREQUENCY_DOMAIN_WEIGHT * bias;
        // Inflate covariance because we perturbed the state estimate directly
        m_P *= 1.05;
    }
}

// ─────────────────────── Utility Methods ───────────────────────
double FiveMinuteHEFKF::compute_control_input(const BucketConfidence20& bucket_conf, 
                                              double price, 
                                              double& extra_noise) {
    double control_input = 0.0;
    extra_noise = 0.0;
    
    if (FiveMinHKFConfig::BUCKET_WEIGHT <= 1e-10) return control_input;
    
    auto stats = BucketExpectation20::compute_expectation(bucket_conf);
    
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
    m_x(0) = initial_data.price;    // price
    m_x(1) = 0.0;                   // velocity (unknown initially)
    m_x(2) = initial_data.volume;   // volume
    m_x(3) = initial_data.spread;   // spread
    
    m_last_price = initial_data.price;
    m_initialized = true;
}

FilterOutput FiveMinuteHEFKF::process(const MarketData& data) {
    if (!m_initialized) {
        throw std::runtime_error("Filter not initialized. Call initialize() first.");
    }
    
    if (!utils::validate_market_data(data)) {
        throw std::invalid_argument("Invalid market data");
    }
    
    // Calculate price velocity
    double price_velocity = (data.price - m_last_price) / m_dt;
    m_last_price = data.price;
    
    // Smart velocity initialization: Use first few price changes to estimate initial velocity
    if (m_update_count < 3 && m_update_count > 0) {
        // During first few updates, directly update velocity estimate with observed velocity
        m_x(1) = (m_x(1) * m_update_count + price_velocity) / (m_update_count + 1);
        // Also increase velocity uncertainty to reflect this is still an estimate
        m_P(1, 1) = std::max(m_P(1, 1), 0.0001);
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
    m_update_count++;
    
    // Prepare output
    FilterOutput output;
    output.price_smoothed = m_x(0);
    output.price_velocity = m_x(1);
    output.volume_denoised = m_x(2);
    output.spread_filtered = m_x(3);
    output.lambda_used = lambda;
    output.timestamp = data.timestamp;
    
    return output;
}

void FiveMinuteHEFKF::reset() {
    m_initialized = false;
    m_x.setZero();
    m_P.setIdentity();
    m_last_price = 0.0;
    m_scale_prev = 1.0;
    m_update_count = 0;  // Reset warm-up counter
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