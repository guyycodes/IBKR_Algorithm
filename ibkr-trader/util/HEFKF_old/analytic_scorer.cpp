// AnalyticScorer Implementation - Confidence Engine for Bucket Enhancement
// Implements feature normalization, regime detection, and probability sharpening

#include "analytic_scorer.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>

// ─────────────────────── Constructor ───────────────────────
// ─────────────────────── Static Weight Tables ───────────────────────
const Weights W_BULL{0.25, 0.25, 0.20, 0.15, 0.15};   // Bull: balanced weights (was 0.40 coherence)
const Weights W_BEAR{0.25, 0.25, 0.20, 0.15, 0.15};   // Bear: balanced weights (was 0.40 coherence)
const Weights W_SIDE{0.35, 0.15, 0.15, 0.20, 0.15};   // Sideways: still emphasize coherence but less (was 0.60)

AnalyticScorer::AnalyticScorer() {
    // Initialize with default normalization parameters
    // These can be tuned based on historical data analysis
}

// ─────────────────────── Main Scoring Interface ───────────────────────
ScoringResult AnalyticScorer::score(const hefkf_common::FrequencyFeatures& freq_features,
                                   const hefkf_common::BucketConfidence& prior_buckets,
                                   MarketRegime regime_hint,
                                   bool is_1min_filter) {
    
    // Step 1: Normalize raw frequency features to [0,1] or [-1,1] ranges
    NormalizedFeatures norm_features = normalize_features(freq_features);
    last_normalized_ = norm_features;
    
    // Step 2: Detect market regime (or use hint if provided)
    MarketRegime detected_regime = (regime_hint != MarketRegime::UNKNOWN) ? 
                                   regime_hint : detect_regime(norm_features);
    
    // Step 3: Compute sharpening factor based on signal quality
    double sharpening_factor = compute_sharpening_factor(norm_features, detected_regime);
    
    // Step 4: Compute directional bias based on momentum and trend
    DirectionalBias bias = compute_directional_bias(norm_features, detected_regime);
    
    // Step 5: Apply enhancements to prior bucket probabilities
    hefkf_common::BucketConfidence enhanced_buckets = apply_enhancements(
        prior_buckets, sharpening_factor, bias);
    
    // Step 6: Compute overall confidence score
    double confidence_score = compute_confidence_score(norm_features, detected_regime);
    
    // Compute enhanced signals using ALL spectral features
    double enhanced_quality = compute_enhanced_quality_signal(norm_features);
    double volatility_alert = compute_volatility_alert(norm_features);
    
    // Compute filter knob adjustments
    FilterKnobAdjustments filter_adjustments = compute_filter_adjustments(
        enhanced_quality, volatility_alert, is_1min_filter);
    
    // Package results
    ScoringResult result;
    result.sharpening_factor = sharpening_factor;
    result.bias = bias;
    result.enhanced_buckets = enhanced_buckets;
    result.detected_regime = detected_regime;
    result.confidence_score = std::max(confidence_score, enhanced_quality);  // Use best of both
    result.filter_adjustments = filter_adjustments;  // NEW: Filter knob adjustments
    
    last_result_ = result;
    return result;
}

// ─────────────────────── Simplified Scoring Interface ───────────────────────
ScoringResult AnalyticScorer::score_simple(const hefkf_common::FrequencyFeatures& freq_features,
                                          const hefkf_common::BucketConfidence& prior_buckets,
                                          double price_velocity,
                                          bool is_1min_filter) {
    
    // Step 1: Normalize features (same as complex version)
    NormalizedFeatures norm_features = normalize_features(freq_features);
    last_normalized_ = norm_features;
    
    // Step 2: Detect simple regime using price velocity and trend strength
    Regime simple_regime = detect_simple_regime(price_velocity, freq_features.trend_strength);
    
    // Step 3: Apply regime-specific scoring using weight tables
    ScoringResult result = apply_simple_regime_scoring(freq_features, prior_buckets, simple_regime, is_1min_filter);
    
    last_result_ = result;
    return result;
}

