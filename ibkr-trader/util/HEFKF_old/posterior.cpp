// Posterior Bucket Probability Extraction Implementation
// Gaussian integration over bucket boundaries and Dirichlet sharpening

#include "posterior.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <iostream>

// ─────────────────────── Key Configuration Parameters ───────────────────────
namespace {
    // Minimum uncertainty in return predictions (as fraction of price)
    // This prevents overconfidence when Kalman uncertainty is very low
    // UPDATED: Reduced to 0.00002 for 10x more granular buckets
    // With buckets now at 0.01-0.02% level, we need finer uncertainty floor
    constexpr double MIN_RETURN_STD = 0.00002;  // 0.002% (0.2 basis points) - allows bucket discrimination
    
    // Velocity-dependent uncertainty factor
    // Adds uncertainty proportional to the expected move size
    // Rationale: Larger/faster moves are inherently less certain
    // Range: 0.3 (standard) to 2.0 (high uncertainty for extreme moves)
    constexpr double VELOCITY_UNCERTAINTY_FACTOR = 0.9;  // Keep at 0.9-1.0 for proper risk modeling
    
    // Maximum return for bucket integration (caps extreme tails)
    // Prevents numerical issues with very large moves
    constexpr double MAX_RETURN_FOR_BUCKETS = 0.05;  // 5% max single-period return
}

// ─────────────────────── Common BucketConfidence Implementation ───────────────────────
namespace hefkf_common {

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

hefkf_1min::BucketConfidence BucketConfidence::to_1min() const {
    hefkf_1min::BucketConfidence bc;
    bc.up_001_002 = up_001_002;
    bc.up_002_005 = up_002_005;
    bc.up_005_010 = up_005_010;
    bc.up_010_plus = up_010_plus;
    bc.dn_001_002 = dn_001_002;
    bc.dn_002_005 = dn_002_005;
    bc.dn_005_010 = dn_005_010;
    bc.dn_010_plus = dn_010_plus;
    return bc;
}

hefkf_5min::BucketConfidence BucketConfidence::to_5min() const {
    hefkf_5min::BucketConfidence bc;
    bc.up_001_002 = up_001_002;
    bc.up_002_005 = up_002_005;
    bc.up_005_010 = up_005_010;
    bc.up_010_plus = up_010_plus;
    bc.dn_001_002 = dn_001_002;
    bc.dn_002_005 = dn_002_005;
    bc.dn_005_010 = dn_005_010;
    bc.dn_010_plus = dn_010_plus;
    return bc;
}

BucketConfidence BucketConfidence::from_1min(const hefkf_1min::BucketConfidence& bc) {
    BucketConfidence common_bc;
    common_bc.up_001_002 = bc.up_001_002;
    common_bc.up_002_005 = bc.up_002_005;
    common_bc.up_005_010 = bc.up_005_010;
    common_bc.up_010_plus = bc.up_010_plus;
    common_bc.dn_001_002 = bc.dn_001_002;
    common_bc.dn_002_005 = bc.dn_002_005;
    common_bc.dn_005_010 = bc.dn_005_010;
    common_bc.dn_010_plus = bc.dn_010_plus;
    return common_bc;
}

BucketConfidence BucketConfidence::from_5min(const hefkf_5min::BucketConfidence& bc) {
    BucketConfidence common_bc;
    common_bc.up_001_002 = bc.up_001_002;
    common_bc.up_002_005 = bc.up_002_005;
    common_bc.up_005_010 = bc.up_005_010;
    common_bc.up_010_plus = bc.up_010_plus;
    common_bc.dn_001_002 = bc.dn_001_002;
    common_bc.dn_002_005 = bc.dn_002_005;
    common_bc.dn_005_010 = bc.dn_005_010;
    common_bc.dn_010_plus = bc.dn_010_plus;
    return common_bc;
}

} // namespace hefkf_common

// ─────────────────────── Gaussian Integration Implementation ───────────────────────
double GaussianIntegrator::gaussian_cdf_interval(double mean, double std_dev, double a, double b) {
    if (std_dev <= 1e-12) {
        // Degenerate case: point mass
        return (mean >= a && mean <= b) ? 1.0 : 0.0;
    }
    
    // Standardize the interval
    double z_a = (a - mean) / std_dev;
    double z_b = (b - mean) / std_dev;
    
    return standard_normal_cdf(z_b) - standard_normal_cdf(z_a);
}

