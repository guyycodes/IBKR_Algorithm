// FrequencyAnalyser Implementation - FFTW3-based Welch PSD and Coherence Analysis
// Optimized for <50µs latency with 2-segment non-overlapping Welch method and reused FFTW plans

#include "frequency_analyser.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <cstring>
#include <iostream>  // For std::cerr debug logging

// ─────────────────────── Shared FrequencyFeatures Implementation ───────────────────────
namespace hefkf_common {

FrequencyFeatures::FrequencyFeatures() {
    // Initialize band maps with default values (matching Python frequency_bands)
    coherence_price_volume_by_band["microstructure"] = 0.0;  // 1-5 min
    coherence_price_volume_by_band["short_term"] = 0.0;      // 5-30 min  
    coherence_price_volume_by_band["medium_term"] = 0.0;     // 30-120 min
    coherence_price_volume_by_band["trend"] = 0.0;           // >120 min
    
    coherence_price_spread_by_band["microstructure"] = 0.0;
    coherence_price_spread_by_band["short_term"] = 0.0;
    coherence_price_spread_by_band["medium_term"] = 0.0;
    coherence_price_spread_by_band["trend"] = 0.0;
    
    // Initialize entropy band maps
    entropy_by_band["microstructure"] = 0.0;
    entropy_by_band["short_term"] = 0.0;
    entropy_by_band["medium_term"] = 0.0;
    entropy_by_band["trend"] = 0.0;
}

} // namespace hefkf_common

// ─────────────────────── FrequencyAnalyser Implementation ───────────────────────

// Frequency band definitions (normalized frequencies for fs=1.0)
const std::vector<FrequencyAnalyser::FreqBand> FrequencyAnalyser::FREQ_BANDS = {
    {0.0,           1.0/(120*60.0), "trend"},        // >120 min periods
    {1.0/(120*60.0), 1.0/(30*60.0), "medium_term"}, // 30-120 min periods
    {1.0/(30*60.0),  1.0/(5*60.0),  "short_term"},  // 5-30 min periods  
    {1.0/(5*60.0),   1.0/(1*60.0),  "microstructure"} // 1-5 min periods
};

// ─────────────────────── Constructor & Destructor ───────────────────────
FrequencyAnalyser::FrequencyAnalyser(double fs) : fs_(fs), total_ticks_pushed_(-1) {
    // Initialize buffers
    window_.resize(WIN);
    fft_input_.resize(WIN);
    fft_input_y_.resize(WIN);
    fft_output_.resize(WIN/2 + 1);  // Real-to-complex FFT output size
    fft_output_y_.resize(WIN/2 + 1);
    
    // Initialize FFTW plan
    init_fftw_plans();
    init_hanning_window();
    
    // Initialize data arrays to zero
    price_.fill(0.0);
    volume_.fill(0.0);
    spread_.fill(0.0);
    
    // Note: total_ticks_pushed_ starts at -1 so that after the initialization
    // push (from FilterPipeline::initialize), it will be 0, matching FilterPipeline's counting
}

FrequencyAnalyser::~FrequencyAnalyser() {
    cleanup_fftw();
}

// Move constructor
FrequencyAnalyser::FrequencyAnalyser(FrequencyAnalyser&& other) noexcept
    : idx_(other.idx_), filled_(other.filled_), fs_(other.fs_),
      price_(std::move(other.price_)), volume_(std::move(other.volume_)), spread_(std::move(other.spread_)),
      plan_forward_(other.plan_forward_), plan_forward_y_(other.plan_forward_y_), window_(std::move(other.window_)),
      fft_input_(std::move(other.fft_input_)), fft_input_y_(std::move(other.fft_input_y_)),
      fft_output_(std::move(other.fft_output_)), fft_output_y_(std::move(other.fft_output_y_)),
      total_ticks_pushed_(other.total_ticks_pushed_) {
    
    other.plan_forward_ = nullptr;  // Prevent double cleanup
    other.plan_forward_y_ = nullptr;
    other.total_ticks_pushed_ = 0;
}