// ─────────────────────── Feature Normalization ───────────────────────
NormalizedFeatures AnalyticScorer::normalize_features(const hefkf_common::FrequencyFeatures& features) const {
    using namespace normalization;
    
    NormalizedFeatures norm;
    
    // Debug extreme spectral flux
    static int norm_call_count = 0;
    norm_call_count++;
    if (norm_call_count >= 1998 && norm_call_count <= 2002) {
        std::cout << "=== normalize_features (call " << norm_call_count << ") ===" << std::endl;
        std::cout << "Input spectral_flux: " << features.spectral_flux << std::endl;
    }
    
    // Core spectral features - map to [0, 1] using expected 95th percentiles
    norm.trend = clamp01(features.trend_strength / trend_scale_);
    norm.flux = clamp01(features.spectral_flux / flux_scale_);
    
    if (norm_call_count >= 1998 && norm_call_count <= 2002) {
        std::cout << "flux_scale_: " << flux_scale_ << ", raw/scale: " << (features.spectral_flux / flux_scale_)
                  << ", norm.flux: " << norm.flux << std::endl;
    }
    
    norm.coher_pv = features.coherence_price_volume_peak;  // Already [0, 1]
    norm.coher_ps = features.coherence_price_spread_peak;  // Already [0, 1]
    
    // NEW: Apply uncertainty floor dampening to prevent overconfidence
    // This ensures no feature can reach extreme values too easily
    // Using a power function: x^1.5 dampens high values more than low values
    // Examples: 0.9^1.5 = 0.853, 0.8^1.5 = 0.715, 0.7^1.5 = 0.586
    constexpr double DAMPENING_POWER = 1.5;
    norm.trend = std::pow(norm.trend, DAMPENING_POWER);
    norm.coher_pv = std::pow(norm.coher_pv, DAMPENING_POWER);
    norm.coher_ps = std::pow(norm.coher_ps, DAMPENING_POWER);
    
    // Additional capping to ensure no single feature dominates
    constexpr double MAX_FEATURE_VALUE = 0.9;
    norm.trend = std::min(norm.trend, MAX_FEATURE_VALUE);
    norm.coher_pv = std::min(norm.coher_pv, MAX_FEATURE_VALUE);
    norm.coher_ps = std::min(norm.coher_ps, MAX_FEATURE_VALUE);
    
    // Spectral centroids - normalize by typical frequency ranges
    // Assuming centroids are in normalized frequency units [0, 0.5]
    norm.centroid_price = clamp01(features.spectral_centroid_price * 2.0);
    norm.centroid_volume = clamp01(features.spectral_centroid_volume * 2.0);
    
    // Band-specific entropy - already in [0, 1]
    auto entropy_it = features.entropy_by_band.find("microstructure");
    norm.entropy_micro = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    entropy_it = features.entropy_by_band.find("short_term");
    norm.entropy_short = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    entropy_it = features.entropy_by_band.find("medium_term");
    norm.entropy_medium = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    entropy_it = features.entropy_by_band.find("trend");
    norm.entropy_trend = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    // Band-specific coherence - already in [0, 1]
    auto coher_it = features.coherence_price_volume_by_band.find("microstructure");
    norm.coher_pv_micro = (coher_it != features.coherence_price_volume_by_band.end()) ? coher_it->second : 0.0;
    
    coher_it = features.coherence_price_volume_by_band.find("short_term");
    norm.coher_pv_short = (coher_it != features.coherence_price_volume_by_band.end()) ? coher_it->second : 0.0;
    
    coher_it = features.coherence_price_volume_by_band.find("medium_term");
    norm.coher_pv_medium = (coher_it != features.coherence_price_volume_by_band.end()) ? coher_it->second : 0.0;
    
    coher_it = features.coherence_price_volume_by_band.find("trend");
    norm.coher_pv_trend = (coher_it != features.coherence_price_volume_by_band.end()) ? coher_it->second : 0.0;
    
    // Derivative features - map from [-inf, inf] to [0, 1] using tanh
    norm.d_trend = tanh_norm(features.trend_strength_derivative, derivative_sensitivity_);
    norm.d_coher_pv = tanh_norm(features.coherence_pv_derivative, derivative_sensitivity_);
    norm.centroid_velocity = tanh_norm(features.centroid_velocity, derivative_sensitivity_);
    
    return norm;
}

