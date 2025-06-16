// AnalyticScorer - Confidence Engine for Regime-Aware Bucket Probability Enhancement
// Transforms FrequencyFeatures into sharpening factors and directional biases
// Keeps Kalman core untouched while adding sophisticated frequency-domain insights

#ifndef ANALYTIC_SCORER_HPP
#define ANALYTIC_SCORER_HPP

#include "frequency_analyser.hpp"
#include "posterior.hpp"
#include <unordered_map>
#include <string>
#include <array>
#include <algorithm>

// ─────────────────────── Configuration Constants ───────────────────────

namespace AnalyticScorerConstants {

// ──── Filter Base Parameters ────
// Base parameters for the Kalman filter knobs that can be adjusted by the analytic scorer
constexpr double BASE_BUCKET_WEIGHT = 0.50;              // Base u-channel gain (bucket confidence weight)
constexpr double BASE_FREQUENCY_WEIGHT = 0.30;           // Base frequency domain weight
constexpr double BASE_LAMBDA_FIXED = 0.95;               // Base forgetting factor (lower = shorter memory)

// ──── Normalization Parameters ────
// Scale factors for normalizing spectral features to [0, 1] range
constexpr double DEFAULT_TREND_SCALE = 0.5;              // 95th percentile for trend_strength (returns-based)
constexpr double DEFAULT_FLUX_SCALE = 0.00005;           // 95th percentile for spectral_flux (returns-based)
constexpr double DEFAULT_DERIVATIVE_SENSITIVITY = 5.0;   // Scaling for derivative tanh mapping
constexpr double TANH_NORM_SCALE = 5.0;                 // Default scale for tanh normalization

// ──── Feature Processing Constants ────
// Constants for feature dampening and normalization
constexpr double DAMPENING_POWER = 1.5;                  // Power function exponent for feature dampening (x^1.5)
constexpr double MAX_FEATURE_VALUE = 0.9;                // Maximum allowed value after clamping
constexpr double CENTROID_NORMALIZATION_FACTOR = 2.0;    // Factor to normalize spectral centroids from [0, 0.5] to [0, 1]

// ──── Regime Detection Thresholds ────
// Thresholds for classifying market regimes based on normalized features
constexpr double TREND_STRONG_THRESHOLD = 0.7;           // Threshold for strong trend detection
constexpr double TREND_WEAK_THRESHOLD = 0.3;             // Threshold for weak trend detection
constexpr double TREND_REVERSAL_THRESHOLD = 0.15;        // Price velocity threshold for trend/reversal detection
constexpr double DERIVATIVE_HIGH_THRESHOLD = 0.6;        // High derivative threshold
constexpr double DERIVATIVE_LOW_THRESHOLD = 0.4;         // Low derivative threshold
constexpr double FLUX_HIGH_THRESHOLD = 0.7;              // High spectral flux threshold
constexpr double FLUX_LOW_THRESHOLD = 0.3;               // Low spectral flux threshold
constexpr double COHERENCE_HIGH_THRESHOLD = 0.8;         // High coherence threshold
constexpr double CENTROID_VELOCITY_HIGH = 0.7;           // High centroid velocity threshold
constexpr double DERIVATIVE_REVERSAL_BAND = 0.3;         // Band around 0.5 for reversal detection

// ──── Sharpening Factor Constants ────
// Parameters controlling Dirichlet sharpening of bucket probabilities
constexpr double BASE_SHARPENING_MULTIPLIER = 1.5;       // Base multiplier for signal quality impact
constexpr double MIN_SHARPENING_FACTOR = 1.0;            // Minimum sharpening (no change)
constexpr double MAX_SHARPENING_FACTOR = 2.5;            // Maximum sharpening (conservative cap)

// Regime-specific sharpening multipliers
constexpr double BULL_SHARPENING_MULT = 1.1;             // Bull trend sharpening multiplier
constexpr double BEAR_SHARPENING_MULT = 1.1;             // Bear trend sharpening multiplier
constexpr double HIGH_VOL_SHARPENING_MULT = 0.85;        // High volatility dampening
constexpr double LOW_VOL_SHARPENING_MULT = 1.05;         // Low volatility slight boost
constexpr double REVERSAL_SHARPENING_MULT = 0.9;         // Reversal regime dampening
constexpr double BREAKOUT_SHARPENING_MULT = 1.15;        // Breakout regime boost

// ──── Directional Bias Constants ────
// Parameters for directional bias in bull/bear trends
constexpr double BULL_UP_BIAS_FACTOR = 0.3;              // Factor for up bias in bull trend
constexpr double BULL_DOWN_BIAS_FACTOR = 0.2;            // Factor for down bias in bull trend
constexpr double BEAR_DOWN_BIAS_FACTOR = 0.3;            // Factor for down bias in bear trend
constexpr double BEAR_UP_BIAS_FACTOR = 0.2;              // Factor for up bias in bear trend

// Move size bias parameters
constexpr double MOVE_BIAS_BASE = 0.05;                  // Base bias for small moves (5%)
constexpr double MOVE_BIAS_SCALE = 0.10;                 // Additional bias scaling with move size (10%)

// ──── Filter Adjustment Constants ────
// Parameters for dynamic filter knob adjustments based on market conditions
constexpr double QUALITY_WEIGHT_BOOST_1MIN = 0.20;       // Max bucket weight boost for 1-min filter
constexpr double QUALITY_WEIGHT_BOOST_5MIN = 0.15;       // Max bucket weight boost for 5-min filter
constexpr double VOLATILITY_WEIGHT_REDUCTION = -0.10;    // Max bucket weight reduction in high volatility
constexpr double QUALITY_FREQ_BOOST = 0.10;              // Max frequency weight boost
constexpr double VOLATILITY_FREQ_REDUCTION = -0.05;      // Max frequency weight reduction
constexpr double QUALITY_LAMBDA_REDUCTION = -0.05;       // Max lambda reduction (faster adaptation)
constexpr double VOLATILITY_LAMBDA_REDUCTION = -0.10;    // Max lambda reduction in high volatility

// ──── Enhanced Signal Thresholds ────
// Thresholds for computing enhanced quality and volatility signals
constexpr double QUALITY_COHERENCE_WEIGHT = 0.3;         // Weight for coherence in quality signal
constexpr double QUALITY_TREND_WEIGHT = 0.2;             // Weight for trend in quality signal
constexpr double QUALITY_BAND_WEIGHT = 0.3;              // Weight for band coherence in quality signal
constexpr double QUALITY_DERIVATIVE_WEIGHT = 0.2;        // Weight for derivatives in quality signal

constexpr double VOLATILITY_FLUX_WEIGHT = 0.3;           // Weight for spectral flux in volatility signal
constexpr double VOLATILITY_ENTROPY_WEIGHT = 0.3;        // Weight for entropy in volatility signal
constexpr double VOLATILITY_CENTROID_WEIGHT = 0.2;       // Weight for centroid velocity in volatility signal
constexpr double VOLATILITY_DERIVATIVE_WEIGHT = 0.2;     // Weight for derivative volatility in volatility signal

} // namespace AnalyticScorerConstants