// Move assignment
FrequencyAnalyser& FrequencyAnalyser::operator=(FrequencyAnalyser&& other) noexcept {
    if (this != &other) {
        cleanup_fftw();
        
        idx_ = other.idx_;
        filled_ = other.filled_;
        fs_ = other.fs_;
        price_ = std::move(other.price_);
        volume_ = std::move(other.volume_);
        spread_ = std::move(other.spread_);
        plan_forward_ = other.plan_forward_;
        plan_forward_y_ = other.plan_forward_y_;
        window_ = std::move(other.window_);
        fft_input_ = std::move(other.fft_input_);
        fft_input_y_ = std::move(other.fft_input_y_);
        fft_output_ = std::move(other.fft_output_);
        fft_output_y_ = std::move(other.fft_output_y_);
        total_ticks_pushed_ = other.total_ticks_pushed_;
        
        other.plan_forward_ = nullptr;
        other.plan_forward_y_ = nullptr;
        other.total_ticks_pushed_ = 0;
    }
    return *this;
}

// ─────────────────────── Main Interface ───────────────────────
void FrequencyAnalyser::push(double price, double volume, double spread) {
    price_[idx_] = price;    // Store new price at current index
    volume_[idx_] = volume;  // Store new volume at current index
    spread_[idx_] = spread;  // Store new spread at current index
    
    idx_ = (idx_ + 1) % WIN; // Move to next position, wrap around
    if (idx_ == 0) {         // If we wrapped back to start
        filled_ = true;      // Buffer is now full
    }
    
    // Increment tick counter for debugging
    total_ticks_pushed_++;
    
    // Debug logging around tick 1000
    if (total_ticks_pushed_ >= 995 && total_ticks_pushed_ <= 1005) {
        std::cerr << "FrequencyAnalyser::push - tick " << total_ticks_pushed_ 
                  << ", idx=" << idx_ << ", filled=" << filled_ 
                  << ", price=" << price << std::endl;
    }
}
// ───────────────────────────────────────────────────────────────────────
// The compute function is the main entrypoint for the frequency analysis, 
// it takes the raw price, volume, and spread data and computes the frequency features
// ───────────────────────────────────────────────────────────────────────
bool FrequencyAnalyser::compute(hefkf_common::FrequencyFeatures& out) {
    if (!filled_) {
        return false;  // Need full window for reliable analysis
    }
    
    // Prepare linearized arrays (handle circular buffer)
    std::vector<double> price_linear(WIN), volume_linear(WIN), spread_linear(WIN);
    std::vector<double> freq_vec;
    
    // Linearize circular buffers
    for (int i = 0; i < WIN; ++i) {
        int circular_idx = (idx_ + i) % WIN;
        price_linear[i] = price_[circular_idx];
        volume_linear[i] = volume_[circular_idx];
        spread_linear[i] = spread_[circular_idx];
    }
    
    // Convert prices to returns for spectral analysis
    // This makes the data stationary and eliminates DC bias
    // IMPORTANT: This alters all spectral metrics:
    // - Spectral flux: (e.g., 0.00001 instead of 0.01)
    // - Trend strength: Now measures consistency of returns, not price trends
    // - All downstream normalization constants must be calibrated accordingly
    std::vector<double> price_returns(WIN);
    price_returns[0] = 0.0;  // First return is undefined, set to 0
    
    for (int i = 1; i < WIN; ++i) {
        if (price_linear[i-1] > EPSILON && price_linear[i] > EPSILON) {
            // Simple returns: (P[i] - P[i-1]) / P[i-1]
            price_returns[i] = (price_linear[i] - price_linear[i-1]) / price_linear[i-1];
        } else {
            price_returns[i] = 0.0;  // Handle edge case of zero/negative prices
        }
    }
    
    // ═══ CORE SPECTRAL ANALYSIS ═══
    // Compute PSDs using Welch method
    std::vector<double> psd_price, psd_volume, psd_spread;
    welch_psd(price_returns.data(), psd_price, freq_vec);  // Use returns instead of raw prices
    welch_psd(volume_linear.data(), psd_volume, freq_vec);
    welch_psd(spread_linear.data(), psd_spread, freq_vec);
    
    // Compute trend strength (low frequency power ratio)
    // Note: Now using returns PSD, so this measures the fraction of price CHANGES in low frequencies
    // rather than absolute price level, which is more meaningful for trend detection
    double total_power = std::accumulate(psd_price.begin(), psd_price.end(), 0.0);
    double current_trend_strength = band_power(freq_vec, psd_price, 0.0, 1.0/(120*60.0)) / 
                                   std::max(total_power, EPSILON);
    out.trend_strength = current_trend_strength;
    
    // ═══ ENHANCED SPECTRAL FEATURES ═══
    // Compute spectral centroids
    out.spectral_centroid_price = compute_spectral_centroid(freq_vec, psd_price);
    out.spectral_centroid_volume = compute_spectral_centroid(freq_vec, psd_volume);
    
    // Compute spectral flux (rate of change in spectral shape)
    if (!prev_psd_price_.empty() && has_previous_compute_) {
        out.spectral_flux = compute_spectral_flux(psd_price, prev_psd_price_);
    } else {
        out.spectral_flux = 0.0;  // No previous data for comparison
    }
    
    // Compute band-specific entropy for market complexity analysis
    for (const auto& band : FREQ_BANDS) {
        out.entropy_by_band[band.name] = compute_band_entropy(freq_vec, psd_price, 
                                                             band.f_low, band.f_high);
    }
    
    // ═══ COHERENCE ANALYSIS ═══
    std::vector<double> coherence_pv, coherence_ps;
    double current_coherence_pv = coherence_estimate(price_returns.data(), volume_linear.data(), 
                                                    coherence_pv, freq_vec);
    out.coherence_price_volume_peak = current_coherence_pv;
    out.coherence_price_spread_peak = coherence_estimate(price_returns.data(), spread_linear.data(), 
                                                        coherence_ps, freq_vec);
    
    // Compute band-wise coherence
    compute_band_coherence(freq_vec, coherence_pv, out.coherence_price_volume_by_band);
    compute_band_coherence(freq_vec, coherence_ps, out.coherence_price_spread_by_band);
    
    // ═══ DERIVATIVE FEATURES (MOMENTUM DETECTION) ═══
    double current_spectral_centroid = out.spectral_centroid_price;
    
    if (has_previous_compute_) {
        // Time delta for derivatives (assuming constant sampling rate)
        double time_delta = 1.0 / fs_;
        
        // Compute derivatives using historical values
        out.trend_strength_derivative = compute_derivative(current_trend_strength, 
                                                          prev_trend_strength_, time_delta);
        out.coherence_pv_derivative = compute_derivative(current_coherence_pv, 
                                                        prev_coherence_pv_peak_, time_delta);
        out.centroid_velocity = compute_derivative(current_spectral_centroid, 
                                                  prev_spectral_centroid_, time_delta);
    } else {
        // First computation - no derivatives available
        out.trend_strength_derivative = 0.0;
        out.coherence_pv_derivative = 0.0;
        out.centroid_velocity = 0.0;
    }
    
    // ═══ UPDATE HISTORICAL TRACKING ═══
    // Store current values for next iteration's derivative calculations
    prev_trend_strength_ = current_trend_strength;
    prev_coherence_pv_peak_ = current_coherence_pv;
    prev_spectral_centroid_ = current_spectral_centroid;
    prev_psd_price_ = psd_price;  // Store for spectral flux calculation
    has_previous_compute_ = true;
    
    return true;
}