double GaussianIntegrator::standard_normal_cdf(double x) {
    // Abramowitz and Stegun approximation (accurate to ~7 decimal places)
    static constexpr double a1 =  0.254829592;
    static constexpr double a2 = -0.284496736;
    static constexpr double a3 =  1.421413741;
    static constexpr double a4 = -1.453152027;
    static constexpr double a5 =  1.061405429;
    static constexpr double p  =  0.3275911;
    
    // Handle extreme values
    if (x < -8.0) return 0.0;
    if (x > 8.0) return 1.0;
    
    int sign = (x >= 0) ? 1 : -1;
    x = std::abs(x);
    
    // A&S formula 7.1.26
    double t = 1.0 / (1.0 + p * x);
    double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * std::exp(-x * x / 2.0) * INV_SQRT_2PI;
    
    return 0.5 * (1.0 + sign * y);
}

// ─────────────────────── Posterior Extraction Implementation ───────────────────────
hefkf_common::BucketConfidence posterior_from_1min_KF(const hefkf_1min::OneMinuteHEFKF& kf,
                                                      double current_price, 
                                                      double dt) {
    if (!kf.is_initialized()) {
        throw std::invalid_argument("Kalman filter not initialized");
    }
    
    // Get current state and covariance
    const auto& state = kf.get_state();
    const auto& covariance = kf.get_covariance();
    
    // Extract price prediction and uncertainty
    double predicted_price = state(0);  // Current price estimate
    double price_velocity = state(1);   // Price velocity
    
    // Future price prediction: price(t+dt) = price(t) + velocity * dt
    double future_price = predicted_price + price_velocity * dt;
    
    // Future price uncertainty from covariance propagation
    // For simple model: Var[price(t+dt)] ≈ Var[price] + dt² * Var[velocity] + 2*dt*Cov[price,velocity]
    double price_var = covariance(0, 0);
    double velocity_var = covariance(1, 1);
    double price_velocity_cov = covariance(0, 1);
    
    double future_price_var = price_var + dt * dt * velocity_var + 2.0 * dt * price_velocity_cov;
    double future_price_std = std::sqrt(std::max(future_price_var, 1e-12));
    
    // FIXED: Compute return based on velocity, not absolute price comparison
    // This gives us the expected return over the next dt period
    double expected_return = (price_velocity * dt) / current_price;
    
    // FIXED: Add minimum uncertainty to prevent overly narrow distributions
    double min_return_std = MIN_RETURN_STD;
    
    // ENHANCED: Add velocity-dependent uncertainty (higher velocity = more uncertainty)
    double velocity_uncertainty = std::abs(expected_return) * VELOCITY_UNCERTAINTY_FACTOR;
    
    double return_std = std::max(future_price_std / current_price, 
                                 min_return_std + velocity_uncertainty);
    
    // Integrate over bucket boundaries
    hefkf_common::BucketConfidence buckets;
    
    using Bounds = GaussianIntegrator::BucketBounds;
    
    buckets.up_001_002 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_001_002_LOW, Bounds::UP_001_002_HIGH);
    
    buckets.up_002_005 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_002_005_LOW, Bounds::UP_002_005_HIGH);
    
    buckets.up_005_010 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_005_010_LOW, Bounds::UP_005_010_HIGH);
    
    buckets.up_010_plus = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_010_PLUS_LOW, Bounds::UP_010_PLUS_HIGH);
    
    buckets.dn_001_002 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_001_002_LOW, Bounds::DN_001_002_HIGH);
    
    buckets.dn_002_005 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_002_005_LOW, Bounds::DN_002_005_HIGH);
    
    buckets.dn_005_010 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_005_010_LOW, Bounds::DN_005_010_HIGH);
    
    buckets.dn_010_plus = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_010_PLUS_LOW, Bounds::DN_010_PLUS_HIGH);
    
    buckets.normalize();
    return buckets;
}

