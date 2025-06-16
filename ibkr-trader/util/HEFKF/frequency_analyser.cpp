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
FrequencyAnalyser::FrequencyAnalyser(double fs) : m_fs(fs), m_total_ticks_pushed(-1) {
    // Initialize buffers
    m_window.resize(WIN);
    m_fft_input.resize(WIN);
    m_fft_input_y.resize(WIN);
    m_fft_output.resize(WIN/2 + 1);  // Real-to-complex FFT output size
    m_fft_output_y.resize(WIN/2 + 1);
    
    // Initialize FFTW plan
    init_fftw_plans();
    init_hanning_window();
    
    // Initialize data arrays to zero
    m_price.fill(0.0);
    m_volume.fill(0.0);
    m_spread.fill(0.0);
    
    // Note: m_total_ticks_pushed starts at -1 so that after the initialization
    // push (from FilterPipeline::initialize), it will be 0, matching FilterPipeline's counting
}

FrequencyAnalyser::~FrequencyAnalyser() {
    cleanup_fftw();
}

// Move constructor
FrequencyAnalyser::FrequencyAnalyser(FrequencyAnalyser&& other) noexcept
    : m_idx(other.m_idx), m_filled(other.m_filled), m_fs(other.m_fs),
      m_price(std::move(other.m_price)), m_volume(std::move(other.m_volume)), m_spread(std::move(other.m_spread)),
      m_plan_forward(other.m_plan_forward), m_plan_forward_y(other.m_plan_forward_y), m_window(std::move(other.m_window)),
      m_fft_input(std::move(other.m_fft_input)), m_fft_input_y(std::move(other.m_fft_input_y)),
      m_fft_output(std::move(other.m_fft_output)), m_fft_output_y(std::move(other.m_fft_output_y)),
      m_total_ticks_pushed(other.m_total_ticks_pushed) {
    
    other.m_plan_forward = nullptr;  // Prevent double cleanup
    other.m_plan_forward_y = nullptr;
    other.m_total_ticks_pushed = 0;
}

// Move assignment
FrequencyAnalyser& FrequencyAnalyser::operator=(FrequencyAnalyser&& other) noexcept {
    if (this != &other) {
        cleanup_fftw();
        
        m_idx = other.m_idx;
        m_filled = other.m_filled;
        m_fs = other.m_fs;
        m_price = std::move(other.m_price);
        m_volume = std::move(other.m_volume);
        m_spread = std::move(other.m_spread);
        m_plan_forward = other.m_plan_forward;
        m_plan_forward_y = other.m_plan_forward_y;
        m_window = std::move(other.m_window);
        m_fft_input = std::move(other.m_fft_input);
        m_fft_input_y = std::move(other.m_fft_input_y);
        m_fft_output = std::move(other.m_fft_output);
        m_fft_output_y = std::move(other.m_fft_output_y);
        m_total_ticks_pushed = other.m_total_ticks_pushed;
        
        other.m_plan_forward = nullptr;
        other.m_plan_forward_y = nullptr;
        other.m_total_ticks_pushed = 0;
    }
    return *this;
}