void FrequencyAnalyser::reset() {
    idx_ = 0;
    filled_ = false;
    price_.fill(0.0);
    volume_.fill(0.0);
    spread_.fill(0.0);
    
    // Reset derivative tracking
    prev_trend_strength_ = 0.0;
    prev_coherence_pv_peak_ = 0.0;
    prev_spectral_centroid_ = 0.0;
    prev_psd_price_.clear();
    has_previous_compute_ = false;
    
    // Reset tick counter to -1 (will become 0 after initialization push)
    total_ticks_pushed_ = -1;
}

// ─────────────────────── Welch PSD Implementation ───────────────────────
// The Power Spectral Density (PSD) of a signal describes how the power of that signal is distributed across different frequencies. 
// It's a way to analyze the frequency content of a signal and identify the dominant frequencies and their respective power contributions
// Power Spectral Density (PSD) is the entrypoint for the frequency analysis, 
// it tells you about the frequency components that make up the signal and how much "power" (or energy) each frequency component contributes.
// ───────────────────────────────────────────────────────────────────────
void FrequencyAnalyser::welch_psd(const double* x, std::vector<double>& out_psd, std::vector<double>& out_freq) {
    const int seg_len = WIN / 2;      // 128 samples per segment
    const int n_segs = 2;             // Two non-overlapping segments: [0..127] and [128..255]
    
    out_psd.clear();
    out_psd.resize(seg_len/2 + 1, 0.0);
    
    // Generate frequency vector (only once)
    if (out_freq.empty()) {
        out_freq.resize(seg_len/2 + 1);
        for (int i = 0; i < seg_len/2 + 1; ++i) {
            out_freq[i] = (i * fs_) / seg_len;
        }
    }
    
    std::vector<double> windowed_seg(seg_len);
    std::vector<std::complex<double>> seg_fft(seg_len/2 + 1);
    
    // Compute window normalization factor
    double window_norm = 0.0;
    for (int i = 0; i < seg_len; ++i) {
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (seg_len - 1))); // Hanning
        window_norm += w * w;
    }
    window_norm /= seg_len;
    
    // Process each segment - now using all 256 samples efficiently
    for (int seg = 0; seg < n_segs; ++seg) {
        int start_idx = seg * seg_len;  // Segment 1: 0-127, Segment 2: 128-255
        
        // Apply Hanning window to segment
        for (int i = 0; i < seg_len; ++i) {
            double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (seg_len - 1)));
            windowed_seg[i] = x[start_idx + i] * w;
        }
        
        // Setup FFTW input
        std::copy(windowed_seg.begin(), windowed_seg.end(), fft_input_.begin());
        
        // Execute FFT
        fftw_execute_dft_r2c(plan_forward_, fft_input_.data(),
                             reinterpret_cast<fftw_complex*>(seg_fft.data()));
        
        // Accumulate magnitude squared
        for (size_t i = 0; i < seg_fft.size(); ++i) {
            double mag_sq = std::norm(seg_fft[i]);
            out_psd[i] += mag_sq;
        }
    }
    
    // Average and normalize
    double norm_factor = 1.0 / (n_segs * fs_ * window_norm);
    for (auto& p : out_psd) {
        p *= norm_factor;
    }
    
    // Handle DC and Nyquist components for real signals
    for (int i = 1; i < static_cast<int>(out_psd.size()) - 1; ++i) {
        out_psd[i] *= 2.0;  // Account for negative frequencies
    }
}