hefkf_common::BucketConfidence posterior_from_5min_KF(const hefkf_5min::FiveMinuteHEFKF& kf,
                                                      double current_price, 
                                                      double dt) {
    if (!kf.is_initialized()) {
        throw std::invalid_argument("Kalman filter not initialized");
    }
    
    // Get current state and covariance
    const auto& state = kf.get_state();
    const auto& covariance = kf.get_covariance();
    
    // Extract price prediction and uncertainty (same logic as 1min)
    double predicted_price = state(0);
    double price_velocity = state(1);
    double future_price = predicted_price + price_velocity * dt;
    
    // Future price uncertainty
    double price_var = covariance(0, 0);
    double velocity_var = covariance(1, 1);
    double price_velocity_cov = covariance(0, 1);
    
    double future_price_var = price_var + dt * dt * velocity_var + 2.0 * dt * price_velocity_cov;
    double future_price_std = std::sqrt(std::max(future_price_var, 1e-12));
    
    // FIXED: Compute return based on velocity, not absolute price comparison
    // This gives us the expected return over the next dt period
    double expected_return = (price_velocity * dt) / current_price;
    
    // FIXED: Add minimum uncertainty to prevent overly narrow distributions
    double min_return_std = MIN_RETURN_STD;
    
    // ENHANCED: Add velocity-dependent uncertainty (higher velocity = more uncertainty)
    double velocity_uncertainty = std::abs(expected_return) * VELOCITY_UNCERTAINTY_FACTOR;
    
    double return_std = std::max(future_price_std / current_price, 
                                 min_return_std + velocity_uncertainty);
    
    // Integrate over bucket boundaries (same as 1min)
    hefkf_common::BucketConfidence buckets;
    
    using Bounds = GaussianIntegrator::BucketBounds;
    
    buckets.up_001_002 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_001_002_LOW, Bounds::UP_001_002_HIGH);
    
    buckets.up_002_005 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_002_005_LOW, Bounds::UP_002_005_HIGH);
    
    buckets.up_005_010 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_005_010_LOW, Bounds::UP_005_010_HIGH);
    
    buckets.up_010_plus = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::UP_010_PLUS_LOW, Bounds::UP_010_PLUS_HIGH);
    
    buckets.dn_001_002 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_001_002_LOW, Bounds::DN_001_002_HIGH);
    
    buckets.dn_002_005 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_002_005_LOW, Bounds::DN_002_005_HIGH);
    
    buckets.dn_005_010 = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_005_010_LOW, Bounds::DN_005_010_HIGH);
    
    buckets.dn_010_plus = GaussianIntegrator::gaussian_cdf_interval(
        expected_return, return_std, Bounds::DN_010_PLUS_LOW, Bounds::DN_010_PLUS_HIGH);
    
    buckets.normalize();
    return buckets;
}

// ─────────────────────── Template Specializations ───────────────────────
template<>
hefkf_common::BucketConfidence posterior_from_KF<hefkf_1min::OneMinuteHEFKF>(
    const hefkf_1min::OneMinuteHEFKF& kf, 
    double current_price, 
    double dt) {
    return posterior_from_1min_KF(kf, current_price, dt);
}

template<>
hefkf_common::BucketConfidence posterior_from_KF<hefkf_5min::FiveMinuteHEFKF>(
    const hefkf_5min::FiveMinuteHEFKF& kf, 
    double current_price, 
    double dt) {
    return posterior_from_5min_KF(kf, current_price, dt);
}