// ─────────────────────── Market Regime Classification ───────────────────────
enum class MarketRegime {
    BULL_TREND,      // Strong upward trend, low volatility
    BEAR_TREND,      // Strong downward trend, low volatility  
    HIGH_VOLATILITY, // High volatility, directionless
    LOW_VOLATILITY,  // Low volatility, range-bound
    REVERSAL,        // Mean-reverting conditions
    BREAKOUT,        // Momentum breakout conditions
    UNKNOWN          // Insufficient data or mixed signals
};

// ─────────────────────── Simplified 3-State Regime Classification ───────────────────────
enum class Regime { 
    Bull,     // Upward trending market
    Bear,     // Downward trending market  
    Sideways  // Range-bound/non-trending market
};

// Simple regime classification based on price velocity and trend strength
Regime classify_regime(double price_vel, double trend_strength);

// ─────────────────────── Normalization Utilities ───────────────────────
namespace normalization {

// Core normalization functions
inline double clamp01(double x) { 
    return std::min(1.0, std::max(0.0, x)); 
}

inline double zscore(double x, double μ, double σ) { 
    return (σ > 1e-9) ? (x - μ) / σ : 0.0; 
}

inline double logistic(double z) { 
    return 1.0 / (1.0 + std::exp(-z)); 
}

// Symmetric mapping for derivatives: [-inf, inf] → [0, 1]
inline double tanh_norm(double x, double scale = AnalyticScorerConstants::TANH_NORM_SCALE) {
    return 0.5 + 0.5 * std::tanh(scale * x);
}

} // namespace normalization

