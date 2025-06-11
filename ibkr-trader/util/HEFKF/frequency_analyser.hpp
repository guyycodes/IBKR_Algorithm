// FrequencyAnalyser - FFTW3-based frequency domain analysis for HEFKF
// Implements 2-segment non-overlapping Welch PSD estimation and coherence analysis with 256-sample rolling window
// Designed for <50µs per-tick latency with O(1) push, O(log n) compute
// ═══════════════════════════════════ COMPREHENSIVE FEATURE USAGE CHART ═══════════════════════════════════

// Feature	                    Used in Simple Scoring?	      Used in Complex Regime?	        Used in Quality Factor?  Used with 1min filter?    Used with 5min filter?
// coherence_price_volume_peak	✅ Yes (weight: 0.4-0.6)	     ✅ Yes (breakout detection)	      ✅ Yes (core quality)    ✅ Yes (filter knobs)     ✅ Yes (filter knobs)
// spectral_flux	            ✅ Yes (weight: 0.1)	         ✅ Yes (high-vol detection)	      ✅ Yes (freq quality)    ✅ Yes (volatility alert) ✅ Yes (volatility alert)
// trend_strength_derivative	✅ Yes (weight: 0.05-0.15)	 ✅ Yes (bull/bear detection)	  ✅ Yes (trend quality)   ✅ Yes (directional bias) ✅ Yes (directional bias)
// centroid_velocity	        ✅ Yes (via enhanced quality) ✅ Yes (reversal detection)	      ✅ Yes (freq quality)    ✅ Yes (freq instability) ✅ Yes (freq instability)
// spectral_centroid (price)    ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (freq quality)    ✅ Yes (filter knobs)     ✅ Yes (filter knobs)
// spectral_centroid (volume)   ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (freq quality)    ✅ Yes (volatility alert) ✅ Yes (volatility alert)
// entropy (all 4 bands)        ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (entropy quality) ✅ Yes (volatility alert) ✅ Yes (volatility alert)
// trend_strength               ✅ Yes (weight: varies)       ✅ Yes (regime classification)    ✅ Yes (trend quality)   ✅ Yes (regime detect)    ✅ Yes (regime detect)
// coherence_price_spread_peak  ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (core quality)    ✅ Yes (filter knobs)     ✅ Yes (filter knobs)

// 📊 Enhanced Quality Signal Breakdown (ALL FEATURES NOW USED):
// Component	        Weight  Features Used	                                                Purpose
// Core Quality	        40%	    (coher_pv×0.7 + coher_ps×0.3) × (1-entropy_short)	            Dual-coherence signal validation
// Trend Quality	    25%	    trend_strength×0.7 + d_trend×0.3	                            Directional stability
// Freq Quality	        20%	    (1-flux)×0.4 + centroid_price×0.25 + centroid_volume×0.15 + (1-centroid_velocity)×0.2	Dual-centroid spectral stability
// Entropy Quality	    15%	    All 4 entropy bands (micro, short, medium, trend)	            Multi-timeframe noise

// ⚡ Volatility Alert Breakdown (ENHANCED WITH NEW FEATURES):
// Component	        Weight	Features Used	                                Purpose
// Flux Alert	        50%	    spectral_flux	                                Primary volatility indicator
// Freq Instability	    30%	    centroid_velocity×0.6 + entropy_trend×0.4	    Frequency domain chaos
// Entropy Chaos	    20%	    entropy_micro + entropy_short + entropy_medium	Multi-timeframe noise

// 🔧 Filter Knob Adjustments (NEW INTEGRATION):
// Parameter	        Formula	                                            Features Used                            Purpose
// bucket_weight	    base + 0.30×quality - 0.20×volatility	            Enhanced quality + volatility alert      Dynamic u-channel gain
// freq_domain_weight	base + 0.15×quality	                                Enhanced quality signal                  Spectral nudging strength
// lambda_fixed	        base - 0.02×volatility (if vol>0.6)	                Volatility alert signal                  Adaptive forgetting rate

// 🎯 SUMMARY: Feature Coverage
// ✅ FULLY UTILIZED: coherence_pv_peak, coherence_ps_peak, spectral_flux, trend_strength_derivative, 
//                   centroid_velocity, spectral_centroid_price, spectral_centroid_volume, entropy_bands, trend_strength
// 📈 INTEGRATION STATUS: ALL 9 major spectral features now actively drive filter parameters and scoring

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
    double coherence_price_volume_peak = 0.0;
    double coherence_price_spread_peak = 0.0;
    
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
    bool is_ready() const { return filled_; }
    int sample_count() const { return filled_ ? WIN : idx_; }
    
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
    int idx_ = 0;                                     // circular index
    bool filled_ = false;
    double fs_;                                       // sampling frequency
    
    // Data buffers (circular)
    std::array<double, WIN> price_, volume_, spread_;
    
    // FFTW plans and buffers (reused for performance)
    fftw_plan plan_forward_;
    fftw_plan plan_forward_y_;  // Second plan for coherence computation
    std::vector<double> window_;                      // Hanning window
    std::vector<double> fft_input_;                   // Real input for FFT
    std::vector<double> fft_input_y_;                 // Real input for second signal (coherence)
    std::vector<std::complex<double>> fft_output_;    // Complex output from FFT
    std::vector<std::complex<double>> fft_output_y_;  // Complex output for second signal
    
    // Frequency analysis helpers
    void welch_psd(const double* x, std::vector<double>& out_psd, std::vector<double>& out_freq);
    double band_power(const std::vector<double>& freq, const std::vector<double>& psd, 
                     double f_low, double f_high) const;
    double coherence_estimate(const double* x, const double* y, 
                            std::vector<double>& out_coherence, std::vector<double>& out_freq);
    double find_peak_coherence(const std::vector<double>& coherence) const;
    
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
    double prev_trend_strength_ = 0.0;
    double prev_coherence_pv_peak_ = 0.0;
    double prev_spectral_centroid_ = 0.0;
    std::vector<double> prev_psd_price_;  // For spectral flux calculation
    bool has_previous_compute_ = false;   // Flag for first computation
};

#endif // FREQUENCY_ANALYSER_HPP 


