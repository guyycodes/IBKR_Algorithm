// Posterior 20 Bucket Probability Extraction Implementation
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
    // Set to ensure proper bucket discrimination for 25bp increments
    constexpr double MIN_RETURN_STD = 0.0002;  // 0.02% (2 basis points) - allows bucket discrimination
    
    // Velocity-dependent uncertainty factor
    // Adds uncertainty proportional to the expected move size
    // Rationale: Larger/faster moves are inherently less certain
    // Range: 0.3 (standard) to 2.0 (high uncertainty for extreme moves)
    constexpr double VELOCITY_UNCERTAINTY_FACTOR = 0.9;  // Keep at 0.9-1.0 for proper risk modeling
    
    // Maximum return for bucket integration (caps extreme tails)
    // Prevents numerical issues with very large moves
    constexpr double MAX_RETURN_FOR_BUCKETS = 0.05;  // 5% max single-period return
}

// ─────────────────────── Common namespace ───────────────────────
namespace hefkf_common {

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
    
    // Save the sign of x
    int sign = (x >= 0) ? 1 : -1;
    x = std::abs(x) / SQRT_2;
    
    // Abramowitz and Stegun approximation formula 7.1.26 for erf
    double t = 1.0 / (1.0 + p * x);
    double y = (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * std::exp(-x * x);
    
    // CDF = 0.5 * (1 + erf(x/sqrt(2)))
    return 0.5 * (1.0 + sign * (1.0 - y));
}

// ─────────────────────── hefkf_common namespace continues ───────────────────────
namespace hefkf_common {

// ─────────────────────── 20-Bucket Posterior Extraction ───────────────────────
BucketConfidence20 posterior_from_KF_20bucket(
    const hefkf_1min::OneMinuteHEFKF& kf,
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
    double price_var = covariance(0, 0);
    double velocity_var = covariance(1, 1);
    double price_velocity_cov = covariance(0, 1);
    
    double future_price_var = price_var + dt * dt * velocity_var + 2.0 * dt * price_velocity_cov;
    double future_price_std = std::sqrt(std::max(future_price_var, 1e-12));
    
    // Compute return based on velocity
    double expected_return = (price_velocity * dt) / current_price;
    
    // Add minimum uncertainty to prevent overly narrow distributions
    double min_return_std = MIN_RETURN_STD;
    
    // Add velocity-dependent uncertainty
    double velocity_uncertainty = std::abs(expected_return) * VELOCITY_UNCERTAINTY_FACTOR;
    
    double return_std = std::max(future_price_std / current_price, 
                                 min_return_std + velocity_uncertainty);
    
    // Integrate over all 20 buckets
    BucketConfidence20 buckets;
    
//----------------- Gaussian integration over bucket boundaries -----------------
    // Process all buckets using the assignment system
    for (int i = 0; i <= 9; ++i) {
        // UP buckets
        BucketAssignment ba_up{UP, i};
        auto [low, high] = BucketBounds20::get_bounds(ba_up);
        buckets[ba_up] = GaussianIntegrator::gaussian_cdf_interval(
            expected_return, return_std, low, high);
        
        // DOWN buckets  
        BucketAssignment ba_dn{DOWN, i};
        auto [dn_low, dn_high] = BucketBounds20::get_bounds(ba_dn);
        buckets[ba_dn] = GaussianIntegrator::gaussian_cdf_interval(
            expected_return, return_std, dn_low, dn_high);
    }
    
    buckets.normalize();
    return buckets;
}

// 5-minute version
BucketConfidence20 posterior_from_5min_KF_20bucket(
    const hefkf_5min::FiveMinuteHEFKF& kf,
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
    
    // Compute return based on velocity
    double expected_return = (price_velocity * dt) / current_price;
    
    // Add minimum uncertainty
    double min_return_std = MIN_RETURN_STD;
    
    // Add velocity-dependent uncertainty
    double velocity_uncertainty = std::abs(expected_return) * VELOCITY_UNCERTAINTY_FACTOR;
    
    double return_std = std::max(future_price_std / current_price, 
                                 min_return_std + velocity_uncertainty);
    
    // Integrate over all 20 buckets
    BucketConfidence20 buckets;
    
    // Process all buckets
    for (int i = 0; i <= 9; ++i) {
        // UP buckets
        BucketAssignment ba_up{UP, i};
        auto [low, high] = BucketBounds20::get_bounds(ba_up);
        buckets[ba_up] = GaussianIntegrator::gaussian_cdf_interval(
            expected_return, return_std, low, high);
        
        // DOWN buckets  
        BucketAssignment ba_dn{DOWN, i};
        auto [dn_low, dn_high] = BucketBounds20::get_bounds(ba_dn);
        buckets[ba_dn] = GaussianIntegrator::gaussian_cdf_interval(
            expected_return, return_std, dn_low, dn_high);
    }
    
    buckets.normalize();
    return buckets;
}


// ─────────────────────── Closed Loop Integration Helpers (20-Bucket) ───────────────────────
hefkf_1min::MarketData create_market_data_1min_20bucket(double price, double volume, double spread,
                                               const BucketConfidence20& bucket_conf,
                                               const FrequencyFeatures& freq_features) {
    hefkf_1min::MarketData data;
    data.price = price;
    data.volume = volume;
    data.spread = spread;
    data.timestamp = std::chrono::system_clock::now();
    
    // Convert bucket confidence 20 to 1min format
    data.bucket_conf = std::make_unique<hefkf_1min::BucketConfidence20>();
    auto& bc = *data.bucket_conf;
    
    // Copy all 20 buckets
    bc.up_000 = bucket_conf.up_000;
    bc.up_010 = bucket_conf.up_010;
    bc.up_020 = bucket_conf.up_020;
    bc.up_030 = bucket_conf.up_030;
    bc.up_040 = bucket_conf.up_040;
    bc.up_050 = bucket_conf.up_050;
    bc.up_060 = bucket_conf.up_060;
    bc.up_070 = bucket_conf.up_070;
    bc.up_080 = bucket_conf.up_080;
    bc.up_090 = bucket_conf.up_090;
    
    bc.dn_000 = bucket_conf.dn_000;
    bc.dn_010 = bucket_conf.dn_010;
    bc.dn_020 = bucket_conf.dn_020;
    bc.dn_030 = bucket_conf.dn_030;
    bc.dn_040 = bucket_conf.dn_040;
    bc.dn_050 = bucket_conf.dn_050;
    bc.dn_060 = bucket_conf.dn_060;
    bc.dn_070 = bucket_conf.dn_070;
    bc.dn_080 = bucket_conf.dn_080;
    bc.dn_090 = bucket_conf.dn_090;
    
    // Convert frequency features
    data.freq_features = std::make_unique<hefkf_1min::FrequencyFeatures>();
    data.freq_features->trend_strength = freq_features.trend_strength;
    data.freq_features->coherence_price_volume_peak = freq_features.coherence_price_volume_peak;
    data.freq_features->coherence_price_spread_peak = freq_features.coherence_price_spread_peak;
    data.freq_features->coherence_price_volume_by_band = freq_features.coherence_price_volume_by_band;
    data.freq_features->coherence_price_spread_by_band = freq_features.coherence_price_spread_by_band;
    
    return data;
}

hefkf_5min::MarketData create_market_data_5min_20bucket(double price, double volume, double spread,
                                               const BucketConfidence20& bucket_conf,
                                               const FrequencyFeatures& freq_features) {
    hefkf_5min::MarketData data;
    data.price = price;
    data.volume = volume;
    data.spread = spread;
    data.timestamp = std::chrono::system_clock::now();
    
    // Convert bucket confidence 20 to 5min format
    data.bucket_conf = std::make_unique<hefkf_5min::BucketConfidence20>();
    auto& bc = *data.bucket_conf;
    
    // Copy all 20 buckets
    bc.up_000 = bucket_conf.up_000;
    bc.up_010 = bucket_conf.up_010;
    bc.up_020 = bucket_conf.up_020;
    bc.up_030 = bucket_conf.up_030;
    bc.up_040 = bucket_conf.up_040;
    bc.up_050 = bucket_conf.up_050;
    bc.up_060 = bucket_conf.up_060;
    bc.up_070 = bucket_conf.up_070;
    bc.up_080 = bucket_conf.up_080;
    bc.up_090 = bucket_conf.up_090;
    
    bc.dn_000 = bucket_conf.dn_000;
    bc.dn_010 = bucket_conf.dn_010;
    bc.dn_020 = bucket_conf.dn_020;
    bc.dn_030 = bucket_conf.dn_030;
    bc.dn_040 = bucket_conf.dn_040;
    bc.dn_050 = bucket_conf.dn_050;
    bc.dn_060 = bucket_conf.dn_060;
    bc.dn_070 = bucket_conf.dn_070;
    bc.dn_080 = bucket_conf.dn_080;
    bc.dn_090 = bucket_conf.dn_090;
    
    // Convert frequency features
    data.freq_features = std::make_unique<hefkf_5min::FrequencyFeatures>();
    data.freq_features->trend_strength = freq_features.trend_strength;
    data.freq_features->coherence_price_volume_peak = freq_features.coherence_price_volume_peak;
    data.freq_features->coherence_price_spread_peak = freq_features.coherence_price_spread_peak;
    data.freq_features->coherence_price_volume_by_band = freq_features.coherence_price_volume_by_band;
    data.freq_features->coherence_price_spread_by_band = freq_features.coherence_price_spread_by_band;
    
    return data;
}

// ─────────────────────── 20-Bucket Dirichlet Sharpening ───────────────────────
void sharpen_dirichlet_20bucket(BucketConfidence20& p, double quality_factor) {
    // Alpha parameter: 1.0 + 1.5 * quality_factor gives range [1, 2.5]
    double alpha = 1.0 + 1.5 * std::clamp(quality_factor, 0.0, 1.0);
    
    // Array of all bucket probabilities (20 total)
    double* probs[] = {
        &p.up_000, &p.up_010, &p.up_020, &p.up_030, &p.up_040,
        &p.up_050, &p.up_060, &p.up_070, &p.up_080, &p.up_090,
        &p.dn_000, &p.dn_010, &p.dn_020, &p.dn_030, &p.dn_040,
        &p.dn_050, &p.dn_060, &p.dn_070, &p.dn_080, &p.dn_090
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
            *q = 1.0 / 20.0;
        }
    }
    