// ─────────────────────── Dirichlet Sharpening Implementation ───────────────────────
void sharpen_dirichlet(hefkf_common::BucketConfidence& p, double quality_factor) {
    // UPDATED: Reduced alpha scaling from 4.0 to 1.5 for more conservative sharpening
    // Alpha parameter: 1.0 + 1.5 * quality_factor gives range [1, 2.5]
    // quality_factor = 0 → alpha = 1 (no change)
    // quality_factor = 1 → alpha = 2.5 (moderate sharpening, was 5.0)
    double alpha = 1.0 + 1.5 * std::clamp(quality_factor, 0.0, 1.0);
    
    // Debug for tick 1000 issue
    static int sharpen_call_count = 0;
    sharpen_call_count++;
    bool debug_this = (sharpen_call_count >= 995 && sharpen_call_count <= 1005);
    
    if (debug_this) {
        std::cerr << "\n--- sharpen_dirichlet (call " << sharpen_call_count << ") ---" << std::endl;
        std::cerr << "quality_factor: " << quality_factor << ", alpha: " << alpha << std::endl;
        std::cerr << "Input buckets - up: " << (p.up_001_002 + p.up_002_005 + p.up_005_010 + p.up_010_plus) 
                  << ", down: " << (p.dn_001_002 + p.dn_002_005 + p.dn_005_010 + p.dn_010_plus) << std::endl;
    }
    
    // Array of pointers to all bucket probabilities
    double* probs[] = {
        &p.up_001_002, &p.up_002_005, &p.up_005_010, &p.up_010_plus,
        &p.dn_001_002, &p.dn_002_005, &p.dn_005_010, &p.dn_010_plus
    };
    
    // Apply Dirichlet sharpening: p_i = p_i^alpha
    double sum = 0.0;
    for (double* q : probs) {
        if (*q > 1e-12) {  // Avoid pow(0, alpha) issues
            *q = std::pow(*q, alpha);
        } else {
            *q = 0.0;
        }
        sum += *q;
    }
    
    // Renormalize
    if (sum > 1e-12) {
        for (double* q : probs) {
            *q /= sum;
        }
    } else {
        // Fallback: uniform distribution
        for (double* q : probs) {
            *q = 1.0 / 8.0;
        }
    }
    
    if (debug_this) {
        std::cerr << "After sharpening - up: " << (p.up_001_002 + p.up_002_005 + p.up_005_010 + p.up_010_plus) 
                  << ", down: " << (p.dn_001_002 + p.dn_002_005 + p.dn_005_010 + p.dn_010_plus) << std::endl;
    }
    
    // NEW: Cap enhanced probabilities to maintain uncertainty
    // No single bucket should exceed 0.95 to preserve risk modeling capability
    constexpr double MAX_BUCKET_PROB = 0.95;
    
    // Check if any probability exceeds the cap
    bool needs_recapping = false;
    for (double* q : probs) {
        if (*q > MAX_BUCKET_PROB) {
            needs_recapping = true;
            break;
        }
    }
    
    // If capping is needed, redistribute excess probability
    if (needs_recapping) {
        double excess_total = 0.0;
        int capped_count = 0;
        
        // First pass: cap and calculate excess
        for (double* q : probs) {
            if (*q > MAX_BUCKET_PROB) {
                excess_total += (*q - MAX_BUCKET_PROB);
                *q = MAX_BUCKET_PROB;
                capped_count++;
            }
        }
        
        // Second pass: redistribute excess proportionally to uncapped buckets
        if (capped_count < 8 && excess_total > 1e-12) {
            double redistribution_sum = 0.0;
            
            // Calculate sum of uncapped probabilities for proportional redistribution
            for (double* q : probs) {
                if (*q < MAX_BUCKET_PROB - 1e-12) {
                    redistribution_sum += *q;
                }
            }
            
            // Redistribute if possible
            if (redistribution_sum > 1e-12) {
                for (double* q : probs) {
                    if (*q < MAX_BUCKET_PROB - 1e-12) {
                        double share = (*q / redistribution_sum) * excess_total;
                        *q = std::min(*q + share, MAX_BUCKET_PROB);
                    }
                }
            }
        }
        
        // Final normalization to ensure sum = 1
        sum = 0.0;
        for (double* q : probs) {
            sum += *q;
        }
        if (sum > 1e-12) {
            for (double* q : probs) {
                *q /= sum;
            }
        }
    }
}

// ─────────────────────── Closed Loop Integration Helpers ───────────────────────
hefkf_1min::MarketData create_market_data_1min(double price, double volume, double spread,
                                               const hefkf_common::BucketConfidence& bucket_conf,
                                               const hefkf_common::FrequencyFeatures& freq_features) {
    hefkf_1min::MarketData data;
    data.price = price;
    data.volume = volume;
    data.spread = spread;
    data.timestamp = std::chrono::system_clock::now();
    
    // Convert bucket confidence
    data.bucket_conf = std::make_unique<hefkf_1min::BucketConfidence>(bucket_conf.to_1min());
    
    // Convert frequency features
    data.freq_features = std::make_unique<hefkf_1min::FrequencyFeatures>();
    data.freq_features->trend_strength = freq_features.trend_strength;
    data.freq_features->coherence_price_volume_peak = freq_features.coherence_price_volume_peak;
    data.freq_features->coherence_price_spread_peak = freq_features.coherence_price_spread_peak;
    data.freq_features->coherence_price_volume_by_band = freq_features.coherence_price_volume_by_band;
    data.freq_features->coherence_price_spread_by_band = freq_features.coherence_price_spread_by_band;
    
    return data;
}

hefkf_5min::MarketData create_market_data_5min(double price, double volume, double spread,
                                               const hefkf_common::BucketConfidence& bucket_conf,
                                               const hefkf_common::FrequencyFeatures& freq_features) {
    hefkf_5min::MarketData data;
    data.price = price;
    data.volume = volume;
    data.spread = spread;
    data.timestamp = std::chrono::system_clock::now();
    
    // Convert bucket confidence
    data.bucket_conf = std::make_unique<hefkf_5min::BucketConfidence>(bucket_conf.to_5min());
    
    // Convert frequency features
    data.freq_features = std::make_unique<hefkf_5min::FrequencyFeatures>();
    data.freq_features->trend_strength = freq_features.trend_strength;
    data.freq_features->coherence_price_volume_peak = freq_features.coherence_price_volume_peak;
    data.freq_features->coherence_price_spread_peak = freq_features.coherence_price_spread_peak;
    data.freq_features->coherence_price_volume_by_band = freq_features.coherence_price_volume_by_band;
    data.freq_features->coherence_price_spread_by_band = freq_features.coherence_price_spread_by_band;
    
    return data;
} 