// ─────────────────────── Regime-Specific Weight Tables ───────────────────────
struct Weights {
    double w_coher;   // Coherence peak weight
    double w_trend;   // Trend strength weight
    double w_flux;    // Spectral flux weight
    double w_entropy; // Entropy weight
    double w_deriv;   // Derivatives weight
    
    // Constructor for easy initialization
    Weights(double coher, double trend, double flux, double entropy, double deriv)
        : w_coher(coher), w_trend(trend), w_flux(flux), w_entropy(entropy), w_deriv(deriv) {}
        
    // Default constructor
    Weights() : w_coher(0.0), w_trend(0.0), w_flux(0.0), w_entropy(0.0), w_deriv(0.0) {}
};

// Static weight tables for different regimes
extern const Weights W_BULL;     // Bull market weights
extern const Weights W_BEAR;     // Bear market weights  
extern const Weights W_SIDE;     // Sideways market weights

// ─────────────────────── Normalized Feature Set ───────────────────────
struct NormalizedFeatures {
    // Core spectral features [0, 1]
    double trend = 0.0;           // Normalized trend strength
    double flux = 0.0;            // Normalized spectral flux
    double coher_pv = 0.0;        // Price-volume coherence peak
    double coher_ps = 0.0;        // Price-spread coherence peak
    
    // Spectral centroids [0, 1]
    double centroid_price = 0.0;
    double centroid_volume = 0.0;
    
    // Band-specific entropy [0, 1]
    double entropy_micro = 0.0;    // Microstructure band
    double entropy_short = 0.0;    // Short-term band
    double entropy_medium = 0.0;   // Medium-term band
    double entropy_trend = 0.0;    // Trend band
    
    // Band-specific coherence [0, 1]
    double coher_pv_micro = 0.0;
    double coher_pv_short = 0.0;
    double coher_pv_medium = 0.0;
    double coher_pv_trend = 0.0;
    
    // Derivative features [0, 1] (momentum indicators)
    double d_trend = 0.0;         // Trend strength derivative
    double d_coher_pv = 0.0;      // Coherence derivative
    double centroid_velocity = 0.0; // Spectral centroid velocity
};

// ─────────────────────── Directional Bias Vector ───────────────────────
struct DirectionalBias {
    // Multiplicative factors for up vs down buckets
    double up_bias = 1.0;         // Multiplier for all up_* buckets
    double down_bias = 1.0;       // Multiplier for all dn_* buckets
    
    // Magnitude-specific biases (optional fine-tuning)
    double small_move_bias = 1.0;  // 0.1-0.2% moves
    double medium_move_bias = 1.0; // 0.2-0.5% moves  
    double large_move_bias = 1.0;  // 0.5-1.0% moves
    double extreme_move_bias = 1.0; // >1.0% moves
    
    // Reset to neutral (no bias)
    void reset() {
        up_bias = down_bias = 1.0;
        small_move_bias = medium_move_bias = large_move_bias = extreme_move_bias = 1.0;
    }
};

// ─────────────────────── 20-Bucket Directional Bias ───────────────────────
struct DirectionalBias20 {
    double up_bias = 1.0;
    double down_bias = 1.0;
    