// ─────────────────────── Coherence Estimation ───────────────────────
double FrequencyAnalyser::coherence_estimate(const double* x, const double* y, 
                                            std::vector<double>& out_coherence, 
                                            std::vector<double>& out_freq) {
    // True coherence using cross-PSD: |Pxy|² / (Pxx * Pyy)
    const int seg_len = WIN / 2;      // 128 samples per segment
    const int n_segs = 2;             // Two non-overlapping segments: [0..127] and [128..255]
    
    out_coherence.clear();
    out_coherence.resize(seg_len/2 + 1, 0.0);
    
    // Generate frequency vector (only once)
    if (out_freq.empty()) {
        out_freq.resize(seg_len/2 + 1);
        for (int i = 0; i < seg_len/2 + 1; ++i) {
            out_freq[i] = (i * fs_) / seg_len;
        }
    }
    
    std::vector<double> psd_x(seg_len/2 + 1, 0.0);
    std::vector<double> psd_y(seg_len/2 + 1, 0.0);
    std::vector<std::complex<double>> psd_xy(seg_len/2 + 1, {0.0, 0.0});
    
    std::vector<double> windowed_x(seg_len), windowed_y(seg_len);
    std::vector<std::complex<double>> fft_x(seg_len/2 + 1), fft_y(seg_len/2 + 1);
    
    // Compute window normalization factor
    double window_norm = 0.0;
    for (int i = 0; i < seg_len; ++i) {
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (seg_len - 1))); // Hanning
        window_norm += w * w;
    }
    window_norm /= seg_len;
    
    // Process each segment
    for (int seg = 0; seg < n_segs; ++seg) {
        int start_idx = seg * seg_len;  // Segment 1: 0-127, Segment 2: 128-255
        
        // Apply Hanning window to both signals
        for (int i = 0; i < seg_len; ++i) {
            double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (seg_len - 1)));
            windowed_x[i] = x[start_idx + i] * w;
            windowed_y[i] = y[start_idx + i] * w;
        }
        
        // Setup FFTW inputs
        std::copy(windowed_x.begin(), windowed_x.end(), fft_input_.begin());
        std::copy(windowed_y.begin(), windowed_y.end(), fft_input_y_.begin());
        
        // Execute FFTs
        fftw_execute_dft_r2c(plan_forward_, fft_input_.data(),
                             reinterpret_cast<fftw_complex*>(fft_x.data()));
        fftw_execute_dft_r2c(plan_forward_y_, fft_input_y_.data(),
                             reinterpret_cast<fftw_complex*>(fft_y.data()));
        
        // Accumulate PSDs and cross-PSD
        for (size_t i = 0; i < fft_x.size(); ++i) {
            psd_x[i] += std::norm(fft_x[i]);      // |X[i]|²
            psd_y[i] += std::norm(fft_y[i]);      // |Y[i]|²
            psd_xy[i] += std::conj(fft_x[i]) * fft_y[i];  // X*[i] * Y[i]
        }
    }
    
    // Compute coherence: |Pxy|² / (Pxx * Pyy)
    double norm_factor = 1.0 / (n_segs * fs_ * window_norm);
    for (size_t i = 0; i < out_coherence.size(); ++i) {
        psd_x[i] *= norm_factor;
        psd_y[i] *= norm_factor;
        psd_xy[i] *= norm_factor;
        
        // Handle DC and Nyquist components for real signals
        if (i > 0 && i < out_coherence.size() - 1) {
            psd_x[i] *= 2.0;
            psd_y[i] *= 2.0;
            psd_xy[i] *= 2.0;
        }
        
        double denom = psd_x[i] * psd_y[i];
        if (denom > EPSILON) {
            out_coherence[i] = std::norm(psd_xy[i]) / denom;  // |Pxy|² / (Pxx * Pyy)
            out_coherence[i] = std::min(1.0, out_coherence[i]); // Clamp to [0,1]
        } else {
            out_coherence[i] = 0.0;
        }
    }
    
    return find_peak_coherence(out_coherence);
}