// ─────────────────────── Market Regime Detection ───────────────────────
MarketRegime AnalyticScorer::detect_regime(const NormalizedFeatures& norm_features) const {
    // Simple regime classification based on key spectral features
    
    // High trend strength + consistent momentum = trending market
    if (norm_features.trend > 0.7) {
        if (norm_features.d_trend > 0.6) {
            return MarketRegime::BULL_TREND;
        } else if (norm_features.d_trend < 0.4) {
            return MarketRegime::BEAR_TREND;
        }
    }
    
    // High spectral flux = high volatility regime
    if (norm_features.flux > 0.7) {
        return MarketRegime::HIGH_VOLATILITY;
    }
    
    // Low flux + low trend = low volatility regime
    if (norm_features.flux < 0.3 && norm_features.trend < 0.3) {
        return MarketRegime::LOW_VOLATILITY;
    }
    
    // Strong momentum reversal signal
    if (std::abs(norm_features.d_trend - 0.5) > 0.3 && norm_features.centroid_velocity > 0.7) {
        return MarketRegime::REVERSAL;
    }
    
    // Strong coherence + strong momentum = breakout
    if (norm_features.coher_pv > 0.8 && norm_features.d_trend > 0.7) {
        return MarketRegime::BREAKOUT;
    }
    
    return MarketRegime::UNKNOWN;
}

// ─────────────────────── Standalone Simple Regime Classification ───────────────────────
Regime classify_regime(double price_vel, double trend_strength) {
    if (trend_strength > 0.15) {
        if (price_vel > 0) return Regime::Bull;
        if (price_vel < 0) return Regime::Bear;
    }
    return Regime::Sideways;
}

// ─────────────────────── Simplified Regime Detection ───────────────────────
Regime AnalyticScorer::detect_simple_regime(double price_velocity, double trend_strength) const {
    if (trend_strength > 0.15) {
        if (price_velocity > 0) return Regime::Bull;
        if (price_velocity < 0) return Regime::Bear;
    }
    return Regime::Sideways;
}

// ─────────────────────── Sharpening Factor Computation ───────────────────────
double AnalyticScorer::compute_sharpening_factor(const NormalizedFeatures& norm_features, 
                                                 MarketRegime regime) const {
    // Base sharpening from signal quality (coherence and low entropy indicate high quality)
    double signal_quality = (norm_features.coher_pv + norm_features.coher_ps) / 2.0;
    double noise_level = (norm_features.entropy_micro + norm_features.entropy_short) / 2.0;
    
    // UPDATED: Reduced base sharpening multiplier from 3.0 to 1.5
    // This prevents excessive sharpening even with high quality signals
    double base_sharpening = 1.0 + 1.5 * signal_quality * (1.0 - noise_level);
    
    // UPDATED: Reduced regime multipliers to be more conservative
    double regime_multiplier = 1.0;
    switch (regime) {
        case MarketRegime::BULL_TREND:
        case MarketRegime::BEAR_TREND:
            regime_multiplier = 1.1; // was 1.2
            break;
        case MarketRegime::HIGH_VOLATILITY:
            regime_multiplier = 0.85; // was 0.8 (slightly less reduction)
            break;
        case MarketRegime::LOW_VOLATILITY:
            regime_multiplier = 1.05; // was 1.1
            break;
        case MarketRegime::BREAKOUT:
            regime_multiplier = 1.15; // was 1.3
            break;
        case MarketRegime::REVERSAL:
            regime_multiplier = 0.9; // unchanged
            break;
        default:
            regime_multiplier = 1.0;
    }
    
    // UPDATED: Reduced max sharpening from 5.0 to 2.5 to match new conservative approach
    return std::clamp(base_sharpening * regime_multiplier, 1.0, 2.5);
}

// ─────────────────────── Directional Bias Computation ───────────────────────
DirectionalBias AnalyticScorer::compute_directional_bias(const NormalizedFeatures& norm_features,
                                                        MarketRegime regime) const {
    DirectionalBias bias;
    bias.reset(); // Start with neutral
    
    // Apply regime-specific bias logic
    switch (regime) {
        case MarketRegime::BULL_TREND:
            apply_bull_trend_scoring(norm_features, bias.up_bias, bias);
            break;
        case MarketRegime::BEAR_TREND:
            apply_bear_trend_scoring(norm_features, bias.down_bias, bias);
            break;
        case MarketRegime::HIGH_VOLATILITY:
            apply_high_vol_scoring(norm_features, bias.extreme_move_bias, bias);
            break;
        case MarketRegime::LOW_VOLATILITY:
            apply_low_vol_scoring(norm_features, bias.small_move_bias, bias);
            break;
        case MarketRegime::REVERSAL:
            apply_reversal_scoring(norm_features, bias.up_bias, bias);
            break;
        case MarketRegime::BREAKOUT:
            apply_breakout_scoring(norm_features, bias.up_bias, bias);
            break;
        default:
            // Keep neutral bias for unknown regimes
            break;
    }
    
    return bias;
}