    // Granular move biases (10 levels instead of 4)
    std::array<double, 10> move_bias = {
        1.0, 1.0, 1.0, 1.0, 1.0,  // 000-040
        1.0, 1.0, 1.0, 1.0, 1.0   // 050-090
    };
    
    void reset() {
        up_bias = down_bias = 1.0;
        std::fill(move_bias.begin(), move_bias.end(), 1.0);
    }
    
    // Smooth bias function based on bucket index
    double get_move_bias(int index) const {
        return move_bias[std::clamp(index, 0, 9)];
    }
};

// ─────────────────────── Filter Knob Adjustments ───────────────────────
struct FilterKnobAdjustments {
    double bucket_weight_adjustment = 0.0;           // Adjustment to u-channel gain
    double frequency_domain_weight_adjustment = 0.0; // Adjustment to frequency domain weight
    double lambda_adjustment = 0.0;                  // Adjustment to forgetting factor (negative = faster forgetting)
    
    // Underlying signals driving the adjustments
    double quality_signal = 0.0;                     // Enhanced quality signal [0,1]
    double volatility_alert = 0.0;                   // Volatility alert signal [0,1]
};

// ─────────────────────── Scoring Result (20-Bucket) ───────────────────────
struct ScoringResult20 {
    double sharpening_factor = 1.0;     // α for Dirichlet sharpening
    DirectionalBias20 bias;             // 20-bucket directional bias vector
    hefkf_common::BucketConfidence20 enhanced_buckets; // Final 20-bucket probabilities
    MarketRegime detected_regime = MarketRegime::UNKNOWN; // Detected market regime
    double confidence_score = 0.0;     // Overall confidence in the prediction [0,1]
    FilterKnobAdjustments filter_adjustments; // Filter knob adjustments
};

// ─────────────────────── AnalyticScorer Class ───────────────────────
class AnalyticScorer {
public:
    // Constructor with configurable parameters
    explicit AnalyticScorer();
    
    // Main scoring interface (complex regime detection) - 20-bucket
    ScoringResult20 score_20bucket(const hefkf_common::FrequencyFeatures& freq_features,
                                   const hefkf_common::BucketConfidence20& prior_buckets,
                                   MarketRegime regime_hint = MarketRegime::UNKNOWN,
                                   bool is_1min_filter = true);
    
    // Simplified scoring interface using 3-state regime detection - 20-bucket
    ScoringResult20 score_simple_20bucket(const hefkf_common::FrequencyFeatures& freq_features,
                                         const hefkf_common::BucketConfidence20& prior_buckets,
                                         double price_velocity, bool is_1min_filter = true);
    
    // 20-bucket versions
    hefkf_common::BucketConfidence20 apply_enhancements_20bucket(
        const hefkf_common::BucketConfidence20& prior_buckets,
        double sharpening_factor,
        const DirectionalBias20& bias) const;
    
    DirectionalBias20 compute_directional_bias_20bucket(
        const NormalizedFeatures& norm_features,
        MarketRegime regime) const;
    
    // Regime detection from frequency features (complex)
    MarketRegime detect_regime(const NormalizedFeatures& norm_features) const;
    
    // Simplified regime detection from price velocity and trend strength
    Regime detect_simple_regime(double price_velocity, double trend_strength) const;
    
    // Configuration
    void set_normalization_params(double trend_scale = AnalyticScorerConstants::DEFAULT_TREND_SCALE, 
                                 double flux_scale = AnalyticScorerConstants::DEFAULT_FLUX_SCALE,
                                 double derivative_sensitivity = AnalyticScorerConstants::DEFAULT_DERIVATIVE_SENSITIVITY);
    
