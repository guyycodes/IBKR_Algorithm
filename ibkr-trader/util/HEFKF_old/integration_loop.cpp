// Integration Loop Implementation - Complete Pipeline
// Single-threaded processing with FrequencyAnalyser + posterior extraction + dual Kalman filters

#include "integration_loop.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>

// ─────────────────────── FilterPipeline Implementation ───────────────────────
FilterPipeline::FilterPipeline(double sampling_frequency) 
    : analyser_(sampling_frequency), dt_(1.0 / sampling_frequency) {
    
    // Initialize bucket confidences to uniform distributions
    double uniform_prob = 1.0 / 8.0;
    current_bucket_1min_.up_001_002 = uniform_prob;
    current_bucket_1min_.up_002_005 = uniform_prob;
    current_bucket_1min_.up_005_010 = uniform_prob;
    current_bucket_1min_.up_010_plus = uniform_prob;
    current_bucket_1min_.dn_001_002 = uniform_prob;
    current_bucket_1min_.dn_002_005 = uniform_prob;
    current_bucket_1min_.dn_005_010 = uniform_prob;
    current_bucket_1min_.dn_010_plus = uniform_prob;
    
    current_bucket_5min_ = current_bucket_1min_;  // Same initial distribution
}

void FilterPipeline::initialize(const KalmanTick& initial_tick) {
    if (!initial_tick.is_valid()) {
        throw std::invalid_argument("Invalid initial tick");
    }
    
    // Create initial MarketData for both filters
    hefkf_1min::MarketData md_1min;
    md_1min.price = initial_tick.px;
    md_1min.volume = initial_tick.volume;
    md_1min.spread = initial_tick.spread;
    md_1min.timestamp = initial_tick.ts;
    
    hefkf_5min::MarketData md_5min;
    md_5min.price = initial_tick.px;
    md_5min.volume = initial_tick.volume;
    md_5min.spread = initial_tick.spread;
    md_5min.timestamp = initial_tick.ts;
    
    // Initialize both Kalman filters
    kf_1min_.initialize(md_1min, dt_);
    kf_5min_.initialize(md_5min, dt_);
    
    // Push first data point to frequency analyser
    analyser_.push(initial_tick.px, initial_tick.volume, initial_tick.spread);
    
    initialized_ = true;
}

FilterPipeline::PipelineOutput FilterPipeline::process(const KalmanTick& tick) {
    if (!initialized_) {
        throw std::runtime_error("Pipeline not initialized. Call initialize() first.");
    }
    
    if (!tick.is_valid()) {
        throw std::invalid_argument("Invalid tick data");
    }
    
    // ═══ STEP 1: Push to Frequency Analyser ═══
    analyser_.push(tick.px, tick.volume, tick.spread);
    
    // ═══ STEP 2: Compute Frequency Features ═══
    hefkf_common::FrequencyFeatures freq_features;
    bool freq_ready = analyser_.compute(freq_features);
    
    // ═══ STEP 3: Create MarketData with Bucket Confidence ═══
    // For 1min filter
    hefkf_1min::MarketData md_1min = create_market_data_1min(
        tick.px, tick.volume, tick.spread, 
        current_bucket_1min_, freq_features
    );
    md_1min.timestamp = tick.ts;
    
    // For 5min filter
    hefkf_5min::MarketData md_5min = create_market_data_5min(
        tick.px, tick.volume, tick.spread, 
        current_bucket_5min_, freq_features
    );
    md_5min.timestamp = tick.ts;
    
    // ═══ STEP 4: Process Through Kalman Filters ═══
    hefkf_1min::FilterOutput output_1min = kf_1min_.process(md_1min);
    hefkf_5min::FilterOutput output_5min = kf_5min_.process(md_5min);
    
    // ═══ STEP 5: Extract Posterior & Apply Dirichlet Sharpening ═══
    if (freq_ready) {
        update_bucket_confidences(freq_features, tick.px);
    }
    
    // ═══ STEP 6: Create Output ═══
    return create_output(output_1min, output_5min, freq_features, tick);
}