// ─────────────────────── Regime-Specific Scoring Functions ───────────────────────
void AnalyticScorer::apply_bull_trend_scoring(const NormalizedFeatures& norm, 
                                             double& sharpening, DirectionalBias& bias) const {
    // UPDATED: Reduced bias multipliers for more conservative adjustments
    bias.up_bias = 1.0 + 0.3 * norm.d_trend;     // was 0.5
    bias.down_bias = 1.0 - 0.2 * norm.d_trend;   // was 0.3
    
    // Favor larger moves in trending markets (reduced)
    bias.medium_move_bias = 1.0 + 0.1 * norm.trend;   // was 0.2
    bias.large_move_bias = 1.0 + 0.15 * norm.trend;   // was 0.3
}

void AnalyticScorer::apply_bear_trend_scoring(const NormalizedFeatures& norm,
                                             double& sharpening, DirectionalBias& bias) const {
    // UPDATED: Reduced bias multipliers for more conservative adjustments
    bias.down_bias = 1.0 + 0.3 * (1.0 - norm.d_trend);   // was 0.5
    bias.up_bias = 1.0 - 0.2 * (1.0 - norm.d_trend);     // was 0.3
    
    // Favor larger moves in trending markets (reduced)
    bias.medium_move_bias = 1.0 + 0.1 * norm.trend;   // was 0.2
    bias.large_move_bias = 1.0 + 0.15 * norm.trend;   // was 0.3
}

void AnalyticScorer::apply_high_vol_scoring(const NormalizedFeatures& norm,
                                           double& sharpening, DirectionalBias& bias) const {
    // Favor extreme moves in high volatility
    bias.extreme_move_bias = 1.0 + 0.4 * norm.flux;
    bias.small_move_bias = 1.0 - 0.2 * norm.flux;
}

void AnalyticScorer::apply_low_vol_scoring(const NormalizedFeatures& norm,
                                          double& sharpening, DirectionalBias& bias) const {
    // Favor small moves in low volatility
    bias.small_move_bias = 1.0 + 0.3 * (1.0 - norm.flux);
    bias.extreme_move_bias = 1.0 - 0.2 * (1.0 - norm.flux);
}

void AnalyticScorer::apply_reversal_scoring(const NormalizedFeatures& norm,
                                           double& sharpening, DirectionalBias& bias) const {
    // Reversal logic: if momentum was up, favor down and vice versa
    if (norm.d_trend > 0.5) {
        bias.down_bias = 1.0 + 0.4 * (norm.d_trend - 0.5);
        bias.up_bias = 1.0 - 0.2 * (norm.d_trend - 0.5);
    } else {
        bias.up_bias = 1.0 + 0.4 * (0.5 - norm.d_trend);
        bias.down_bias = 1.0 - 0.2 * (0.5 - norm.d_trend);
    }
}

void AnalyticScorer::apply_breakout_scoring(const NormalizedFeatures& norm,
                                           double& sharpening, DirectionalBias& bias) const {
    // Breakouts favor the direction of momentum with large moves
    if (norm.d_trend > 0.5) {
        bias.up_bias = 1.0 + 0.6 * norm.coher_pv;
        bias.down_bias = 1.0 - 0.2 * norm.coher_pv;
    } else {
        bias.down_bias = 1.0 + 0.6 * norm.coher_pv;
        bias.up_bias = 1.0 - 0.2 * norm.coher_pv;
    }
    
    bias.large_move_bias = 1.0 + 0.4 * norm.coher_pv;
    bias.extreme_move_bias = 1.0 + 0.5 * norm.coher_pv;
}

// ─────────────────────── Enhancement Application ───────────────────────
hefkf_common::BucketConfidence AnalyticScorer::apply_enhancements(
    const hefkf_common::BucketConfidence& prior_buckets,
    double sharpening_factor,
    const DirectionalBias& bias) const {
    
    hefkf_common::BucketConfidence enhanced = prior_buckets;
    
    // Apply directional biases
    enhanced.up_001_002 *= bias.up_bias * bias.small_move_bias;
    enhanced.up_002_005 *= bias.up_bias * bias.medium_move_bias;
    enhanced.up_005_010 *= bias.up_bias * bias.large_move_bias;
    enhanced.up_010_plus *= bias.up_bias * bias.extreme_move_bias;
    
    enhanced.dn_001_002 *= bias.down_bias * bias.small_move_bias;
    enhanced.dn_002_005 *= bias.down_bias * bias.medium_move_bias;
    enhanced.dn_005_010 *= bias.down_bias * bias.large_move_bias;
    enhanced.dn_010_plus *= bias.down_bias * bias.extreme_move_bias;
    
    // Normalize after bias application
    enhanced.normalize();
    
    // Apply Dirichlet sharpening (reuse existing function from posterior.hpp)
    sharpen_dirichlet(enhanced, (sharpening_factor - 1.0) / 4.0);
    
    return enhanced;
}