double FrequencyAnalyser::find_peak_coherence(const std::vector<double>& coherence) const {
    if (coherence.empty()) return 0.0;
    return *std::max_element(coherence.begin(), coherence.end());
}

// ─────────────────────── Frequency Band Analysis ───────────────────────
double FrequencyAnalyser::band_power(const std::vector<double>& freq, const std::vector<double>& psd, 
                                    double f_low, double f_high) const {
    double power = 0.0;
    for (size_t i = 0; i < freq.size() && i < psd.size(); ++i) {
        if (freq[i] >= f_low && freq[i] <= f_high) {
            power += psd[i];
        }
    }
    return power;
}

void FrequencyAnalyser::compute_band_coherence(const std::vector<double>& freq, 
                                              const std::vector<double>& coherence,
                                              std::unordered_map<std::string, double>& band_map) const {
    for (const auto& band : FREQ_BANDS) {
        double sum = 0.0;
        int count = 0;
        
        for (size_t i = 0; i < freq.size() && i < coherence.size(); ++i) {
            if (freq[i] >= band.f_low && freq[i] <= band.f_high) {
                sum += coherence[i];
                count++;
            }
        }
        
        band_map[band.name] = (count > 0) ? sum / count : 0.0;
    }
}

// ─────────────────────── Helper Functions ───────────────────────
void FrequencyAnalyser::init_fftw_plans() {
    plan_forward_ = fftw_plan_dft_r2c_1d(WIN/2, fft_input_.data(),
                                         reinterpret_cast<fftw_complex*>(fft_output_.data()),
                                         FFTW_MEASURE);
    
    plan_forward_y_ = fftw_plan_dft_r2c_1d(WIN/2, fft_input_y_.data(),
                                           reinterpret_cast<fftw_complex*>(fft_output_y_.data()),
                                           FFTW_MEASURE);
    
    if (!plan_forward_ || !plan_forward_y_) {
        throw std::runtime_error("Failed to create FFTW plans");
    }
}

void FrequencyAnalyser::cleanup_fftw() {
    if (plan_forward_) {
        fftw_destroy_plan(plan_forward_);
        plan_forward_ = nullptr;
    }
    if (plan_forward_y_) {
        fftw_destroy_plan(plan_forward_y_);
        plan_forward_y_ = nullptr;
    }
}

void FrequencyAnalyser::init_hanning_window() {
    for (int i = 0; i < WIN; ++i) {
        window_[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (WIN - 1)));
    }
}

void FrequencyAnalyser::apply_window(const double* input, double* windowed) const {
    for (int i = 0; i < WIN; ++i) {
        windowed[i] = input[i] * window_[i];
    }
}