// ─────────────────────── Main Interface ───────────────────────
void FrequencyAnalyser::push(double price, double volume, double spread) {
    m_price[m_idx] = price;    // Store new price at current index
    m_volume[m_idx] = volume;  // Store new volume at current index
    m_spread[m_idx] = spread;  // Store new spread at current index
    
    m_idx = (m_idx + 1) % WIN; // Move to next position, wrap around
    if (m_idx == 0) {         // If we wrapped back to start
        m_filled = true;      // Buffer is now full
    }
    
    // Increment tick counter for debugging
    m_total_ticks_pushed++;
    
    // Debug logging around tick 1000
    if (m_total_ticks_pushed >= 995 && m_total_ticks_pushed <= 1005) {
        std::cerr << "FrequencyAnalyser::push - tick " << m_total_ticks_pushed 
                  << ", idx=" << m_idx << ", filled=" << m_filled 
                  << ", price=" << price << std::endl;
    }
}
// ───────────────────────────────────────────────────────────────────────
// The compute function is the main entrypoint for the frequency analysis, 
// it takes the raw price, volume, and spread data and computes the frequency features
// ───────────────────────────────────────────────────────────────────────
bool FrequencyAnalyser::compute(hefkf_common::FrequencyFeatures& out) {
    if (!m_filled) {
        return false;  // Need full window for reliable analysis
    }
    
    // Prepare linearized arrays (handle circular buffer)
    std::vector<double> price_linear(WIN), volume_linear(WIN), spread_linear(WIN);
    std::vector<double> freq_vec;
    
    // Linearize circular buffers
    for (int i = 0; i < WIN; ++i) {
        int circular_idx = (m_idx + i) % WIN;
        price_linear[i] = m_price[circular_idx];
        volume_linear[i] = m_volume[circular_idx];
        spread_linear[i] = m_spread[circular_idx];
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
    if (!m_prev_psd_price.empty() && m_has_previous_compute) {
        out.spectral_flux = compute_spectral_flux(psd_price, m_prev_psd_price);
    } else {
        out.spectral_flux = 0.0;  // No previous data for comparison
    }
    
    // Compute band-specific entropy for market complexity analysis
    for (const auto& band : FREQ_BANDS) {
        out.entropy_by_band[band.name] = compute_band_entropy(freq_vec, psd_price, 
                                                             band.f_low, band.f_high);
    }
    
    // ═══ COHERENCE ANALYSIS ═══
    // Convert volume and spread to returns for consistent comparison
    std::vector<double> volume_returns(WIN);
    std::vector<double> spread_returns(WIN);
    volume_returns[0] = 0.0;
    spread_returns[0] = 0.0;
    
    for (int i = 1; i < WIN; ++i) {
        // Volume returns (handle zero volume)
        if (volume_linear[i-1] > EPSILON && volume_linear[i] > EPSILON) {
            volume_returns[i] = (volume_linear[i] - volume_linear[i-1]) / volume_linear[i-1];
        } else {
            volume_returns[i] = 0.0;
        }
        
        // Spread returns (handle zero/negative spread)
        if (std::abs(spread_linear[i-1]) > EPSILON && std::abs(spread_linear[i]) > EPSILON) {
            spread_returns[i] = (spread_linear[i] - spread_linear[i-1]) / std::abs(spread_linear[i-1]);
        } else {
            spread_returns[i] = 0.0;
        }
    }
    
    std::vector<double> coherence_pv, coherence_ps;
    double current_coherence_pv = coherence_estimate(price_returns.data(), volume_returns.data(), 
                                                    coherence_pv, freq_vec);
    out.coherence_price_volume_peak = current_coherence_pv;
    out.coherence_price_spread_peak = coherence_estimate(price_returns.data(), spread_returns.data(), 
                                                        coherence_ps, freq_vec);
    
    // Compute band-wise coherence
    compute_band_coherence(freq_vec, coherence_pv, out.coherence_price_volume_by_band);
    compute_band_coherence(freq_vec, coherence_ps, out.coherence_price_spread_by_band);
    
    // ═══ DERIVATIVE FEATURES (MOMENTUM DETECTION) ═══
    double current_spectral_centroid = out.spectral_centroid_price;
    
    if (m_has_previous_compute) {
        // Time delta for derivatives (assuming constant sampling rate)
        double time_delta = 1.0 / m_fs;
        
        // Compute derivatives using historical values
        out.trend_strength_derivative = compute_derivative(current_trend_strength, 
                                                          m_prev_trend_strength, time_delta);
        out.coherence_pv_derivative = compute_derivative(current_coherence_pv, 
                                                        m_prev_coherence_pv_peak, time_delta);
        out.centroid_velocity = compute_derivative(current_spectral_centroid, 
                                                  m_prev_spectral_centroid, time_delta);
    } else {
        // First computation - no derivatives available
        out.trend_strength_derivative = 0.0;
        out.coherence_pv_derivative = 0.0;
        out.centroid_velocity = 0.0;
    }
    
    // ═══ UPDATE HISTORICAL TRACKING ═══
    // Store current values for next iteration's derivative calculations
    m_prev_trend_strength = current_trend_strength;
    m_prev_coherence_pv_peak = current_coherence_pv;
    m_prev_spectral_centroid = current_spectral_centroid;
    m_prev_psd_price = psd_price;  // Store for spectral flux calculation
    m_has_previous_compute = true;
    
    return true;
}

void FrequencyAnalyser::reset() {
    m_idx = 0;
    m_filled = false;
    m_price.fill(0.0);
    m_volume.fill(0.0);
    m_spread.fill(0.0);
    
    // Reset derivative tracking
    m_prev_trend_strength = 0.0;
    m_prev_coherence_pv_peak = 0.0;
    m_prev_spectral_centroid = 0.0;
    m_prev_psd_price.clear();
    m_has_previous_compute = false;
    
    // Reset tick counter to -1 (will become 0 after initialization push)
    m_total_ticks_pushed = -1;
}

// ─────────────────────── Welch PSD Implementation ───────────────────────
// The Power Spectral Density (PSD) of a signal describes how the power of that signal is distributed across different frequencies. 
// It's a way to analyze the frequency content of a signal and identify the dominant frequencies and their respective power contributions
// Power Spectral Density (PSD) is the entrypoint for the frequency analysis, 
// it tells you about the frequency components that make up the signal and how much "power" (or energy) each frequency component contributes.
// ───────────────────────────────────────────────────────────────────────
void FrequencyAnalyser::welch_psd(const double* x, std::vector<double>& out_psd, std::vector<double>& out_freq) {
    const int seg_len = WIN / 4;      // 64 samples per segment (was WIN/2 = 128)
    const int n_segs = 4;             // Four non-overlapping segments: [0..63], [64..127], [128..191], [192..255]
    
    out_psd.clear();
    out_psd.resize(seg_len/2 + 1, 0.0);
    
    // Generate frequency vector (only once)
    if (out_freq.empty()) {
        out_freq.resize(seg_len/2 + 1);
        for (int i = 0; i < seg_len/2 + 1; ++i) {
            out_freq[i] = (i * m_fs) / seg_len;
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
        int start_idx = seg * seg_len;  // Segments: 0-63, 64-127, 128-191, 192-255
        
        // Apply Hanning window to segment
        for (int i = 0; i < seg_len; ++i) {
            double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (seg_len - 1)));
            windowed_seg[i] = x[start_idx + i] * w;
        }
        
        // Setup FFTW input
        std::copy(windowed_seg.begin(), windowed_seg.end(), m_fft_input.begin());
        
        // Execute FFT
        fftw_execute_dft_r2c(m_plan_forward, m_fft_input.data(),
                             reinterpret_cast<fftw_complex*>(seg_fft.data()));
        
        // Accumulate magnitude squared
        for (size_t i = 0; i < seg_fft.size(); ++i) {
            double mag_sq = std::norm(seg_fft[i]);
            out_psd[i] += mag_sq;
        }
    }
    
    // Average and normalize
    double norm_factor = 1.0 / (n_segs * m_fs * window_norm);
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

        std::cerr << "DEBUG: coherence_estimate called with signals at addresses " 
              << static_cast<const void*>(x) << " and " << static_cast<const void*>(y) << std::endl;
    
    // Clear buffers to prevent spillover between calls
    std::fill(m_fft_input.begin(), m_fft_input.end(), 0.0);
    std::fill(m_fft_input_y.begin(), m_fft_input_y.end(), 0.0);
    std::fill(m_fft_output.begin(), m_fft_output.end(), std::complex<double>(0.0, 0.0));
    std::fill(m_fft_output_y.begin(), m_fft_output_y.end(), std::complex<double>(0.0, 0.0));
    
    // True coherence using cross-PSD: |Pxy|² / (Pxx * Pyy)
    const int seg_len = WIN / 4;      // 64 samples per segment (was WIN/2 = 128)
    const int n_segs = 4;             // Four non-overlapping segments: [0..63], [64..127], [128..191], [192..255]
    
    out_coherence.clear();
    out_coherence.resize(seg_len/2 + 1, 0.0);
    
    // Generate frequency vector (only once)
    if (out_freq.empty()) {
        out_freq.resize(seg_len/2 + 1);
        for (int i = 0; i < seg_len/2 + 1; ++i) {
            out_freq[i] = (i * m_fs) / seg_len;
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
        int start_idx = seg * seg_len;  // Segments: 0-63, 64-127, 128-191, 192-255
        
        // Apply Hanning window to both signals
        for (int i = 0; i < seg_len; ++i) {
            double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (seg_len - 1)));
            windowed_x[i] = x[start_idx + i] * w;
            windowed_y[i] = y[start_idx + i] * w;
        }
        
        // Pre-whiten: Linear detrending to remove residual drift
        // This reduces spectral leakage into low frequencies
        double a_x = 0.0, a_y = 0.0;           // slope estimates
        for (int i = 0; i < seg_len; ++i) {    // simple un-normalized LR
            a_x += i * windowed_x[i];
            a_y += i * windowed_y[i];
        }
        a_x = (12.0 / (seg_len * (seg_len*seg_len - 1))) * a_x;
        a_y = (12.0 / (seg_len * (seg_len*seg_len - 1))) * a_y;
        
        for (int i = 0; i < seg_len; ++i) {
            windowed_x[i] -= a_x * (i - (seg_len-1)/2.0);
            windowed_y[i] -= a_y * (i - (seg_len-1)/2.0);
        }
        
        // Setup FFTW inputs
        std::copy(windowed_x.begin(), windowed_x.end(), m_fft_input.begin());
        std::copy(windowed_y.begin(), windowed_y.end(), m_fft_input_y.begin());
        
        // Execute FFTs
        fftw_execute_dft_r2c(m_plan_forward, m_fft_input.data(),
                             reinterpret_cast<fftw_complex*>(fft_x.data()));
        fftw_execute_dft_r2c(m_plan_forward_y, m_fft_input_y.data(),
                             reinterpret_cast<fftw_complex*>(fft_y.data()));
        
        // Accumulate PSDs and cross-PSD
        for (size_t i = 0; i < fft_x.size(); ++i) {
            psd_x[i] += std::norm(fft_x[i]);      // |X[i]|²
            psd_y[i] += std::norm(fft_y[i]);      // |Y[i]|²
            psd_xy[i] += std::conj(fft_x[i]) * fft_y[i];  // X*[i] * Y[i]
        }
    }
    
    // Compute coherence: |Pxy|² / (Pxx * Pyy)
    double norm_factor = 1.0 / (n_segs * m_fs * window_norm);
    
    // Find max PSD values for threshold calculation
    double max_psd_x = 0.0;
    double max_psd_y = 0.0;
    
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
        
        // Track max values
        max_psd_x = std::max(max_psd_x, psd_x[i]);
        max_psd_y = std::max(max_psd_y, psd_y[i]);
        
        if (i < 5) {  // Debug first few frequencies
            std::cerr << "DEBUG: i=" << i << ", psd_x=" << psd_x[i] 
                    << ", psd_y=" << psd_y[i] 
                    << ", |psd_xy|²=" << std::norm(psd_xy[i]) << std::endl;
        }
        
        double denom = psd_x[i] * psd_y[i];
        if (denom > EPSILON) {
            out_coherence[i] = std::norm(psd_xy[i]) / denom;  // |Pxy|² / (Pxx * Pyy)
            out_coherence[i] = std::min(1.0, out_coherence[i]); // Clamp to [0,1]
        } else {
            out_coherence[i] = 0.0;
        }
    }
    
    // Apply power threshold filtering
    double power_threshold = 0.001 * std::min(max_psd_x, max_psd_y);  // 0.1% of peak power (was 1%)
    
    // Filter coherence values where either PSD is below threshold
    for (size_t i = 0; i < out_coherence.size(); ++i) {
        if (psd_x[i] < power_threshold || psd_y[i] < power_threshold) {
            out_coherence[i] = 0.0;  // Zero out coherence for low-power bins
        }
    }
    std::cerr << "DEBUG: Coherence spectrum:" << std::endl;
    std::cerr << "  First 10: ";
    for (size_t i = 0; i < std::min(size_t(10), out_coherence.size()); ++i) {
        std::cerr << out_coherence[i] << " ";
    }
    std::cerr << std::endl;

    // Print around the expected peaks
    if (out_coherence.size() > 20) {
        std::cerr << "  Around 0.15 Hz (bins 17-22): ";
        for (size_t i = 17; i < std::min(size_t(23), out_coherence.size()); ++i) {
            std::cerr << out_coherence[i] << " ";
        }
        std::cerr << std::endl;
    }

    if (out_coherence.size() > 45) {
        std::cerr << "  Around peak (bins 38-43): ";
        for (size_t i = 38; i < std::min(size_t(44), out_coherence.size()); ++i) {
            std::cerr << i << ":" << out_coherence[i] << " ";
        }
        std::cerr << std::endl;
    }

    // Print the tail to see if there's spurious coherence
    if (out_coherence.size() > 60) {
        std::cerr << "  Tail (last 5): ";
        for (size_t i = out_coherence.size() - 5; i < out_coherence.size(); ++i) {
            std::cerr << out_coherence[i] << " ";
        }
        std::cerr << std::endl;
    }
    
    // Use band-weighted coherence instead of peak
    // Focus on low-to-medium frequency band (0.01-0.1 Hz) 
    // This captures price-volume correlations at trading-relevant timescales
    // while avoiding high-frequency noise and single-bin artifacts
    double band_coherence = band_coherence_weighted(out_coherence, out_freq, 0.01, 0.1);

    std::cerr << "DEBUG: Returning band-weighted coherence = " << band_coherence << std::endl;
    return band_coherence;
    }

