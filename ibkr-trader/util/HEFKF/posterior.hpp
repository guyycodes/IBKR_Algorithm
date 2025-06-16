// Posterior Bucket Probability Extraction from Kalman Filters
// Implements Gaussian integration over bucket boundaries and Dirichlet sharpening
// Works with both 1min and 5min HEFKF filters using template functions

#ifndef POSTERIOR_HPP
#define POSTERIOR_HPP

#include "1min_HEFKF.hpp"
#include "5min_HEFKF.hpp"
#include "frequency_analyser.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <string>
#include <utility>

// ─────────────────────── 20-Bucket System Structures ───────────────────────
namespace hefkf_common {

// Bucket type and assignment from Phase 1
enum BucketType { UP, DOWN, NEUTRAL };

struct BucketAssignment {
    BucketType type;
    int index;  // 0-9 representing 000-090
    
    std::string to_string() const {
        if (type == NEUTRAL && index == 0) return "neutral";
        return (type == UP ? "up_" : "dn_") + 
               std::to_string(index * 10).insert(0, 3 - std::to_string(index * 10).length(), '0');
    }
};

// Phase 2: New 20-bucket confidence structure
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
    
    void normalize();
    bool is_valid() const;
    
    // Array access for algorithmic processing
    double& operator[](const BucketAssignment& ba);
    double operator[](const BucketAssignment& ba) const;
};

// Bucket boundary definitions for 20-bucket system
struct BucketBounds20 {
    // UP bucket boundaries (in fractional returns)
    static constexpr double UP_000_LOW = -0.00125;   // -0.125%
    static constexpr double UP_000_HIGH = 0.00125;   // +0.125%
    static constexpr double UP_010_LOW = 0.00125;    // +0.125%
    static constexpr double UP_010_HIGH = 0.00375;   // +0.375%
    static constexpr double UP_020_LOW = 0.00375;    // +0.375%
    static constexpr double UP_020_HIGH = 0.00625;   // +0.625%
    static constexpr double UP_030_LOW = 0.00625;    // +0.625%
    static constexpr double UP_030_HIGH = 0.00875;   // +0.875%
    static constexpr double UP_040_LOW = 0.00875;    // +0.875%
    static constexpr double UP_040_HIGH = 0.01125;   // +1.125%
    static constexpr double UP_050_LOW = 0.01125;    // +1.125%
    static constexpr double UP_050_HIGH = 0.01375;   // +1.375%
    static constexpr double UP_060_LOW = 0.01375;    // +1.375%
    static constexpr double UP_060_HIGH = 0.01625;   // +1.625%
    static constexpr double UP_070_LOW = 0.01625;    // +1.625%
    static constexpr double UP_070_HIGH = 0.01875;   // +1.875%
    static constexpr double UP_080_LOW = 0.01875;    // +1.875%
    static constexpr double UP_080_HIGH = 0.02125;   // +2.125%
    static constexpr double UP_090_LOW = 0.02125;    // +2.125%
    static constexpr double UP_090_HIGH = 0.05000;   // +5.000% (practical cap)
    
    // DOWN bucket boundaries (mirror of UP)
    static constexpr double DN_000_LOW = -0.00125;   // -0.125%
    static constexpr double DN_000_HIGH = 0.00125;   // +0.125%
    static constexpr double DN_010_LOW = -0.00375;   // -0.375%
    static constexpr double DN_010_HIGH = -0.00125;  // -0.125%
    static constexpr double DN_020_LOW = -0.00625;   // -0.625%
    static constexpr double DN_020_HIGH = -0.00375;  // -0.375%
    static constexpr double DN_030_LOW = -0.00875;   // -0.875%
    static constexpr double DN_030_HIGH = -0.00625;  // -0.625%
    static constexpr double DN_040_LOW = -0.01125;   // -1.125%
    static constexpr double DN_040_HIGH = -0.00875;  // -0.875%
    static constexpr double DN_050_LOW = -0.01375;   // -1.375%
    static constexpr double DN_050_HIGH = -0.01125;  // -1.125%
    static constexpr double DN_060_LOW = -0.01625;   // -1.625%
    static constexpr double DN_060_HIGH = -0.01375;  // -1.375%
    static constexpr double DN_070_LOW = -0.01875;   // -1.875%
    static constexpr double DN_070_HIGH = -0.01625;  // -1.625%
    static constexpr double DN_080_LOW = -0.02125;   // -2.125%
    static constexpr double DN_080_HIGH = -0.01875;  // -1.875%
    static constexpr double DN_090_LOW = -0.05000;   // -5.000% (practical cap)
    static constexpr double DN_090_HIGH = -0.02125;  // -2.125%
    
    // Helper to get bounds by bucket assignment
    static std::pair<double, double> get_bounds(const BucketAssignment& ba);
};


// ─────────────────────── 20-Bucket Functions ───────────────────────
// Posterior extraction for 20-bucket system
BucketConfidence20 posterior_from_KF_20bucket(
    const hefkf_1min::OneMinuteHEFKF& kf,
    double current_price, 
    double dt);

BucketConfidence20 posterior_from_5min_KF_20bucket(
    const hefkf_5min::FiveMinuteHEFKF& kf,
    double current_price, 
    double dt);

} // namespace hefkf_common
// Dirichlet sharpening for 20-bucket system
void sharpen_dirichlet_20bucket(hefkf_common::BucketConfidence20& p, double quality_factor);

// Bucket assignment function
hefkf_common::BucketAssignment assign_bucket(double return_pct);

// ─────────────────────── Gaussian Integration Helpers ───────────────────────
class GaussianIntegrator {
public:
    // Compute probability that Gaussian random variable X falls in [a, b]
    static double gaussian_cdf_interval(double mean, double std_dev, double a, double b);
    
    // Standard normal CDF approximation
    static double standard_normal_cdf(double x);
    
    static constexpr double INV_SQRT_2PI = 0.3989422804014327;  // 1/sqrt(2π)
    static constexpr double SQRT_2 = 1.4142135623730951;        // sqrt(2)
};

// ─────────────────────── Closed Loop Integration Helpers ───────────────────────

// Create MarketData with bucket confidence for 1min filter (20-bucket)
hefkf_1min::MarketData create_market_data_1min_20bucket(double price, double volume, double spread,
                                               const hefkf_common::BucketConfidence20& bucket_conf,
                                               const hefkf_common::FrequencyFeatures& freq_features);

// Create MarketData with bucket confidence for 5min filter (20-bucket)
hefkf_5min::MarketData create_market_data_5min_20bucket(double price, double volume, double spread,
                                               const hefkf_common::BucketConfidence20& bucket_conf,
                                               const hefkf_common::FrequencyFeatures& freq_features);

#endif // POSTERIOR_HPP 