    // State queries
    const NormalizedFeatures& get_last_normalized() const { return m_last_normalized; }
    const ScoringResult20& get_last_result() const { return m_last_result; }

private:
    // Normalization parameters
    // UPDATED: Recalibrated for returns-based spectral analysis
    double m_trend_scale = AnalyticScorerConstants::DEFAULT_TREND_SCALE;         // 95th percentile for trend_strength (returns-based)
    double m_flux_scale = AnalyticScorerConstants::DEFAULT_FLUX_SCALE;      // 95th percentile for spectral_flux (returns-based, was 0.05)
    double m_derivative_sensitivity = AnalyticScorerConstants::DEFAULT_DERIVATIVE_SENSITIVITY; // Scaling for derivative tanh mapping
    
    // State tracking
    NormalizedFeatures m_last_normalized;
    ScoringResult20 m_last_result;
    
    // Core processing functions
    NormalizedFeatures normalize_features(const hefkf_common::FrequencyFeatures& features) const;
    
    double compute_sharpening_factor(const NormalizedFeatures& norm_features, 
                                   MarketRegime regime) const;
    

    
    double compute_confidence_score(const NormalizedFeatures& norm_features,
                                  MarketRegime regime) const;
    
    // Quality factor computation using weight tables
    double compute_weighted_score(const NormalizedFeatures& norm_features, 
                                 const Weights& weights) const;
    

    
    // Enhanced scoring signals using ALL spectral features
    double compute_enhanced_quality_signal(const NormalizedFeatures& norm_features) const;
    double compute_volatility_alert(const NormalizedFeatures& norm_features) const;
    
    // Filter knob adjustment computation
    FilterKnobAdjustments compute_filter_adjustments(double quality_signal, 
                                                   double volatility_alert,
                                                   bool is_1min_filter = true) const;
    
    ScoringResult20 apply_simple_regime_scoring_20bucket(const hefkf_common::FrequencyFeatures& freq_features,
                                                        const hefkf_common::BucketConfidence20& prior_buckets,
                                                        Regime simple_regime,
                                                        bool is_1min_filter) const;
    
    // Regime-specific scoring strategies (20-bucket)
    void apply_bull_trend_scoring(const NormalizedFeatures& norm, 
                                 double& sharpening, DirectionalBias20& bias) const;
    void apply_bear_trend_scoring(const NormalizedFeatures& norm,
                                 double& sharpening, DirectionalBias20& bias) const;
    void apply_high_vol_scoring(const NormalizedFeatures& norm,
                               double& sharpening, DirectionalBias20& bias) const;
    void apply_low_vol_scoring(const NormalizedFeatures& norm,
                              double& sharpening, DirectionalBias20& bias) const;
    void apply_reversal_scoring(const NormalizedFeatures& norm,
                               double& sharpening, DirectionalBias20& bias) const;
    void apply_breakout_scoring(const NormalizedFeatures& norm,
                               double& sharpening, DirectionalBias20& bias) const;
};

// ─────────────────────── Utility Functions ───────────────────────
// Convert regime enum to string for logging/debugging
std::string regime_to_string(MarketRegime regime);
std::string regime_to_string(Regime regime);

// Market regime detection utilities
MarketRegime classify_regime_from_price_action(double trend_strength, 
                                             double volatility_proxy,
                                             double momentum_indicator);

// ─────────────────────── Filter Knob Application Utilities ───────────────────────
// Apply filter adjustments to base parameters
struct FilterParameters {
    double bucket_weight = AnalyticScorerConstants::BASE_BUCKET_WEIGHT;              // Base u-channel gain
    double frequency_domain_weight = AnalyticScorerConstants::BASE_FREQUENCY_WEIGHT;    // Base frequency domain weight (1min: 0.30, 5min: 0.35)
    double lambda_fixed = AnalyticScorerConstants::BASE_LAMBDA_FIXED;               // Base forgetting factor
    
    // Apply adjustments from scoring result
    void apply_adjustments(const FilterKnobAdjustments& adjustments);
    
    // Get adjusted values
    double get_adjusted_bucket_weight() const;
    double get_adjusted_frequency_weight() const; 
    double get_adjusted_lambda() const;
};

// Helper function to create base parameters for filter type
FilterParameters create_base_filter_params(bool is_1min_filter);

#endif // ANALYTIC_SCORER_HPP 