// ─────────────────────── Spectral Feature Analysis ───────────────────────
double FrequencyAnalyser::compute_spectral_centroid(
    const std::vector<double>& freq, 
    const std::vector<double>& psd) const {
    
    double weighted_sum = 0.0;
    double magnitude_sum = 0.0;
    
    for (size_t i = 0; i < freq.size() && i < psd.size(); ++i) {
        weighted_sum += freq[i] * psd[i];
        magnitude_sum += psd[i];
    }
    
    if (magnitude_sum < EPSILON) {
        return 0.0;
    }
    
    return weighted_sum / magnitude_sum;
}

double FrequencyAnalyser::compute_spectral_flux(
    const std::vector<double>& current_psd,
    const std::vector<double>& previous_psd) const {
    
    if (previous_psd.empty() || current_psd.size() != previous_psd.size()) {
        return 0.0;
    }
    
    double flux = 0.0;
    
    // Calculate normalized spectral difference
    for (size_t i = 0; i < current_psd.size(); ++i) {
        double diff = current_psd[i] - previous_psd[i];
        // Only consider positive differences (spectral increase)
        if (diff > 0) {
            flux += diff * diff;
        }
    }
    
    double raw_flux = std::sqrt(flux) / current_psd.size();
    
    // Cap spectral flux to reasonable bounds (0.0 to 1.0)
    // Normal range is 0.001-0.05, so 1.0 is already very high
    const double MAX_SPECTRAL_FLUX = 1.0;
    
    // Debug logging for extreme values
    if (raw_flux > 0.1) {  // 2x the normal maximum
        std::cerr << "WARNING: Extreme spectral flux detected: " << raw_flux 
                  << " (normal range: 0.001-0.05)" << std::endl;
        std::cerr << "  PSD vector size: " << current_psd.size() 
                  << ", Sum of squared diffs: " << flux << std::endl;
        std::cerr << "  Total ticks pushed: " << total_ticks_pushed_ << std::endl;
    }
    
    return std::min(raw_flux, MAX_SPECTRAL_FLUX);
}

double FrequencyAnalyser::compute_band_entropy(
    const std::vector<double>& freq,
    const std::vector<double>& psd,
    double f_low, double f_high) const {
    
    // Extract band power distribution
    std::vector<double> band_powers;
    double total_band_power = 0.0;
    
    for (size_t i = 0; i < freq.size() && i < psd.size(); ++i) {
        if (freq[i] >= f_low && freq[i] <= f_high) {
            band_powers.push_back(psd[i]);
            total_band_power += psd[i];
        }
    }
    
    if (band_powers.empty() || total_band_power < EPSILON) {
        return 0.0;
    }
    
    // Calculate Shannon entropy
    double entropy = 0.0;
    for (double power : band_powers) {
        if (power > EPSILON) {
            double p = power / total_band_power;
            entropy -= p * std::log2(p);
        }
    }
    
    // Normalize by log2(n) to get value in [0, 1]
    double max_entropy = std::log2(band_powers.size());
    return (max_entropy > EPSILON) ? entropy / max_entropy : 0.0;
}

// ─────────────────────── Derivative Computation ───────────────────────
double FrequencyAnalyser::compute_derivative(
    double current_value, 
    double previous_value, 
    double time_delta) const {
    
    if (time_delta < EPSILON) {
        return 0.0;
    }
    
    return (current_value - previous_value) / time_delta;
}

// ─────────────────────── Time-Domain Trend Analysis ───────────────────────
// Alternative trend strength computation using time-domain linear regression
// Currently unused - kept for future use as an alternative to frequency-domain approach
// This measures monotonic directional trend strength vs. frequency-domain cyclical analysis
double FrequencyAnalyser::compute_trend_strength(const double* price_data) const {
    // Simple trend strength using linear regression slope
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
    
    for (int i = 0; i < WIN; ++i) {
        sum_x += i;
        sum_y += price_data[i];
        sum_xy += i * price_data[i];
        sum_x2 += i * i;
    }
    
    double denom = WIN * sum_x2 - sum_x * sum_x;
    if (std::abs(denom) < EPSILON) return 0.0;
    
    double slope = (WIN * sum_xy - sum_x * sum_y) / denom;
    return std::abs(slope);  // Magnitude of trend
} 