// ─────────────────────── Confidence Score Computation ───────────────────────
double AnalyticScorer::compute_confidence_score(const NormalizedFeatures& norm_features,
                                               MarketRegime regime) const {
    // Combine signal quality indicators - now using BOTH coherence signals
    double coherence_avg = (norm_features.coher_pv * 0.7 + norm_features.coher_ps * 0.3);
    double entropy_avg = (norm_features.entropy_micro + norm_features.entropy_short + 
                         norm_features.entropy_medium + norm_features.entropy_trend) / 4.0;
    
    // High coherence + low entropy = high confidence
    double signal_quality = coherence_avg * (1.0 - entropy_avg);
    
    // Regime-specific confidence adjustments
    double regime_confidence = 0.5; // Default for unknown
    switch (regime) {
        case MarketRegime::BULL_TREND:
        case MarketRegime::BEAR_TREND:
            regime_confidence = 0.8;
            break;
        case MarketRegime::BREAKOUT:
            regime_confidence = 0.9;
            break;
        case MarketRegime::HIGH_VOLATILITY:
            regime_confidence = 0.3;
            break;
        case MarketRegime::LOW_VOLATILITY:
            regime_confidence = 0.7;
            break;
        case MarketRegime::REVERSAL:
            regime_confidence = 0.4;
            break;
    }
    
    return std::clamp(0.5 * signal_quality + 0.5 * regime_confidence, 0.0, 1.0);
}

// ─────────────────────── Configuration ───────────────────────
void AnalyticScorer::set_normalization_params(double trend_scale, 
                                              double flux_scale,
                                              double derivative_sensitivity) {
    trend_scale_ = std::max(0.01, trend_scale);
    flux_scale_ = std::max(0.001, flux_scale);
    derivative_sensitivity_ = std::max(0.1, derivative_sensitivity);
}

// ─────────────────────── Quality Factor Computation ───────────────────────
double AnalyticScorer::compute_weighted_score(const NormalizedFeatures& norm_features, 
                                             const Weights& weights) const {
    // Quality factor q (0-1) using your exact specification
    using namespace normalization;
    return clamp01(
        weights.w_coher  * norm_features.coher_pv +
        weights.w_trend  * norm_features.trend +
        weights.w_flux   * norm_features.flux +
        weights.w_entropy* norm_features.entropy_short +
        weights.w_deriv  * norm_features.d_trend);
}

// ─────────────────────── Directional Bias Creation ───────────────────────
hefkf_common::BucketConfidence AnalyticScorer::create_directional_bias(Regime regime, double quality) const {
    // UPDATED: Reduced bias magnitude from 0.25 to 0.15 for more conservative redistribution
    // This prevents extreme probability shifts that could lead to overconfidence
    double bias_mag = 0.15 * quality;   // Max 15% mass re-allocated (was 25%)
    hefkf_common::BucketConfidence bias{};
    
    if (regime == Regime::Bull) {
        bias.up_002_005  = bias_mag * 0.4;   // 40% of bias mass
        bias.up_005_010  = bias_mag * 0.35;  // 35% of bias mass  
        bias.up_010_plus = bias_mag * 0.25;  // 25% of bias mass
    }
    else if (regime == Regime::Bear) {
        bias.dn_002_005  = bias_mag * 0.4;   // 40% of bias mass
        bias.dn_005_010  = bias_mag * 0.35;  // 35% of bias mass
        bias.dn_010_plus = bias_mag * 0.25;  // 25% of bias mass
    }
    // Sideways -> bias all zeros (default initialization)
    
    return bias;
}