void FilterPipeline::reset() {
    kf_1min_.reset();
    kf_5min_.reset();
    analyser_.reset();
    initialized_ = false;
    
    // Reset bucket confidences to uniform
    double uniform_prob = 1.0 / 8.0;
    current_bucket_1min_ = hefkf_common::BucketConfidence{};
    current_bucket_1min_.up_001_002 = uniform_prob;
    current_bucket_1min_.up_002_005 = uniform_prob;
    current_bucket_1min_.up_005_010 = uniform_prob;
    current_bucket_1min_.up_010_plus = uniform_prob;
    current_bucket_1min_.dn_001_002 = uniform_prob;
    current_bucket_1min_.dn_002_005 = uniform_prob;
    current_bucket_1min_.dn_005_010 = uniform_prob;
    current_bucket_1min_.dn_010_plus = uniform_prob;
    
    current_bucket_5min_ = current_bucket_1min_;
}

FilterPipeline::PipelineOutput FilterPipeline::create_output(
    const hefkf_1min::FilterOutput& out_1min,
    const hefkf_5min::FilterOutput& out_5min,
    const hefkf_common::FrequencyFeatures& freq_features,
    const KalmanTick& tick) {
    
    PipelineOutput output;
    output.output_1min = out_1min;
    output.output_5min = out_5min;
    output.freq_features = freq_features;
    output.bucket_conf_1min = current_bucket_1min_;
    output.bucket_conf_5min = current_bucket_5min_;
    output.freq_ready = analyser_.is_ready();
    output.timestamp = tick.ts;
    
    return output;
}

double FilterPipeline::compute_quality_factor(const hefkf_common::FrequencyFeatures& freq_features) {
    // Compute quality factor as specified in the user's code
    double pv_peak = freq_features.coherence_price_volume_peak;
    double pv_micro = 0.0;
    
    auto it = freq_features.coherence_price_volume_by_band.find("microstructure");
    if (it != freq_features.coherence_price_volume_by_band.end()) {
        pv_micro = it->second;
    }
    
    return 0.5 * (pv_peak + pv_micro);
}

void FilterPipeline::update_bucket_confidences(const hefkf_common::FrequencyFeatures& freq_features,
                                              double current_price) {
    // Extract posterior bucket probabilities from both filters
    hefkf_common::BucketConfidence post_1min = posterior_from_KF(kf_1min_, current_price, dt_);
    hefkf_common::BucketConfidence post_5min = posterior_from_KF(kf_5min_, current_price, dt_);
    
    // Use AnalyticScorer to enhance buckets based on market regime
    double price_velocity = kf_1min_.get_state()(1);  // Get velocity from 1min filter
    
    // score_simple()
    //   ├── Detect regime (Bull/Bear/Sideways)
    //   ├── Apply directional bias (redistribute probability)
    //   ├── Normalize the distribution
    //   └── Call sharpen_dirichlet() to concentrate probabilities
    // Score and enhance 1min buckets
    auto scoring_result_1min = scorer_.score_simple(
        freq_features, 
        post_1min, 
        price_velocity,
        true  // is_1min_filter
    );
    
    // Debug: Log regime detection for first few updates
    static int debug_count = 0;
    if (debug_count < 5) {
        std::cout << "Debug: velocity=" << price_velocity 
                  << ", trend=" << freq_features.trend_strength
                  << ", raw_up=" << (post_1min.up_001_002 + post_1min.up_002_005 + 
                                    post_1min.up_005_010 + post_1min.up_010_plus)
                  << ", enhanced_up=" << (scoring_result_1min.enhanced_buckets.up_001_002 + 
                                         scoring_result_1min.enhanced_buckets.up_002_005 +
                                         scoring_result_1min.enhanced_buckets.up_005_010 + 
                                         scoring_result_1min.enhanced_buckets.up_010_plus)
                  << std::endl;
        debug_count++;
    }
    
    // Score and enhance 5min buckets  
    auto scoring_result_5min = scorer_.score_simple(
        freq_features,
        post_5min,
        price_velocity,
        false  // is_5min_filter
    );
    
    // Update current bucket confidences with enhanced versions
    current_bucket_1min_ = scoring_result_1min.enhanced_buckets;
    current_bucket_5min_ = scoring_result_5min.enhanced_buckets;
}

// ─────────────────────── ProcessingLoop Implementation ───────────────────────
ProcessingLoop::ProcessingLoop(size_t /* buffer_size */) : pipeline_(1.0) {
    // buffer_size parameter is ignored since we're using fixed-size StaticRingBuffer
    // In a real implementation, you might make this configurable
}

void ProcessingLoop::add_tick(const KalmanTick& tick) {
    ring_buffer_.push(tick);
}

