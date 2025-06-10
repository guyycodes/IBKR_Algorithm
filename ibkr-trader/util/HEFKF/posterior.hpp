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

// ─────────────────────── Common Bucket Confidence ───────────────────────
namespace hefkf_common {

// Bucket confidence structure that works with both filter types
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
    
    // Convert to 1min namespace bucket confidence
    hefkf_1min::BucketConfidence to_1min() const;
    
    // Convert to 5min namespace bucket confidence  
    hefkf_5min::BucketConfidence to_5min() const;
    
    // Create from 1min namespace
    static BucketConfidence from_1min(const hefkf_1min::BucketConfidence& bc);
    
    // Create from 5min namespace
    static BucketConfidence from_5min(const hefkf_5min::BucketConfidence& bc);
};

} // namespace hefkf_common

// ─────────────────────── Posterior Extraction Functions ───────────────────────

// Template function that works with both 1min and 5min filters
template<typename FilterType>
hefkf_common::BucketConfidence posterior_from_KF(const FilterType& kf, 
                                                 double current_price, 
                                                 double dt);

// Specialized implementations for each filter type
hefkf_common::BucketConfidence posterior_from_1min_KF(const hefkf_1min::OneMinuteHEFKF& kf,
                                                      double current_price, 
                                                      double dt);

hefkf_common::BucketConfidence posterior_from_5min_KF(const hefkf_5min::FiveMinuteHEFKF& kf,
                                                      double current_price, 
                                                      double dt);

// ─────────────────────── Dirichlet Sharpening ───────────────────────
void sharpen_dirichlet(hefkf_common::BucketConfidence& p, double quality_factor);

// ─────────────────────── Gaussian Integration Helpers ───────────────────────
class GaussianIntegrator {
public:
    // Bucket boundary definitions (return percentages)
    struct BucketBounds {
        static constexpr double UP_001_002_LOW = 0.001;    // 0.1%
        static constexpr double UP_001_002_HIGH = 0.002;   // 0.2%
        static constexpr double UP_002_005_LOW = 0.002;    // 0.2%
        static constexpr double UP_002_005_HIGH = 0.005;   // 0.5%
        static constexpr double UP_005_010_LOW = 0.005;    // 0.5%
        static constexpr double UP_005_010_HIGH = 0.010;   // 1.0%
        static constexpr double UP_010_PLUS_LOW = 0.010;   // 1.0%
        static constexpr double UP_010_PLUS_HIGH = 0.050;  // 5.0% (practical upper bound)
        
        // Negative buckets (mirror image)
        static constexpr double DN_001_002_LOW = -0.002;   // -0.2%
        static constexpr double DN_001_002_HIGH = -0.001;  // -0.1%
        static constexpr double DN_002_005_LOW = -0.005;   // -0.5%
        static constexpr double DN_002_005_HIGH = -0.002;  // -0.2%
        static constexpr double DN_005_010_LOW = -0.010;   // -1.0%
        static constexpr double DN_005_010_HIGH = -0.005;  // -0.5%
        static constexpr double DN_010_PLUS_LOW = -0.050;  // -5.0% (practical lower bound)
        static constexpr double DN_010_PLUS_HIGH = -0.010; // -1.0%
    };
    
    // Compute probability that Gaussian random variable X falls in [a, b]
    static double gaussian_cdf_interval(double mean, double std_dev, double a, double b);
    
    // Standard normal CDF approximation
    static double standard_normal_cdf(double x);
    
private:
    static constexpr double INV_SQRT_2PI = 0.3989422804014327;  // 1/sqrt(2π)
    static constexpr double SQRT_2 = 1.4142135623730951;        // sqrt(2)
};

// ─────────────────────── Closed Loop Integration Helpers ───────────────────────

// Create MarketData with bucket confidence for 1min filter
hefkf_1min::MarketData create_market_data_1min(double price, double volume, double spread,
                                               const hefkf_common::BucketConfidence& bucket_conf,
                                               const hefkf_common::FrequencyFeatures& freq_features);

// Create MarketData with bucket confidence for 5min filter  
hefkf_5min::MarketData create_market_data_5min(double price, double volume, double spread,
                                               const hefkf_common::BucketConfidence& bucket_conf,
                                               const hefkf_common::FrequencyFeatures& freq_features);

// ─────────────────────── Template Specializations ───────────────────────

// Template specialization for 1min filter
template<>
hefkf_common::BucketConfidence posterior_from_KF<hefkf_1min::OneMinuteHEFKF>(
    const hefkf_1min::OneMinuteHEFKF& kf, 
    double current_price, 
    double dt);

// Template specialization for 5min filter
template<>
hefkf_common::BucketConfidence posterior_from_KF<hefkf_5min::FiveMinuteHEFKF>(
    const hefkf_5min::FiveMinuteHEFKF& kf, 
    double current_price, 
    double dt);

#endif // POSTERIOR_HPP 