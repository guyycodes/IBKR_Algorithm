// AnalyticScorer Implementation - 20 bucket Confidence Engine for Bucket Enhancement
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
ScoringResult20 AnalyticScorer::score_20bucket(const hefkf_common::FrequencyFeatures& freq_features,
                                              const hefkf_common::BucketConfidence20& prior_buckets,
                                              MarketRegime regime_hint,
                                              bool is_1min_filter) {
    
    // Step 1: Normalize raw frequency features to [0,1] or [-1,1] ranges
    NormalizedFeatures norm_features = normalize_features(freq_features);
    m_last_normalized = norm_features;
    
    // Step 2: Detect market regime (or use hint if provided)
    MarketRegime detected_regime = (regime_hint != MarketRegime::UNKNOWN) ? 
                                   regime_hint : detect_regime(norm_features);
    
    // Step 3: Compute sharpening factor based on signal quality
    double sharpening_factor = compute_sharpening_factor(norm_features, detected_regime);
    
    // Step 4: Compute directional bias based on momentum and trend (20-bucket)
    DirectionalBias20 bias = compute_directional_bias_20bucket(norm_features, detected_regime);
    
    // Step 5: Apply enhancements to prior bucket probabilities (20-bucket)
    hefkf_common::BucketConfidence20 enhanced_buckets = apply_enhancements_20bucket(
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
    ScoringResult20 result;
    result.sharpening_factor = sharpening_factor;
    result.bias = bias;
    result.enhanced_buckets = enhanced_buckets;
    result.detected_regime = detected_regime;
    result.confidence_score = std::max(confidence_score, enhanced_quality);  // Use best of both
    result.filter_adjustments = filter_adjustments;  // Filter knob adjustments
    
    m_last_result = result;
    return result;
}

// ─────────────────────── Simplified Scoring Interface (20-Bucket) ───────────────────────
ScoringResult20 AnalyticScorer::score_simple_20bucket(const hefkf_common::FrequencyFeatures& freq_features,
                                                     const hefkf_common::BucketConfidence20& prior_buckets,
                                                     double price_velocity,
                                                     bool is_1min_filter) {
    
    // Step 1: Normalize features (same as complex version)
    NormalizedFeatures norm_features = normalize_features(freq_features);
    m_last_normalized = norm_features;
    
    // Step 2: Detect simple regime using price velocity and trend strength
    Regime simple_regime = detect_simple_regime(price_velocity, freq_features.trend_strength);
    
    // Step 3: Apply regime-specific scoring using weight tables (20-bucket)
    ScoringResult20 result = apply_simple_regime_scoring_20bucket(freq_features, prior_buckets, simple_regime, is_1min_filter);
    
    m_last_result = result;
    return result;
}

// ─────────────────────── Feature Normalization ───────────────────────
NormalizedFeatures AnalyticScorer::normalize_features(const hefkf_common::FrequencyFeatures& features) const {
    using namespace normalization;
    
    NormalizedFeatures norm;
    
    // Debug spectral flux
    static int norm_call_count = 0;
    norm_call_count++;
    if (norm_call_count >= 1998 && norm_call_count <= 2002) {
        std::cout << "=== normalize_features (call " << norm_call_count << ") ===" << std::endl;
        std::cout << "Input spectral_flux: " << features.spectral_flux << std::endl;
    }
    
    // ─────────────────────── Normalization ───────────────────────
    // Core spectral features - map to [0, 1] using expected 95th percentiles
    norm.trend = clamp01(features.trend_strength / m_trend_scale);
    norm.flux = clamp01(features.spectral_flux / m_flux_scale);
    
    if (norm_call_count >= 1998 && norm_call_count <= 2002) {
        std::cout << "flux_scale_: " << m_flux_scale << ", raw/scale: " << (features.spectral_flux / m_flux_scale)
                  << ", norm.flux: " << norm.flux << std::endl;
    }
    
    // ─────────────────────── Coherence ───────────────────────
    norm.coher_pv = features.coherence_price_volume_peak;  // Already [0, 1]
    norm.coher_ps = features.coherence_price_spread_peak;  // Already [0, 1]
    
    // ─────────────────────── Dampening ───────────────────────
    // NEW: Apply uncertainty floor dampening to prevent overconfidence
    // This ensures no feature can reach extreme values too easily
    // Using a power function: x^1.5 dampens high values more than low values
    // Examples: 0.9^1.5 = 0.853, 0.8^1.5 = 0.715, 0.7^1.5 = 0.586
    norm.trend = std::pow(norm.trend, AnalyticScorerConstants::DAMPENING_POWER);
    norm.coher_pv = std::pow(norm.coher_pv, AnalyticScorerConstants::DAMPENING_POWER);
    norm.coher_ps = std::pow(norm.coher_ps, AnalyticScorerConstants::DAMPENING_POWER);
    
    // ─────────────────────── Capping ───────────────────────
    // Additional capping to ensure no single feature dominates
    norm.trend = std::min(norm.trend, AnalyticScorerConstants::MAX_FEATURE_VALUE);
    norm.coher_pv = std::min(norm.coher_pv, AnalyticScorerConstants::MAX_FEATURE_VALUE);
    norm.coher_ps = std::min(norm.coher_ps, AnalyticScorerConstants::MAX_FEATURE_VALUE);
    
    // ─────────────────────── Spectral Centroids ───────────────────────
    // Spectral centroids - normalize by typical frequency ranges
    // Assuming centroids are in normalized frequency units [0, 0.5]
    norm.centroid_price = clamp01(features.spectral_centroid_price * AnalyticScorerConstants::CENTROID_NORMALIZATION_FACTOR);
    norm.centroid_volume = clamp01(features.spectral_centroid_volume * AnalyticScorerConstants::CENTROID_NORMALIZATION_FACTOR);
    
    // ─────────────────────── Entropy ───────────────────────
    // Band-specific entropy - already in [0, 1]
    auto entropy_it = features.entropy_by_band.find("microstructure");
    norm.entropy_micro = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    entropy_it = features.entropy_by_band.find("short_term");
    norm.entropy_short = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    entropy_it = features.entropy_by_band.find("medium_term");
    norm.entropy_medium = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    entropy_it = features.entropy_by_band.find("trend");
    norm.entropy_trend = (entropy_it != features.entropy_by_band.end()) ? entropy_it->second : 0.0;
    
    // ─────────────────────── Coherence ───────────────────────
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
    norm.d_trend = tanh_norm(features.trend_strength_derivative, m_derivative_sensitivity);
    norm.d_coher_pv = tanh_norm(features.coherence_pv_derivative, m_derivative_sensitivity);
    norm.centroid_velocity = tanh_norm(features.centroid_velocity, m_derivative_sensitivity);
    
    return norm;
}

// ─────────────────────── Market Regime Detection ───────────────────────
MarketRegime AnalyticScorer::detect_regime(const NormalizedFeatures& norm_features) const {
    // Simple regime classification based on key spectral features
    
    // High trend strength + consistent momentum = trending market
    if (norm_features.trend > AnalyticScorerConstants::TREND_STRONG_THRESHOLD) {
        if (norm_features.d_trend > AnalyticScorerConstants::DERIVATIVE_HIGH_THRESHOLD) {
            return MarketRegime::BULL_TREND;
        } else if (norm_features.d_trend < AnalyticScorerConstants::DERIVATIVE_LOW_THRESHOLD) {
            return MarketRegime::BEAR_TREND;
        }
    }
    
    // High spectral flux = high volatility regime
    if (norm_features.flux > AnalyticScorerConstants::FLUX_HIGH_THRESHOLD) {
        return MarketRegime::HIGH_VOLATILITY;
    }
    
    // Low flux + low trend = low volatility regime
    if (norm_features.flux < AnalyticScorerConstants::FLUX_LOW_THRESHOLD && norm_features.trend < AnalyticScorerConstants::TREND_WEAK_THRESHOLD) {
        return MarketRegime::LOW_VOLATILITY;
    }
    
    // Strong momentum reversal signal
    if (std::abs(norm_features.d_trend - 0.5) > AnalyticScorerConstants::DERIVATIVE_REVERSAL_BAND && norm_features.centroid_velocity > AnalyticScorerConstants::CENTROID_VELOCITY_HIGH) {
        return MarketRegime::REVERSAL;
    }
    
    // Strong coherence + strong momentum = breakout
    if (norm_features.coher_pv > AnalyticScorerConstants::COHERENCE_HIGH_THRESHOLD && norm_features.d_trend > AnalyticScorerConstants::CENTROID_VELOCITY_HIGH) {
        return MarketRegime::BREAKOUT;
    }
    
    return MarketRegime::UNKNOWN;
}

// ─────────────────────── Standalone Simple Regime Classification ───────────────────────
Regime classify_regime(double price_vel, double trend_strength) {
    if (trend_strength > AnalyticScorerConstants::TREND_REVERSAL_THRESHOLD) {
        if (price_vel > 0) return Regime::Bull;
        if (price_vel < 0) return Regime::Bear;
    }
    return Regime::Sideways;
}

// ─────────────────────── Simplified Regime Detection ───────────────────────
Regime AnalyticScorer::detect_simple_regime(double price_velocity, double trend_strength) const {
    if (trend_strength > AnalyticScorerConstants::TREND_REVERSAL_THRESHOLD) {
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
    double base_sharpening = 1.0 + AnalyticScorerConstants::BASE_SHARPENING_MULTIPLIER * signal_quality * (1.0 - noise_level);
    
    // UPDATED: Reduced regime multipliers to be more conservative
    double regime_multiplier = 1.0;
    switch (regime) {
        case MarketRegime::BULL_TREND:
            regime_multiplier = AnalyticScorerConstants::BULL_SHARPENING_MULT; // was 1.2
            break;
        case MarketRegime::BEAR_TREND:
            regime_multiplier = AnalyticScorerConstants::BEAR_SHARPENING_MULT; // was 1.2
            break;
        case MarketRegime::HIGH_VOLATILITY:
            regime_multiplier = AnalyticScorerConstants::HIGH_VOL_SHARPENING_MULT; // was 0.8 (slightly less reduction)
            break;
        case MarketRegime::LOW_VOLATILITY:
            regime_multiplier = AnalyticScorerConstants::LOW_VOL_SHARPENING_MULT; // was 1.1
            break;
        case MarketRegime::BREAKOUT:
            regime_multiplier = AnalyticScorerConstants::BREAKOUT_SHARPENING_MULT; // was 1.3
            break;
        case MarketRegime::REVERSAL:
            regime_multiplier = AnalyticScorerConstants::REVERSAL_SHARPENING_MULT; // unchanged
            break;
        default:
            regime_multiplier = 1.0;
    }
    
    // UPDATED: Reduced max sharpening from 5.0 to 2.5 to match new conservative approach
    return std::clamp(base_sharpening * regime_multiplier, AnalyticScorerConstants::MIN_SHARPENING_FACTOR, AnalyticScorerConstants::MAX_SHARPENING_FACTOR);
}



// ─────────────────────── Regime-Specific Scoring Functions (20-Bucket) ───────────────────────
void AnalyticScorer::apply_bull_trend_scoring(const NormalizedFeatures& norm, 
                                             double& sharpening, DirectionalBias20& bias) const {
    // UPDATED: Reduced bias multipliers for more conservative adjustments
    bias.up_bias = 1.0 + AnalyticScorerConstants::BULL_UP_BIAS_FACTOR * norm.d_trend;     // was 0.5
    bias.down_bias = 1.0 - AnalyticScorerConstants::BULL_DOWN_BIAS_FACTOR * norm.d_trend;   // was 0.3
    
    // Favor larger moves in trending markets (reduced)
    // Apply smooth gradient across all 10 move sizes
    for (int i = 0; i < 10; ++i) {
        double scale = static_cast<double>(i) / 9.0;  // 0.0 to 1.0
        bias.move_bias[i] = 1.0 + (AnalyticScorerConstants::MOVE_BIAS_BASE + AnalyticScorerConstants::MOVE_BIAS_SCALE * scale) * norm.trend;  // 5% to 15% boost
    }
}

void AnalyticScorer::apply_bear_trend_scoring(const NormalizedFeatures& norm,
                                             double& sharpening, DirectionalBias20& bias) const {
    // UPDATED: Reduced bias multipliers for more conservative adjustments
    bias.down_bias = 1.0 + AnalyticScorerConstants::BEAR_DOWN_BIAS_FACTOR * (1.0 - norm.d_trend);   // was 0.5
    bias.up_bias = 1.0 - AnalyticScorerConstants::BEAR_UP_BIAS_FACTOR * (1.0 - norm.d_trend);     // was 0.3
    
    // Favor larger moves in trending markets (reduced)
    // Apply smooth gradient across all 10 move sizes
    for (int i = 0; i < 10; ++i) {
        double scale = static_cast<double>(i) / 9.0;  // 0.0 to 1.0
        bias.move_bias[i] = 1.0 + (AnalyticScorerConstants::MOVE_BIAS_BASE + AnalyticScorerConstants::MOVE_BIAS_SCALE * scale) * norm.trend;  // 5% to 15% boost
    }
}

void AnalyticScorer::apply_high_vol_scoring(const NormalizedFeatures& norm,
                                           double& sharpening, DirectionalBias20& bias) const {
    // Favor extreme moves in high volatility
    // Apply inverse gradient - smaller moves penalized, larger moves boosted
    for (int i = 0; i < 10; ++i) {
        double scale = static_cast<double>(i) / 9.0;  // 0.0 to 1.0
        bias.move_bias[i] = 1.0 - 0.2 * (1.0 - scale) * norm.flux +  // Penalty for small
                            0.4 * scale * norm.flux;                   // Boost for large
    }
}

void AnalyticScorer::apply_low_vol_scoring(const NormalizedFeatures& norm,
                                          double& sharpening, DirectionalBias20& bias) const {
    // Favor small moves in low volatility
    // Apply gradient - smaller moves boosted, larger moves penalized
    for (int i = 0; i < 10; ++i) {
        double scale = static_cast<double>(i) / 9.0;  // 0.0 to 1.0
        bias.move_bias[i] = 1.0 + 0.3 * (1.0 - scale) * (1.0 - norm.flux) -  // Boost for small
                            0.2 * scale * (1.0 - norm.flux);                   // Penalty for large
    }
}

void AnalyticScorer::apply_reversal_scoring(const NormalizedFeatures& norm,
                                           double& sharpening, DirectionalBias20& bias) const {
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
                                           double& sharpening, DirectionalBias20& bias) const {
    // Breakouts favor the direction of momentum with large moves
    if (norm.d_trend > 0.5) {
        bias.up_bias = 1.0 + 0.6 * norm.coher_pv;
        bias.down_bias = 1.0 - 0.2 * norm.coher_pv;
    } else {
        bias.down_bias = 1.0 + 0.6 * norm.coher_pv;
        bias.up_bias = 1.0 - 0.2 * norm.coher_pv;
    }
    
    // Favor large moves during breakouts
    for (int i = 0; i < 10; ++i) {
        double scale = static_cast<double>(i) / 9.0;  // 0.0 to 1.0
        bias.move_bias[i] = 1.0 + (0.2 + 0.3 * scale) * norm.coher_pv;  // 20% to 50% boost
    }
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
    m_trend_scale = std::max(0.01, trend_scale);
    m_flux_scale = std::max(0.001, flux_scale);
    m_derivative_sensitivity = std::max(0.1, derivative_sensitivity);
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



// ─────────────────────── Enhanced Quality Signal (Uses ALL Features) ───────────────────────
double AnalyticScorer::compute_enhanced_quality_signal(const NormalizedFeatures& norm_features) const {
    // Enhanced quality signal using ALL normalized spectral features
    
    // Coherence-based signal quality
    double core_quality = AnalyticScorerConstants::QUALITY_COHERENCE_WEIGHT * (
        norm_features.coher_pv * 0.7 +      // Price-volume coherence (primary)
        norm_features.coher_ps * 0.3        // Price-spread coherence (secondary validation)
    ) * (1.0 - norm_features.entropy_short);
    
    // Trend and momentum indicators  
    double trend_quality = AnalyticScorerConstants::QUALITY_TREND_WEIGHT * (norm_features.trend * 0.7 + norm_features.d_trend * 0.3);
    
    // Multi-band coherence assessment
    double band_quality = AnalyticScorerConstants::QUALITY_BAND_WEIGHT * (
        norm_features.coher_pv_micro * 0.2 +
        norm_features.coher_pv_short * 0.3 +
        norm_features.coher_pv_medium * 0.3 +
        norm_features.coher_pv_trend * 0.2
    );
    
    // Derivative momentum indicators
    double derivative_quality = AnalyticScorerConstants::QUALITY_DERIVATIVE_WEIGHT * (
        norm_features.d_trend * 0.5 +
        norm_features.d_coher_pv * 0.3 +
        (1.0 - norm_features.centroid_velocity) * 0.2  // Low velocity = stable
    );
    
    using namespace normalization;
    return clamp01(core_quality + trend_quality + band_quality + derivative_quality);
}

// ─────────────────────── Volatility Alert Signal ───────────────────────
double AnalyticScorer::compute_volatility_alert(const NormalizedFeatures& norm_features) const {
    // Volatility alert using spectral instability indicators
    
    // Spectral flux volatility
    double flux_alert = AnalyticScorerConstants::VOLATILITY_FLUX_WEIGHT * norm_features.flux;
    
    // Entropy-based volatility (disorder/complexity)
    double entropy_alert = AnalyticScorerConstants::VOLATILITY_ENTROPY_WEIGHT * (
        norm_features.entropy_micro * 0.3 +
        norm_features.entropy_short * 0.3 +
        norm_features.entropy_medium * 0.2 +
        norm_features.entropy_trend * 0.2
    );
    
    // Centroid velocity volatility
    double centroid_alert = AnalyticScorerConstants::VOLATILITY_CENTROID_WEIGHT * (
        norm_features.centroid_velocity * 0.7 +
        norm_features.centroid_volume * 0.3
    );
    
    // Derivative volatility (rapid changes in trends/coherence)
    double derivative_alert = AnalyticScorerConstants::VOLATILITY_DERIVATIVE_WEIGHT * (
        std::abs(norm_features.d_trend - 0.5) * 0.6 +  // Distance from neutral
        std::abs(norm_features.d_coher_pv - 0.5) * 0.4
    );
    
    using namespace normalization;
    return clamp01(flux_alert + entropy_alert + centroid_alert + derivative_alert);
}

// ─────────────────────── Filter Knob Adjustments ───────────────────────
FilterKnobAdjustments AnalyticScorer::compute_filter_adjustments(double quality_signal, 
                                                                double volatility_alert,
                                                                bool is_1min_filter) const {
    FilterKnobAdjustments adjustments;
    
    // Store the underlying signals
    adjustments.quality_signal = quality_signal;
    adjustments.volatility_alert = volatility_alert;
    
    // Filter knob adjustments based on market conditions
    
    // 1. bucket_weight (u-channel gain) adjustment
    double quality_boost = is_1min_filter ? AnalyticScorerConstants::QUALITY_WEIGHT_BOOST_1MIN : AnalyticScorerConstants::QUALITY_WEIGHT_BOOST_5MIN;
    adjustments.bucket_weight_adjustment = quality_boost * quality_signal + AnalyticScorerConstants::VOLATILITY_WEIGHT_REDUCTION * volatility_alert;
    
    // 2. frequency_domain_weight adjustment
    adjustments.frequency_domain_weight_adjustment = AnalyticScorerConstants::QUALITY_FREQ_BOOST * quality_signal + 
                                                    AnalyticScorerConstants::VOLATILITY_FREQ_REDUCTION * volatility_alert;
    
    // 3. lambda adjustment: reduce lambda for faster adaptation in volatile or high-quality markets
    adjustments.lambda_adjustment = AnalyticScorerConstants::QUALITY_LAMBDA_REDUCTION * quality_signal + 
                                   AnalyticScorerConstants::VOLATILITY_LAMBDA_REDUCTION * volatility_alert;
    
    return adjustments;
}

ScoringResult20 AnalyticScorer::apply_simple_regime_scoring_20bucket(const hefkf_common::FrequencyFeatures& freq_features,
                                                                    const hefkf_common::BucketConfidence20& prior_buckets,
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
    double alpha = 1.0 + AnalyticScorerConstants::BASE_SHARPENING_MULTIPLIER * quality;   // Range: [1, 2.5] (was [1, 5])
    
    // Create 20-bucket directional bias based on simple regime
    DirectionalBias20 bias;
    bias.reset();
    
    // Simple regime-based bias (reduced from 0.15 to be more conservative)
    double bias_mag = 0.15 * quality;  // Max 15% mass re-allocated
    
    if (simple_regime == Regime::Bull) {
        bias.up_bias = 1.0 + bias_mag;
        bias.down_bias = 1.0 - bias_mag * 0.5;
        // Favor medium to large moves: distribute bias across buckets 2-5 (0.5%-1.25%)
        for (int i = 2; i <= 5; ++i) {
            bias.move_bias[i] = 1.0 + bias_mag * 0.3;
        }
    }
    else if (simple_regime == Regime::Bear) {
        bias.down_bias = 1.0 + bias_mag;
        bias.up_bias = 1.0 - bias_mag * 0.5;
        // Favor medium to large moves
        for (int i = 2; i <= 5; ++i) {
            bias.move_bias[i] = 1.0 + bias_mag * 0.3;
        }
    }
    // Sideways -> neutral bias (all 1.0 from reset)
    
    // Apply bias by creating enhanced buckets
    hefkf_common::BucketConfidence20 enhanced_buckets = apply_enhancements_20bucket(
        prior_buckets, alpha, bias);
    
    // Debug tracking for tick 1000 issue
    static int call_count = 0;
    call_count++;
    bool debug_this = (call_count >= 1998 && call_count <= 2002) && is_1min_filter;
    
    if (debug_this) {
        std::cerr << "\n=== apply_simple_regime_scoring_20bucket Debug (call " << call_count << ") ===" << std::endl;
        std::cerr << "Regime: " << (simple_regime == Regime::Bull ? "Bull" : 
                                   simple_regime == Regime::Bear ? "Bear" : "Sideways") << std::endl;
        std::cerr << "Quality: " << quality << std::endl;
        
        // Calculate UP total for 20 buckets
        double prior_up_total = prior_buckets.up_000 + prior_buckets.up_010 + prior_buckets.up_020 + 
                               prior_buckets.up_030 + prior_buckets.up_040 + prior_buckets.up_050 + 
                               prior_buckets.up_060 + prior_buckets.up_070 + prior_buckets.up_080 + 
                               prior_buckets.up_090;
        double enhanced_up_total = enhanced_buckets.up_000 + enhanced_buckets.up_010 + enhanced_buckets.up_020 + 
                                  enhanced_buckets.up_030 + enhanced_buckets.up_040 + enhanced_buckets.up_050 + 
                                  enhanced_buckets.up_060 + enhanced_buckets.up_070 + enhanced_buckets.up_080 + 
                                  enhanced_buckets.up_090;
        
        std::cerr << "Prior buckets - up total: " << prior_up_total << std::endl;
        std::cerr << "Enhanced buckets - up total: " << enhanced_up_total << std::endl;
        std::cerr << "Sharpening parameter: " << alpha << std::endl;
    }
    
    // Compute enhanced signals using ALL spectral features
    double enhanced_quality = compute_enhanced_quality_signal(norm_features);
    double volatility_alert = compute_volatility_alert(norm_features);
    
    // Compute filter knob adjustments
    FilterKnobAdjustments filter_adjustments = compute_filter_adjustments(
        enhanced_quality, volatility_alert, is_1min_filter);
    
    // Package results
    ScoringResult20 result;
    result.sharpening_factor = alpha;
    result.bias = bias;  // Use the 20-bucket bias we created
    result.enhanced_buckets = enhanced_buckets;
    result.detected_regime = MarketRegime::UNKNOWN; // Could map simple to complex if needed
    result.confidence_score = enhanced_quality;  // Use enhanced quality instead of basic quality
    result.filter_adjustments = filter_adjustments;  // Filter knob adjustments
    
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
    params.bucket_weight = AnalyticScorerConstants::BASE_BUCKET_WEIGHT;  // Same for both
    params.frequency_domain_weight = AnalyticScorerConstants::BASE_FREQUENCY_WEIGHT;  // Base value
    // Adjust frequency weight for 5-min filter
    if (!is_1min_filter) {
        params.frequency_domain_weight += 0.05;  // 5-min uses slightly higher frequency weight
    }
    params.lambda_fixed = AnalyticScorerConstants::BASE_LAMBDA_FIXED;   // Default, can be adjusted per filter
    return params;
}

// ─────────────────────── 20-Bucket Enhancement Implementation ───────────────────────
hefkf_common::BucketConfidence20 AnalyticScorer::apply_enhancements_20bucket(
    const hefkf_common::BucketConfidence20& prior_buckets,
    double sharpening_factor,
    const DirectionalBias20& bias) const {
    
    hefkf_common::BucketConfidence20 enhanced = prior_buckets;
    
    // Apply directional biases with smooth transitions
    for (int i = 0; i <= 9; ++i) {
        hefkf_common::BucketAssignment ba_up{hefkf_common::UP, i};
        hefkf_common::BucketAssignment ba_dn{hefkf_common::DOWN, i};
        
        enhanced[ba_up] *= bias.up_bias * bias.get_move_bias(i);
        enhanced[ba_dn] *= bias.down_bias * bias.get_move_bias(i);
    }
    
    enhanced.normalize();
    sharpen_dirichlet_20bucket(enhanced, (sharpening_factor - 1.0) / 4.0);
    
    return enhanced;
}

// ─────────────────────── 20-Bucket Directional Bias Computation ───────────────────────
DirectionalBias20 AnalyticScorer::compute_directional_bias_20bucket(
    const NormalizedFeatures& norm_features,
    MarketRegime regime) const {
    
    DirectionalBias20 bias;
    bias.reset();
    
    // Apply regime-specific bias logic
    switch (regime) {
        case MarketRegime::BULL_TREND:
            bias.up_bias = 1.0 + 0.3 * norm_features.d_trend;
            bias.down_bias = 1.0 - 0.2 * norm_features.d_trend;
            // Favor medium to large moves in trending markets
            for (int i = 2; i <= 6; ++i) {  // Buckets 020-060 (0.5%-1.5%)
                bias.move_bias[i] = 1.0 + 0.15 * norm_features.trend;
            }
            break;
            
        case MarketRegime::BEAR_TREND:
            bias.down_bias = 1.0 + 0.3 * (1.0 - norm_features.d_trend);
            bias.up_bias = 1.0 - 0.2 * (1.0 - norm_features.d_trend);
            // Favor medium to large moves in trending markets
            for (int i = 2; i <= 6; ++i) {
                bias.move_bias[i] = 1.0 + 0.15 * norm_features.trend;
            }
            break;
            
        case MarketRegime::HIGH_VOLATILITY:
            // Favor extreme moves in high volatility
            for (int i = 7; i <= 9; ++i) {  // Buckets 070-090 (1.75%+)
                bias.move_bias[i] = 1.0 + 0.4 * norm_features.flux;
            }
            // Suppress small moves
            for (int i = 0; i <= 2; ++i) {  // Buckets 000-020 (0-0.5%)
                bias.move_bias[i] = 1.0 - 0.2 * norm_features.flux;
            }
            break;
            
        case MarketRegime::LOW_VOLATILITY:
            // Favor small moves in low volatility
            for (int i = 0; i <= 3; ++i) {  // Buckets 000-030 (0-0.75%)
                bias.move_bias[i] = 1.0 + 0.3 * (1.0 - norm_features.flux);
            }
            // Suppress extreme moves
            for (int i = 7; i <= 9; ++i) {
                bias.move_bias[i] = 1.0 - 0.2 * (1.0 - norm_features.flux);
            }
            break;
            
        case MarketRegime::REVERSAL:
            // Reversal logic based on momentum
            if (norm_features.d_trend > 0.5) {
                bias.down_bias = 1.0 + 0.4 * (norm_features.d_trend - 0.5);
                bias.up_bias = 1.0 - 0.2 * (norm_features.d_trend - 0.5);
            } else {
                bias.up_bias = 1.0 + 0.4 * (0.5 - norm_features.d_trend);
                bias.down_bias = 1.0 - 0.2 * (0.5 - norm_features.d_trend);
            }
            break;
            
        case MarketRegime::BREAKOUT:
            // Breakouts favor the direction of momentum with large moves
            if (norm_features.d_trend > 0.5) {
                bias.up_bias = 1.0 + 0.6 * norm_features.coher_pv;
                bias.down_bias = 1.0 - 0.2 * norm_features.coher_pv;
            } else {
                bias.down_bias = 1.0 + 0.6 * norm_features.coher_pv;
                bias.up_bias = 1.0 - 0.2 * norm_features.coher_pv;
            }
            // Favor large and extreme moves
            for (int i = 5; i <= 9; ++i) {  // Buckets 050-090 (1.25%+)
                bias.move_bias[i] = 1.0 + 0.4 * norm_features.coher_pv;
            }
            break;
            
        default:
            // Keep neutral bias for unknown regimes
            break;
    }
    
    // Smooth the move bias array to avoid discontinuities
    // Apply simple 3-point smoothing
    std::array<double, 10> smoothed;
    for (int i = 0; i < 10; ++i) {
        if (i == 0) {
            smoothed[i] = (bias.move_bias[0] + bias.move_bias[1]) / 2.0;
        } else if (i == 9) {
            smoothed[i] = (bias.move_bias[8] + bias.move_bias[9]) / 2.0;
        } else {
            smoothed[i] = (bias.move_bias[i-1] + bias.move_bias[i] + bias.move_bias[i+1]) / 3.0;
        }
    }
    bias.move_bias = smoothed;
    
    return bias;
}