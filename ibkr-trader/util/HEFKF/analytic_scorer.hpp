// AnalyticScorer - Confidence Engine for Regime-Aware Bucket Probability Enhancement
// Transforms FrequencyFeatures into sharpening factors and directional biases
// Keeps Kalman core untouched while adding sophisticated frequency-domain insights

#ifndef ANALYTIC_SCORER_HPP
#define ANALYTIC_SCORER_HPP

#include "frequency_analyser.hpp"
#include "posterior.hpp"
#include <unordered_map>
#include <string>

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
inline double tanh_norm(double x, double scale = 5.0) {
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

// ─────────────────────── Scoring Result ───────────────────────
struct ScoringResult {
    double sharpening_factor = 1.0;     // α for Dirichlet sharpening
    DirectionalBias bias;               // Directional bias vector
    hefkf_common::BucketConfidence enhanced_buckets; // Final bucket probabilities
    MarketRegime detected_regime = MarketRegime::UNKNOWN; // Detected market regime
    double confidence_score = 0.0;     // Overall confidence in the prediction [0,1]
};

// ─────────────────────── AnalyticScorer Class ───────────────────────
class AnalyticScorer {
public:
    // Constructor with configurable parameters
    explicit AnalyticScorer();
    
    // Main scoring interface (complex regime detection)
    ScoringResult score(const hefkf_common::FrequencyFeatures& freq_features,
                       const hefkf_common::BucketConfidence& prior_buckets,
                       MarketRegime regime_hint = MarketRegime::UNKNOWN);
    
    // Simplified scoring interface using 3-state regime detection  
    ScoringResult score_simple(const hefkf_common::FrequencyFeatures& freq_features,
                              const hefkf_common::BucketConfidence& prior_buckets,
                              double price_velocity);
    
    // Regime detection from frequency features (complex)
    MarketRegime detect_regime(const NormalizedFeatures& norm_features) const;
    
    // Simplified regime detection from price velocity and trend strength
    Regime detect_simple_regime(double price_velocity, double trend_strength) const;
    
    // Configuration
    void set_normalization_params(double trend_scale = 0.25, 
                                 double flux_scale = 0.05,
                                 double derivative_sensitivity = 5.0);
    
    // State queries
    const NormalizedFeatures& get_last_normalized() const { return last_normalized_; }
    const ScoringResult& get_last_result() const { return last_result_; }

private:
    // Normalization parameters
    double trend_scale_ = 0.25;        // 95th percentile for trend_strength
    double flux_scale_ = 0.05;         // 95th percentile for spectral_flux  
    double derivative_sensitivity_ = 5.0; // Scaling for derivative tanh mapping
    
    // State tracking
    NormalizedFeatures last_normalized_;
    ScoringResult last_result_;
    
    // Core processing functions
    NormalizedFeatures normalize_features(const hefkf_common::FrequencyFeatures& features) const;
    
    double compute_sharpening_factor(const NormalizedFeatures& norm_features, 
                                   MarketRegime regime) const;
    
    DirectionalBias compute_directional_bias(const NormalizedFeatures& norm_features,
                                           MarketRegime regime) const;
    
    hefkf_common::BucketConfidence apply_enhancements(
        const hefkf_common::BucketConfidence& prior_buckets,
        double sharpening_factor,
        const DirectionalBias& bias) const;
    
    double compute_confidence_score(const NormalizedFeatures& norm_features,
                                  MarketRegime regime) const;
    
    // Quality factor computation using weight tables
    double compute_weighted_score(const NormalizedFeatures& norm_features, 
                                 const Weights& weights) const;
    
    // Create directional bias vector for simple regime scoring
    hefkf_common::BucketConfidence create_directional_bias(Regime regime, double quality) const;
    
    ScoringResult apply_simple_regime_scoring(const hefkf_common::FrequencyFeatures& freq_features,
                                            const hefkf_common::BucketConfidence& prior_buckets,
                                            Regime simple_regime) const;
    
    // Regime-specific scoring strategies
    void apply_bull_trend_scoring(const NormalizedFeatures& norm, 
                                 double& sharpening, DirectionalBias& bias) const;
    void apply_bear_trend_scoring(const NormalizedFeatures& norm,
                                 double& sharpening, DirectionalBias& bias) const;
    void apply_high_vol_scoring(const NormalizedFeatures& norm,
                               double& sharpening, DirectionalBias& bias) const;
    void apply_low_vol_scoring(const NormalizedFeatures& norm,
                              double& sharpening, DirectionalBias& bias) const;
    void apply_reversal_scoring(const NormalizedFeatures& norm,
                               double& sharpening, DirectionalBias& bias) const;
    void apply_breakout_scoring(const NormalizedFeatures& norm,
                               double& sharpening, DirectionalBias& bias) const;
};

// ─────────────────────── Utility Functions ───────────────────────
// Convert regime enum to string for logging/debugging
std::string regime_to_string(MarketRegime regime);
std::string regime_to_string(Regime regime);

// Market regime detection utilities
MarketRegime classify_regime_from_price_action(double trend_strength, 
                                             double volatility_proxy,
                                             double momentum_indicator);

#endif // ANALYTIC_SCORER_HPP 