// Integration Loop - Complete Frequency-Aware Bucket Confidence Pipeline
// Brings together FrequencyAnalyser, posterior extraction, and 1min/5min Kalman filters
// Single-threaded processing loop with closed-loop bucket confidence sharpening

#ifndef INTEGRATION_LOOP_HPP
#define INTEGRATION_LOOP_HPP

#include "frequency_analyser.hpp"
#include "posterior.hpp"
#include "1min_HEFKF.hpp"
#include "5min_HEFKF.hpp"
#include "analytic_scorer.hpp"
#include "static_ring_buffer.hpp"
#include "kalman_tick.hpp"
#include <chrono>
#include <memory>
#include <array>


// ─────────────────────── Integrated Filter Pipeline ───────────────────────
class FilterPipeline {
public:
    // Constructor
    explicit FilterPipeline(double sampling_frequency = 1.0);
    
    // Destructor
    ~FilterPipeline() = default;
    
    // Non-copyable, movable
    FilterPipeline(const FilterPipeline&) = delete;
    FilterPipeline& operator=(const FilterPipeline&) = delete;
    FilterPipeline(FilterPipeline&&) = default;
    FilterPipeline& operator=(FilterPipeline&&) = default;
    
    // Initialize with first tick
    void initialize(const KalmanTick& initial_tick);
    
    // Process single tick through the complete pipeline
    struct PipelineOutput {
        hefkf_1min::FilterOutput output_1min;
        hefkf_5min::FilterOutput output_5min;
        hefkf_common::FrequencyFeatures freq_features;
        hefkf_common::BucketConfidence20 bucket_conf_1min;
        hefkf_common::BucketConfidence20 bucket_conf_5min;
        bool freq_ready = false;
        std::chrono::system_clock::time_point timestamp;
    };
    
    PipelineOutput process(const KalmanTick& tick);
    
    // Reset all components
    void reset();
    
    // State queries
    bool is_initialized() const { return m_initialized; }
    bool is_frequency_ready() const { return m_analyser.is_ready(); }
    int frequency_sample_count() const { return m_analyser.sample_count(); }
    
    // Get current bucket confidences (for external use)
    const hefkf_common::BucketConfidence20& get_current_bucket_1min() const { return m_current_bucket_1min; }
    const hefkf_common::BucketConfidence20& get_current_bucket_5min() const { return m_current_bucket_5min; }
    
    // Getters for testing
    const hefkf_1min::OneMinuteHEFKF& get_kf_1min() const { return m_kf_1min; }
    const hefkf_5min::FiveMinuteHEFKF& get_kf_5min() const { return m_kf_5min; }

private:
    // Core components
    FrequencyAnalyser m_analyser;
    hefkf_1min::OneMinuteHEFKF m_kf_1min;
    hefkf_5min::FiveMinuteHEFKF m_kf_5min;
    AnalyticScorer m_scorer;
    
    // State tracking
    bool m_initialized = false;
    double m_dt = 1.0;  // Time step
    std::chrono::system_clock::time_point m_last_tick_time;
    double m_sample_rate;
    PipelineOutput m_current_output;
    
    // Debug tick counter
    size_t m_total_ticks_processed = 0;  // Track total ticks for debugging
    
    // Bucket confidence state (carries forward between ticks)
    hefkf_common::BucketConfidence20 m_current_bucket_1min;
    hefkf_common::BucketConfidence20 m_current_bucket_5min;
    
    // Helper methods
    PipelineOutput create_output(const hefkf_1min::FilterOutput& out_1min,
                                const hefkf_5min::FilterOutput& out_5min,
                                const hefkf_common::FrequencyFeatures& freq_features,
                                const KalmanTick& tick);
    
    double compute_quality_factor(const hefkf_common::FrequencyFeatures& freq_features);
    void update_bucket_confidences(const hefkf_common::FrequencyFeatures& freq_features,
                                  double current_price);
};

// ─────────────────────── Processing Loop ───────────────────────
class ProcessingLoop {
public:
    explicit ProcessingLoop(size_t buffer_size = 4096);
    
    ~ProcessingLoop() = default;
    
    // Add tick to ring buffer
    void add_tick(const KalmanTick& tick);
    
    // Main processing loop (call repeatedly)
    bool process_latest();
    
    // Get latest outputs
    const FilterPipeline::PipelineOutput& get_latest_output() const { return m_latest_output; }
    
    // Control
    void start() { m_running = true; }
    void stop() { m_running = false; }
    bool is_running() const { return m_running; }
    
    // Statistics
    size_t get_processed_count() const { return m_processed_count; }
    size_t get_buffer_size() const { return m_ring_buffer.size(); }
    
    // Debug mode
    void set_debug_mode(bool enable) { m_debug_mode = enable; }

private:
    StaticRingBuffer<KalmanTick, 4096> m_ring_buffer;
    FilterPipeline m_pipeline;
    FilterPipeline::PipelineOutput m_latest_output;
    
    bool m_running = false;
    size_t m_processed_count = 0;
    size_t m_last_processed_idx = SIZE_MAX;
    bool m_debug_mode = false;
    
    // Timing tracking for Hz measurement
    std::chrono::steady_clock::time_point m_first_tick_time;
    std::chrono::steady_clock::time_point m_last_tick_time;
};

// ─────────────────────── Utility Functions ───────────────────────
namespace integration_utils {
    
    // Create KalmanTick from basic market data
    KalmanTick create_tick(double price, double volume, double spread,
                          const std::chrono::system_clock::time_point& timestamp = std::chrono::system_clock::now());
    
    // Create KalmanTick with bid/ask
    KalmanTick create_tick_with_bid_ask(double bid, double ask, double volume,
                                       const std::chrono::system_clock::time_point& timestamp = std::chrono::system_clock::now());
    
    // Print pipeline output for debugging
    void print_pipeline_output(const FilterPipeline::PipelineOutput& output);
    
    // Validate tick sequence for timing consistency
    bool validate_tick_sequence(const std::vector<KalmanTick>& ticks);
}

#endif // INTEGRATION_LOOP_HPP 