// ─────────────────────── Enhanced Quality Signal (Uses ALL Features) ───────────────────────
double AnalyticScorer::compute_enhanced_quality_signal(const NormalizedFeatures& norm_features) const {
    // Enhanced quality signal using ALL normalized spectral features
    
    // Core signal quality (40% weight) - now using BOTH coherence signals
    double core_quality = 0.4 * (
        norm_features.coher_pv * 0.7 +      // Price-volume coherence (primary)
        norm_features.coher_ps * 0.3        // Price-spread coherence (secondary validation)
    ) * (1.0 - norm_features.entropy_short);
    
    // Trend and momentum indicators (25% weight)  
    double trend_quality = 0.25 * (norm_features.trend * 0.7 + norm_features.d_trend * 0.3);
    
    // Frequency domain characteristics (20% weight) - NOW USING BOTH CENTROIDS!
    double freq_quality = 0.2 * (
        (1.0 - norm_features.flux) * 0.4 +         // Low flux = stable = good quality
        norm_features.centroid_price * 0.25 +      // Price frequency characteristics
        norm_features.centroid_volume * 0.15 +     // Volume frequency characteristics  
        (1.0 - norm_features.centroid_velocity) * 0.2  // Low centroid velocity = stable spectrum
    );
    
    // Multi-band entropy assessment (15% weight) - NOW USING TREND ENTROPY!
    double entropy_quality = 0.15 * (
        (1.0 - norm_features.entropy_micro) * 0.3 +
        (1.0 - norm_features.entropy_short) * 0.3 +
        (1.0 - norm_features.entropy_medium) * 0.2 +
        (1.0 - norm_features.entropy_trend) * 0.2    // NOW INCLUDED!
    );
    
    using namespace normalization;
    return clamp01(core_quality + trend_quality + freq_quality + entropy_quality);
}

// ─────────────────────── Volatility Alert Signal ───────────────────────
double AnalyticScorer::compute_volatility_alert(const NormalizedFeatures& norm_features) const {
    // Volatility alert using spectral instability indicators
    
    // Primary volatility indicators (50% weight)
    double flux_alert = 0.5 * norm_features.flux;  // High spectral flux = high volatility
    
    // Frequency domain instability (30% weight) - NOW USING VOLUME CENTROID TOO!
    double freq_instability = 0.3 * (
        norm_features.centroid_velocity * 0.5 +     // Rapid price frequency shifts
        norm_features.centroid_volume * 0.2 +       // Volume frequency patterns
        norm_features.entropy_trend * 0.3           // High long-term complexity
    );
    
    // Multi-timeframe entropy chaos (20% weight)
    double entropy_chaos = 0.2 * (
        norm_features.entropy_micro * 0.4 +   // Microstructure noise
        norm_features.entropy_short * 0.3 +   // Short-term complexity
        norm_features.entropy_medium * 0.3    // Medium-term complexity
    );
    
    using namespace normalization;
    return clamp01(flux_alert + freq_instability + entropy_chaos);
}

// ─────────────────────── Filter Knob Adjustments ───────────────────────
FilterKnobAdjustments AnalyticScorer::compute_filter_adjustments(double quality_signal, 
                                                                double volatility_alert,
                                                                bool is_1min_filter) const {
    FilterKnobAdjustments adjustments;
    
    // Store the underlying signals
    adjustments.quality_signal = quality_signal;
    adjustments.volatility_alert = volatility_alert;
    
    // Your exact specifications:
    
    // 1. bucket_weight (u-channel gain): Base 0.50 + 0.30×quality - 0.20×volatility_alert
    adjustments.bucket_weight_adjustment = 0.30 * quality_signal - 0.20 * volatility_alert;
    
    // 2. frequency_domain_weight: Base 0.30 (1min)/0.35 (5min) + 0.15×quality
    adjustments.frequency_domain_weight_adjustment = 0.15 * quality_signal;
    
    // 3. lambda adjustment: If volatility_alert > 0.6, drop lambda by 0.02×volatility_alert
    if (volatility_alert > 0.6) {
        adjustments.lambda_adjustment = -0.02 * volatility_alert;  // Negative = faster forgetting
    } else {
        adjustments.lambda_adjustment = 0.0;  // No adjustment for low volatility
    }
    
    return adjustments;
}