    // Cap enhanced probabilities to maintain uncertainty
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
        if (capped_count < 20 && excess_total > 1e-12) {
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

// ─────────────────────── 20-Bucket System Implementation ───────────────────────

// BucketConfidence20 normalize implementation
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

// Array access operators
double& BucketConfidence20::operator[](const BucketAssignment& ba) {
    if (ba.type == UP) {
        switch (ba.index) {
            case 0: return up_000;
            case 1: return up_010;
            case 2: return up_020;
            case 3: return up_030;
            case 4: return up_040;
            case 5: return up_050;
            case 6: return up_060;
            case 7: return up_070;
            case 8: return up_080;
            case 9: return up_090;
            default: throw std::out_of_range("Invalid UP bucket index");
        }
    } else if (ba.type == DOWN) {
        switch (ba.index) {
            case 0: return dn_000;
            case 1: return dn_010;
            case 2: return dn_020;
            case 3: return dn_030;
            case 4: return dn_040;
            case 5: return dn_050;
            case 6: return dn_060;
            case 7: return dn_070;
            case 8: return dn_080;
            case 9: return dn_090;
            default: throw std::out_of_range("Invalid DOWN bucket index");
        }
    } else {  // NEUTRAL
        if (ba.index == 0) {
            // For neutral, return average of up_000 and dn_000
            static double neutral_temp;
            neutral_temp = (up_000 + dn_000) / 2.0;
            return neutral_temp;
        }
        throw std::out_of_range("Invalid NEUTRAL bucket index");
    }
}

double BucketConfidence20::operator[](const BucketAssignment& ba) const {
    if (ba.type == UP) {
        switch (ba.index) {
            case 0: return up_000;
            case 1: return up_010;
            case 2: return up_020;
            case 3: return up_030;
            case 4: return up_040;
            case 5: return up_050;
            case 6: return up_060;
            case 7: return up_070;
            case 8: return up_080;
            case 9: return up_090;
            default: throw std::out_of_range("Invalid UP bucket index");
        }
    } else if (ba.type == DOWN) {
        switch (ba.index) {
            case 0: return dn_000;
            case 1: return dn_010;
            case 2: return dn_020;
            case 3: return dn_030;
            case 4: return dn_040;
            case 5: return dn_050;
            case 6: return dn_060;
            case 7: return dn_070;
            case 8: return dn_080;
            case 9: return dn_090;
            default: throw std::out_of_range("Invalid DOWN bucket index");
        }
    } else {  // NEUTRAL
        if (ba.index == 0) {
            return (up_000 + dn_000) / 2.0;
        }
        throw std::out_of_range("Invalid NEUTRAL bucket index");
    }
}

// BucketBounds20 helper implementation
std::pair<double, double> BucketBounds20::get_bounds(const BucketAssignment& ba) {
    if (ba.type == UP) {
        switch (ba.index) {
            case 0: return {UP_000_LOW, UP_000_HIGH};
            case 1: return {UP_010_LOW, UP_010_HIGH};
            case 2: return {UP_020_LOW, UP_020_HIGH};
            case 3: return {UP_030_LOW, UP_030_HIGH};
            case 4: return {UP_040_LOW, UP_040_HIGH};
            case 5: return {UP_050_LOW, UP_050_HIGH};
            case 6: return {UP_060_LOW, UP_060_HIGH};
            case 7: return {UP_070_LOW, UP_070_HIGH};
            case 8: return {UP_080_LOW, UP_080_HIGH};
            case 9: return {UP_090_LOW, UP_090_HIGH};
            default: throw std::out_of_range("Invalid UP bucket index");
        }
    } else if (ba.type == DOWN) {
        switch (ba.index) {
            case 0: return {DN_000_LOW, DN_000_HIGH};
            case 1: return {DN_010_LOW, DN_010_HIGH};
            case 2: return {DN_020_LOW, DN_020_HIGH};
            case 3: return {DN_030_LOW, DN_030_HIGH};
            case 4: return {DN_040_LOW, DN_040_HIGH};
            case 5: return {DN_050_LOW, DN_050_HIGH};
            case 6: return {DN_060_LOW, DN_060_HIGH};
            case 7: return {DN_070_LOW, DN_070_HIGH};
            case 8: return {DN_080_LOW, DN_080_HIGH};
            case 9: return {DN_090_LOW, DN_090_HIGH};
            default: throw std::out_of_range("Invalid DOWN bucket index");
        }
    } else {  // NEUTRAL  
        return {-0.00125, 0.00125};  // ±0.125%
    }
}

// Phase 1 bucket assignment implementation
BucketAssignment assign_bucket(double return_pct) {
    const double NEUTRAL_THRESHOLD = 0.125;  // ±0.125% is neutral
    
    if (std::abs(return_pct) < NEUTRAL_THRESHOLD) {
        return {NEUTRAL, 0};  // Maps to up_000 or dn_000
    }
    
    BucketType type = (return_pct > 0) ? UP : DOWN;
    double abs_return = std::abs(return_pct);
    
    // Calculate bucket index based on distance from neutral zone
    int index = static_cast<int>((abs_return + 0.125) / 0.25);
    
    // Cap at boundary bucket
    if (index > 9) index = 9;
    
    return {type, index};
}

} // namespace hefkf_common 