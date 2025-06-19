// FrequencyAnalyser - FFTW3-based frequency domain analysis for HEFKF
// Implements 2-segment non-overlapping Welch PSD estimation and coherence analysis with 256-sample rolling window
// Designed for <50µs per-tick latency with O(1) push, O(log n) compute

#ifndef FREQUENCY_ANALYSER_HPP
#define FREQUENCY_ANALYSER_HPP

#include <fftw3.h>
#include <array>
#include <vector>
#include <complex>
#include <unordered_map>
#include <string>

// ─────────────────────── Shared Frequency Features Namespace ───────────────────────
namespace hefkf_common {

// Shared FrequencyFeatures structure compatible with both 1min and 5min filters
struct FrequencyFeatures {
    // Existing core features
    double trend_strength = 0.0;
    double coherence_price_volume_peak = 0.0;  // Weighted avg coherence across trading frequencies
    double coherence_price_spread_peak = 0.0;  // Weighted avg coherence across trading frequencies
    
    // Coherence by frequency band
    std::unordered_map<std::string, double> coherence_price_volume_by_band;
    std::unordered_map<std::string, double> coherence_price_spread_by_band;
    
    // New spectral features
    double spectral_centroid_price = 0.0;
    double spectral_centroid_volume = 0.0;
    double spectral_flux = 0.0;
    std::unordered_map<std::string, double> entropy_by_band;
    
    // Derivative features for momentum detection
    double trend_strength_derivative = 0.0;
    double coherence_pv_derivative = 0.0;
    double centroid_velocity = 0.0;  // Rate of change of spectral centroid
    
    // Default constructor initializes band maps
    FrequencyFeatures();
};

} // namespace hefkf_common

// ─────────────────────── FrequencyAnalyser Class ───────────────────────
class FrequencyAnalyser {
public:
    explicit FrequencyAnalyser(double fs = 1.0);
    ~FrequencyAnalyser();
    
    // Non-copyable due to FFTW plans
    FrequencyAnalyser(const FrequencyAnalyser&) = delete;
    FrequencyAnalyser& operator=(const FrequencyAnalyser&) = delete;
    
    // Movable
    FrequencyAnalyser(FrequencyAnalyser&& other) noexcept;
    FrequencyAnalyser& operator=(FrequencyAnalyser&& other) noexcept;

    // Main interface
    void push(double price, double volume, double spread);
    bool compute(hefkf_common::FrequencyFeatures& out);    // false = not enough data
    
    // State queries
    bool is_ready() const { return m_filled; }
    int sample_count() const { return m_filled ? WIN : m_idx; }
    
    // Reset state
    void reset();

private:
    static constexpr int WIN = 256;                   // Welch segment length (matches Python default)
    static constexpr int SEG_LEN = WIN / 2;           // 128 samples per segment (non-overlapping)
    static constexpr double EPSILON = 1e-12;          // Numerical stability
    
    // Frequency band definitions (normalized frequencies, fs=1.0 assumed)
    struct FreqBand {
        double f_low, f_high;
        std::string name;
    };
    
    static const std::vector<FreqBand> FREQ_BANDS;
    
    // Rolling window state
    int m_idx = 0;                                     // circular index
    bool m_filled = false;
    double m_fs;                                       // sampling frequency
    
    // Data buffers (circular)
    std::array<double, WIN> m_price, m_volume, m_spread;
    
    // FFTW plans and buffers (reused for performance)
    fftw_plan m_plan_forward;
    fftw_plan m_plan_forward_y;  // Second plan for coherence computation
    std::vector<double> m_window;                      // Hanning window
    std::vector<double> m_fft_input;                   // Real input for FFT
    std::vector<double> m_fft_input_y;                 // Real input for second signal (coherence)
    std::vector<std::complex<double>> m_fft_output;    // Complex output from FFT
    std::vector<std::complex<double>> m_fft_output_y;  // Complex output for second signal
    
    // Frequency analysis helpers
    void welch_psd(const double* x, std::vector<double>& out_psd, std::vector<double>& out_freq);
    double band_power(const std::vector<double>& freq, const std::vector<double>& psd, 
                     double f_low, double f_high) const;
    double coherence_estimate(const double* x, const double* y, 
                            std::vector<double>& out_coherence, std::vector<double>& out_freq);

    double band_coherence_weighted(const std::vector<double>& coherence,
                                  const std::vector<double>& freq,
                                  double f_low, double f_high) const;
    
    // Frequency band analysis
    void compute_band_coherence(const std::vector<double>& freq, const std::vector<double>& coherence,
                               std::unordered_map<std::string, double>& band_map) const;
    
    // Spectral feature analysis
    double compute_spectral_centroid(const std::vector<double>& freq, const std::vector<double>& psd) const;
    double compute_spectral_flux(const std::vector<double>& current_psd, const std::vector<double>& previous_psd) const;
    double compute_band_entropy(const std::vector<double>& freq, const std::vector<double>& psd, 
                               double f_low, double f_high) const;
    
    // Derivative computation
    double compute_derivative(double current_value, double previous_value, double time_delta) const;
    
    // Trend analysis
    double compute_trend_strength(const double* price_data) const;
    
    // Window functions
    void init_hanning_window();
    void apply_window(const double* input, double* windowed) const;
    
    // Initialization helpers
    void init_fftw_plans();
    void cleanup_fftw();
    
    // Historical tracking for derivatives
    double m_prev_trend_strength = 0.0;
    double m_prev_coherence_pv_peak = 0.0;
    double m_prev_spectral_centroid = 0.0;
    std::vector<double> m_prev_psd_price;  // For spectral flux calculation
    bool m_has_previous_compute = false;   // Flag for first computation
    
    // Debug tick counter
    size_t m_total_ticks_pushed = 0;  // Track total ticks for boundary debugging
    
    // Goertzel-based micro-resolution spectral analysis for entropy
    static constexpr double MICRO_F[] = {
        0.004, 0.0065, 0.009, 0.0115, 0.014, 0.0165
    };  // 6 frequencies spanning 1-5 min band
    
    void micro_psd_goertzel(const double* x, std::array<double, 6>& P) const;
    double entropy6(const std::array<double, 6>& P) const;
};

#endif // FREQUENCY_ANALYSER_HPP 