ScoringResult AnalyticScorer::apply_simple_regime_scoring(const hefkf_common::FrequencyFeatures& freq_features,
                                                        const hefkf_common::BucketConfidence& prior_buckets,
                                                        Regime simple_regime,
                                                        bool is_1min_filter) const {
    //is_1min_filter is used to determine different base values for frequency domain weights.
    NormalizedFeatures norm_features = normalize_features(freq_features);
    
    // Select weight table based on regime
    const Weights* weights;
    switch (simple_regime) {
        case Regime::Bull:
            weights = &W_BULL;
            break;
        case Regime::Bear:
            weights = &W_BEAR;
            break;
        case Regime::Sideways:
        default:
            weights = &W_SIDE;
            break;
    }
    
    // Compute quality factor q (0-1)
    double quality = compute_weighted_score(norm_features, *weights);
    
    // NEW: Apply quality dampening to make high quality scores harder to achieve
    // This prevents the system from being overconfident in trending markets
    // Using sqrt dampening: quality 0.64 → 0.8, quality 0.81 → 0.9, quality 1.0 → 1.0
    quality = std::sqrt(quality);
    
    // UPDATED: Dirichlet sharpening α(q) - reduced from 4.0 to 1.5
    // This matches the change in sharpen_dirichlet function
    double alpha = 1.0 + 1.5 * quality;   // Range: [1, 2.5] (was [1, 5])
    
    // Create directional bias vector using helper function
    hefkf_common::BucketConfidence bias = create_directional_bias(simple_regime, quality);
    
    // Apply bias by adding to prior buckets
    hefkf_common::BucketConfidence enhanced_buckets = prior_buckets;
    
    // Debug tracking for tick 1000 issue
    static int call_count = 0;
    call_count++;
    bool debug_this = (call_count >= 1998 && call_count <= 2002) && is_1min_filter;
    
    if (debug_this) {
        std::cerr << "\n=== apply_simple_regime_scoring Debug (call " << call_count << ") ===" << std::endl;
        std::cerr << "Regime: " << (simple_regime == Regime::Bull ? "Bull" : 
                                   simple_regime == Regime::Bear ? "Bear" : "Sideways") << std::endl;
        std::cerr << "Quality: " << quality << std::endl;
        std::cerr << "Prior buckets - up total: " << (prior_buckets.up_001_002 + prior_buckets.up_002_005 + 
                                                      prior_buckets.up_005_010 + prior_buckets.up_010_plus) << std::endl;
        std::cerr << "Bias created - up bias: " << (bias.up_001_002 + bias.up_002_005 + 
                                                    bias.up_005_010 + bias.up_010_plus) << std::endl;
    }
    
    enhanced_buckets.up_001_002 += bias.up_001_002;
    enhanced_buckets.up_002_005 += bias.up_002_005;
    enhanced_buckets.up_005_010 += bias.up_005_010;
    enhanced_buckets.up_010_plus += bias.up_010_plus;
    enhanced_buckets.dn_001_002 += bias.dn_001_002;
    enhanced_buckets.dn_002_005 += bias.dn_002_005;
    enhanced_buckets.dn_005_010 += bias.dn_005_010;
    enhanced_buckets.dn_010_plus += bias.dn_010_plus;
    
    if (debug_this) {
        std::cerr << "After bias addition - up total: " << (enhanced_buckets.up_001_002 + enhanced_buckets.up_002_005 + 
                                                            enhanced_buckets.up_005_010 + enhanced_buckets.up_010_plus) << std::endl;
        std::cerr << "Individual buckets after bias:" << std::endl;
        std::cerr << "  up_001_002: " << enhanced_buckets.up_001_002 << std::endl;
        std::cerr << "  up_002_005: " << enhanced_buckets.up_002_005 << std::endl;
        std::cerr << "  up_005_010: " << enhanced_buckets.up_005_010 << std::endl;
        std::cerr << "  up_010_plus: " << enhanced_buckets.up_010_plus << std::endl;
        std::cerr << "  dn_001_002: " << enhanced_buckets.dn_001_002 << std::endl;
        std::cerr << "  dn_002_005: " << enhanced_buckets.dn_002_005 << std::endl;
        std::cerr << "  dn_005_010: " << enhanced_buckets.dn_005_010 << std::endl;
        std::cerr << "  dn_010_plus: " << enhanced_buckets.dn_010_plus << std::endl;
    }
    
    // Normalize after bias addition
    enhanced_buckets.normalize();
    
    if (debug_this) {
        std::cerr << "After normalization - up total: " << (enhanced_buckets.up_001_002 + enhanced_buckets.up_002_005 + 
                                                            enhanced_buckets.up_005_010 + enhanced_buckets.up_010_plus) << std::endl;
    }
    
    // Apply Dirichlet sharpening using existing function
    sharpen_dirichlet(enhanced_buckets, (alpha - 1.0) / 4.0);
    
    if (debug_this) {
        double final_up_total = enhanced_buckets.up_001_002 + enhanced_buckets.up_002_005 + 
                               enhanced_buckets.up_005_010 + enhanced_buckets.up_010_plus;
        std::cerr << "After sharpening - up total: " << final_up_total << std::endl;
        std::cerr << "Sharpening parameter passed: " << ((alpha - 1.0) / 4.0) << std::endl;
        
        // Check for anomaly
        double prior_up_total = prior_buckets.up_001_002 + prior_buckets.up_002_005 + 
                               prior_buckets.up_005_010 + prior_buckets.up_010_plus;
        if (prior_up_total > 0.9 && final_up_total < 0.1) {
            std::cerr << "CRITICAL ANOMALY: Enhancement inverted probabilities!" << std::endl;
            std::cerr << "Prior up: " << prior_up_total << " -> Enhanced up: " << final_up_total << std::endl;
        }
    }
    
    // Compute enhanced signals using ALL spectral features
    double enhanced_quality = compute_enhanced_quality_signal(norm_features);
    double volatility_alert = compute_volatility_alert(norm_features);
    
    // Compute filter knob adjustments
    FilterKnobAdjustments filter_adjustments = compute_filter_adjustments(
        enhanced_quality, volatility_alert, is_1min_filter);
    
    // Package results
    ScoringResult result;
    result.sharpening_factor = alpha;
    result.bias.reset(); // Not using DirectionalBias struct in simple approach
    result.enhanced_buckets = enhanced_buckets;
    result.detected_regime = MarketRegime::UNKNOWN; // Could map simple to complex if needed
    result.confidence_score = enhanced_quality;  // Use enhanced quality instead of basic quality
    result.filter_adjustments = filter_adjustments;  // NEW: Filter knob adjustments
    
    return result;
}