// ─────────────────────── Band Coherence Weighted ───────────────────────
// This function calculates the weighted coherence of a signal over a given frequency band.
// It uses a weighted average of the coherence values in the band, with the weights being the coherence values themselves.
// The function also applies a power threshold to the coherence values, so that only bins with a coherence value greater than the threshold are included in the weighted average.
// The function also applies a coverage threshold to the coherence values, so that only bins with a coherence value greater than the threshold are included in the weighted average.
// The function also applies a smoothness exponent to the coherence values, so that the weighted average is more sensitive to changes in the coherence values.
// ───────────────────────────────────────────────────────────────────────
double FrequencyAnalyser::band_coherence_weighted(const std::vector<double>& coherence,
                                                 const std::vector<double>& freq,
                                                 double f_low, double f_high) const {
    constexpr double ACTIVE_TH  = 0.05;   // ignore bins that carry no info
    constexpr double COHERENT_TH = 0.45;  // "high‑quality" bin threshold
    constexpr double ALPHA       = 1.25;  // coverage penalty exponent (0.5-1.5, higher = stricter separation)

    double sumC = 0.0;
    std::size_t activeBins = 0, coherentBins = 0;

    for (std::size_t i = 0; i < coherence.size() && i < freq.size(); ++i) {
        if (freq[i] <  f_low || freq[i] > f_high)   continue;
        if (coherence[i] < ACTIVE_TH)               continue;   // skip dead bins

        sumC += coherence[i];
        ++activeBins;
        if (coherence[i] >= COHERENT_TH) ++coherentBins;
    }

    if (activeBins == 0)      return 0.0;            // nothing usable

    const double meanC    = sumC / static_cast<double>(activeBins);
    const double coverage = static_cast<double>(coherentBins) /
                            static_cast<double>(activeBins);

    // final score
    return meanC * std::pow(coverage, ALPHA);
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
    // Create plans for 64-sample FFTs (WIN/4) instead of 128-sample (WIN/2)
    m_plan_forward = fftw_plan_dft_r2c_1d(WIN/4, m_fft_input.data(),
                                         reinterpret_cast<fftw_complex*>(m_fft_output.data()),
                                         FFTW_MEASURE);
    
    m_plan_forward_y = fftw_plan_dft_r2c_1d(WIN/4, m_fft_input_y.data(),
                                           reinterpret_cast<fftw_complex*>(m_fft_output_y.data()),
                                           FFTW_MEASURE);
    
    if (!m_plan_forward || !m_plan_forward_y) {
        throw std::runtime_error("Failed to create FFTW plans");
    }
}

void FrequencyAnalyser::cleanup_fftw() {
    if (m_plan_forward) {
        fftw_destroy_plan(m_plan_forward);
        m_plan_forward = nullptr;
    }
    if (m_plan_forward_y) {
        fftw_destroy_plan(m_plan_forward_y);
        m_plan_forward_y = nullptr;
    }
}

void FrequencyAnalyser::init_hanning_window() {
    for (int i = 0; i < WIN; ++i) {
        m_window[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (WIN - 1)));
    }
}

void FrequencyAnalyser::apply_window(const double* input, double* windowed) const {
    for (int i = 0; i < WIN; ++i) {
        windowed[i] = input[i] * m_window[i];
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
        std::cerr << "  Total ticks pushed: " << m_total_ticks_pushed << std::endl;
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