bool ProcessingLoop::process_latest() {
    if (!running_ || ring_buffer_.empty()) {
        return false;
    }
    
    try {
        const KalmanTick& latest_tick = ring_buffer_.latest();
        
        // Initialize pipeline with first tick if needed
        if (!pipeline_.is_initialized()) {
            pipeline_.initialize(latest_tick);
            processed_count_++;
            return true;
        }
        
        // Process the latest tick through the complete pipeline
        latest_output_ = pipeline_.process(latest_tick);
        processed_count_++;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "ProcessingLoop error: " << e.what() << std::endl;
        return false;
    }
}

// ─────────────────────── Utility Functions Implementation ───────────────────────
namespace integration_utils {

KalmanTick create_tick(double price, double volume, double spread,
                      const std::chrono::system_clock::time_point& timestamp) {
    KalmanTick tick;
    tick.px = price;
    tick.volume = volume;
    tick.spread = spread;
    tick.ts = timestamp;
    
    // Calculate bid/ask from mid and spread
    tick.bid = price - spread / 2.0;
    tick.ask = price + spread / 2.0;
    tick.trade_count = 1;  // Default
    
    return tick;
}

KalmanTick create_tick_with_bid_ask(double bid, double ask, double volume,
                                   const std::chrono::system_clock::time_point& timestamp) {
    KalmanTick tick;
    tick.bid = bid;
    tick.ask = ask;
    tick.px = (bid + ask) / 2.0;  // Mid price
    tick.spread = ask - bid;
    tick.volume = volume;
    tick.ts = timestamp;
    tick.trade_count = 1;  // Default
    
    return tick;
}

void print_pipeline_output(const FilterPipeline::PipelineOutput& output) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "=== Pipeline Output ===" << std::endl;
    std::cout << "Timestamp: " << std::chrono::duration_cast<std::chrono::milliseconds>(
        output.timestamp.time_since_epoch()).count() << "ms" << std::endl;
    
    std::cout << "1min Filter:" << std::endl;
    std::cout << "  Price: " << output.output_1min.price_smoothed 
              << " | Velocity: " << output.output_1min.price_velocity
              << " | Lambda: " << output.output_1min.lambda_used << std::endl;
    
    std::cout << "5min Filter:" << std::endl;
    std::cout << "  Price: " << output.output_5min.price_smoothed 
              << " | Velocity: " << output.output_5min.price_velocity
              << " | Lambda: " << output.output_5min.lambda_used << std::endl;
    
    if (output.freq_ready) {
        std::cout << "Frequency Features:" << std::endl;
        std::cout << "  Trend Strength: " << output.freq_features.trend_strength << std::endl;
        std::cout << "  PV Coherence Peak: " << output.freq_features.coherence_price_volume_peak << std::endl;
        
        auto it = output.freq_features.coherence_price_volume_by_band.find("microstructure");
        if (it != output.freq_features.coherence_price_volume_by_band.end()) {
            std::cout << "  PV Microstructure: " << it->second << std::endl;
        }
    }
    
    std::cout << "Bucket Confidence (1min):" << std::endl;
    std::cout << "  UP: " << output.bucket_conf_1min.up_001_002 << " | "
              << output.bucket_conf_1min.up_002_005 << " | "
              << output.bucket_conf_1min.up_005_010 << " | "
              << output.bucket_conf_1min.up_010_plus << std::endl;
    std::cout << "  DN: " << output.bucket_conf_1min.dn_001_002 << " | "
              << output.bucket_conf_1min.dn_002_005 << " | "
              << output.bucket_conf_1min.dn_005_010 << " | "
              << output.bucket_conf_1min.dn_010_plus << std::endl;
    
    std::cout << "========================" << std::endl;
}

bool validate_tick_sequence(const std::vector<KalmanTick>& ticks) {
    if (ticks.empty()) return true;
    
    for (size_t i = 1; i < ticks.size(); ++i) {
        // Check that timestamps are non-decreasing
        if (ticks[i].ts < ticks[i-1].ts) {
            std::cerr << "Tick sequence validation failed: timestamps not in order at index " 
                      << i << std::endl;
            return false;
        }
        
        // Check that all ticks are valid
        if (!ticks[i].is_valid()) {
            std::cerr << "Tick sequence validation failed: invalid tick at index " 
                      << i << std::endl;
            return false;
        }
    }
    
    return true;
}

} // namespace integration_utils 