// ─────────────────────── Utility Functions ───────────────────────
std::string regime_to_string(MarketRegime regime) {
    switch (regime) {
        case MarketRegime::BULL_TREND: return "BULL_TREND";
        case MarketRegime::BEAR_TREND: return "BEAR_TREND";
        case MarketRegime::HIGH_VOLATILITY: return "HIGH_VOLATILITY";
        case MarketRegime::LOW_VOLATILITY: return "LOW_VOLATILITY";
        case MarketRegime::REVERSAL: return "REVERSAL";
        case MarketRegime::BREAKOUT: return "BREAKOUT";
        default: return "UNKNOWN";
    }
}

std::string regime_to_string(Regime regime) {
    switch (regime) {
        case Regime::Bull: return "BULL";
        case Regime::Bear: return "BEAR";
        case Regime::Sideways: return "SIDEWAYS";
        default: return "UNKNOWN";
    }
}

MarketRegime classify_regime_from_price_action(double trend_strength, 
                                             double volatility_proxy,
                                             double momentum_indicator) {
    // Simple classification based on traditional technical indicators
    if (trend_strength > 0.7 && momentum_indicator > 0.6) {
        return MarketRegime::BULL_TREND;
    } else if (trend_strength > 0.7 && momentum_indicator < 0.4) {
        return MarketRegime::BEAR_TREND;
    } else if (volatility_proxy > 0.8) {
        return MarketRegime::HIGH_VOLATILITY;
    } else if (volatility_proxy < 0.3) {
        return MarketRegime::LOW_VOLATILITY;
    }
    
    return MarketRegime::UNKNOWN;
}

// ─────────────────────── Filter Parameter Utilities ───────────────────────
void FilterParameters::apply_adjustments(const FilterKnobAdjustments& adjustments) {
    // Apply adjustments while maintaining reasonable bounds
    bucket_weight = std::clamp(bucket_weight + adjustments.bucket_weight_adjustment, 0.1, 0.9);
    frequency_domain_weight = std::clamp(frequency_domain_weight + adjustments.frequency_domain_weight_adjustment, 0.1, 0.6);
    lambda_fixed = std::clamp(lambda_fixed + adjustments.lambda_adjustment, 0.8, 0.99);
}

double FilterParameters::get_adjusted_bucket_weight() const {
    return bucket_weight;
}

double FilterParameters::get_adjusted_frequency_weight() const {
    return frequency_domain_weight;
}

double FilterParameters::get_adjusted_lambda() const {
    return lambda_fixed;
}

FilterParameters create_base_filter_params(bool is_1min_filter) {
    FilterParameters params;
    params.bucket_weight = 0.50;  // Same for both
    params.frequency_domain_weight = is_1min_filter ? 0.30 : 0.35;  // Your specification
    params.lambda_fixed = 0.95;   // Default, can be adjusted per filter
    return params;
}