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
#include <chrono>
#include <memory>
#include <array>

// ─────────────────────── Extended KalmanTick Structure ───────────────────────
struct KalmanTick {
    double px = 0.0;              // price (existing)
    double volume = 0.0;          // volume (added)
    double spread = 0.0;          // bid-ask spread (added)
    std::chrono::system_clock::time_point ts;  // timestamp (existing)
    
    // Additional fields that might be useful
    double bid = 0.0;
    double ask = 0.0;
    int64_t trade_count = 0;
    
    // Validation
    bool is_valid() const {
        return px > 0.0 && volume >= 0.0 && spread >= 0.0 && 
               !std::isnan(px) && !std::isnan(volume) && !std::isnan(spread);
    }
};

// ─────────────────────── Ring Buffer Template (Simplified) ───────────────────────
template<typename T, size_t N>
class StaticRingBuffer {
private:
    std::array<T, N> buffer_;
    size_t head_ = 0;
    size_t size_ = 0;
    
public:
    void push(const T& item) {
        buffer_[head_] = item;
        head_ = (head_ + 1) % N;
        if (size_ < N) size_++;
    }
    
    bool empty() const { return size_ == 0; }
    size_t size() const { return size_; }
    
    const T& latest() const {
        if (empty()) throw std::runtime_error("Buffer is empty");
        size_t latest_idx = (head_ == 0) ? N - 1 : head_ - 1;
        return buffer_[latest_idx];
    }
    
    const T& operator[](size_t idx) const {
        if (idx >= size_) throw std::out_of_range("Index out of range");
        size_t actual_idx = (head_ + N - size_ + idx) % N;
        return buffer_[actual_idx];
    }
};

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
        hefkf_common::BucketConfidence bucket_conf_1min;
        hefkf_common::BucketConfidence bucket_conf_5min;
        bool freq_ready = false;
        std::chrono::system_clock::time_point timestamp;
    };
    
    PipelineOutput process(const KalmanTick& tick);
    
    // Reset all components
    void reset();
    
    // State queries
    bool is_initialized() const { return initialized_; }
    bool is_frequency_ready() const { return analyser_.is_ready(); }
    int frequency_sample_count() const { return analyser_.sample_count(); }
    
    // Get current bucket confidences (for external use)
    const hefkf_common::BucketConfidence& get_current_bucket_1min() const { return current_bucket_1min_; }
    const hefkf_common::BucketConfidence& get_current_bucket_5min() const { return current_bucket_5min_; }
    
    // Getters for testing
    const hefkf_1min::OneMinuteHEFKF& get_kf_1min() const { return kf_1min_; }
    const hefkf_5min::FiveMinuteHEFKF& get_kf_5min() const { return kf_5min_; }

private:
    // Core components
    FrequencyAnalyser analyser_;
    hefkf_1min::OneMinuteHEFKF kf_1min_;
    hefkf_5min::FiveMinuteHEFKF kf_5min_;
    AnalyticScorer scorer_;
    
    // State tracking
    bool initialized_ = false;
    double dt_ = 1.0;  // Time step
    std::chrono::system_clock::time_point last_tick_time_;
    double sample_rate_;
    PipelineOutput current_output_;
    
    // Debug tick counter
    size_t total_ticks_processed_ = 0;  // Track total ticks for debugging
    
    // Bucket confidence state (carries forward between ticks)
    hefkf_common::BucketConfidence current_bucket_1min_;
    hefkf_common::BucketConfidence current_bucket_5min_;
    
    // Helper methods
    PipelineOutput create_output(const hefkf_1min::FilterOutput& out_1min,
                                const hefkf_5min::FilterOutput& out_5min,
                                const hefkf_common::FrequencyFeatures& freq_features,
                                const KalmanTick& tick);
    
    double compute_quality_factor(const hefkf_common::FrequencyFeatures& freq_features);
    void update_bucket_confidences(const hefkf_common::FrequencyFeatures& freq_features,
                                  double current_price);
};

// ─────────────────────── Processing Loop Example ───────────────────────
class ProcessingLoop {
public:
    explicit ProcessingLoop(size_t buffer_size = 4096);
    ~ProcessingLoop() = default;
    
    // Add tick to ring buffer
    void add_tick(const KalmanTick& tick);
    
    // Main processing loop (call repeatedly)
    bool process_latest();
    
    // Get latest outputs
    const FilterPipeline::PipelineOutput& get_latest_output() const { return latest_output_; }
    
    // Control
    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool is_running() const { return running_; }
    
    // Statistics
    size_t get_processed_count() const { return processed_count_; }
    size_t get_buffer_size() const { return ring_buffer_.size(); }

private:
    StaticRingBuffer<KalmanTick, 4096> ring_buffer_;
    FilterPipeline pipeline_;
    FilterPipeline::PipelineOutput latest_output_;
    
    bool running_ = false;
    size_t processed_count_ = 0;
    size_t last_processed_idx_ = SIZE_MAX;
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