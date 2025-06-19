// Integration Loop Implementation - Complete Pipeline
// Single-threaded processing with FrequencyAnalyser + posterior extraction + dual Kalman filters

#include "integration_loop.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>

// ─────────────────────── FilterPipeline Implementation ───────────────────────
FilterPipeline::FilterPipeline(double sampling_frequency) 
    : m_analyser(sampling_frequency), m_dt(1.0 / sampling_frequency) {
    
    // Initialize bucket confidences to uniform distributions
    // CHANGED: 20 buckets instead of 8, so uniform probability is 1/20
    double uniform_prob = 1.0 / 20.0;
    
    // Initialize all 20 UP buckets (indices 0-9)
    m_current_bucket_1min.up_000 = uniform_prob;
    m_current_bucket_1min.up_010 = uniform_prob;
    m_current_bucket_1min.up_020 = uniform_prob;
    m_current_bucket_1min.up_030 = uniform_prob;
    m_current_bucket_1min.up_040 = uniform_prob;
    m_current_bucket_1min.up_050 = uniform_prob;
    m_current_bucket_1min.up_060 = uniform_prob;
    m_current_bucket_1min.up_070 = uniform_prob;
    m_current_bucket_1min.up_080 = uniform_prob;
    m_current_bucket_1min.up_090 = uniform_prob;
    
    // Initialize all 20 DOWN buckets (indices 0-9)
    m_current_bucket_1min.dn_000 = uniform_prob;
    m_current_bucket_1min.dn_010 = uniform_prob;
    m_current_bucket_1min.dn_020 = uniform_prob;
    m_current_bucket_1min.dn_030 = uniform_prob;
    m_current_bucket_1min.dn_040 = uniform_prob;
    m_current_bucket_1min.dn_050 = uniform_prob;
    m_current_bucket_1min.dn_060 = uniform_prob;
    m_current_bucket_1min.dn_070 = uniform_prob;
    m_current_bucket_1min.dn_080 = uniform_prob;
    m_current_bucket_1min.dn_090 = uniform_prob;
    
    m_current_bucket_5min = m_current_bucket_1min;  // Same initial distribution
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
    md_1min.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(initial_tick.ts));
    
    hefkf_5min::MarketData md_5min;
    md_5min.price = initial_tick.px;
    md_5min.volume = initial_tick.volume;
    md_5min.spread = initial_tick.spread;
    md_5min.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(initial_tick.ts));
    
    // Initialize both Kalman filters
    m_kf_1min.initialize(md_1min, m_dt);
    m_kf_5min.initialize(md_5min, m_dt);
    
    // Push first data point to frequency analyser
    m_analyser.push(initial_tick.px, initial_tick.volume, initial_tick.spread);
    
    m_initialized = true;
}

FilterPipeline::PipelineOutput FilterPipeline::process(const KalmanTick& tick) {
    if (!m_initialized) {
        throw std::runtime_error("Pipeline not initialized. Call initialize() first.");
    }
    
    if (!tick.is_valid()) {
        throw std::invalid_argument("Invalid tick data");
    }
    
    // ═══ STEP 1: Push to Frequency Analyser ═══
    m_analyser.push(tick.px, tick.volume, tick.spread);
    
    // ═══ STEP 2: Compute Frequency Features ═══
    hefkf_common::FrequencyFeatures freq_features;
    bool freq_ready = m_analyser.compute(freq_features);
    
    // ═══ STEP 3: Create MarketData with Bucket Confidence ═══
    // For 1min filter - CHANGED: Using 20-bucket version
    hefkf_1min::MarketData md_1min = create_market_data_1min_20bucket(
        tick.px, tick.volume, tick.spread, 
        m_current_bucket_1min, freq_features
    );
    md_1min.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(tick.ts));
    
    // For 5min filter - CHANGED: Using 20-bucket version
    hefkf_5min::MarketData md_5min = create_market_data_5min_20bucket(
        tick.px, tick.volume, tick.spread, 
        m_current_bucket_5min, freq_features
    );
    md_5min.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(tick.ts));
    
    // ═══ STEP 4: Process Through Kalman Filters ═══
    hefkf_1min::FilterOutput output_1min = m_kf_1min.process(md_1min);
    hefkf_5min::FilterOutput output_5min = m_kf_5min.process(md_5min);
    
    // ═══ STEP 5: Extract Posterior & Apply Dirichlet Sharpening ═══
    if (freq_ready) {
        update_bucket_confidences(freq_features, tick.px);
    }
    
    // ═══ STEP 6: Create Output ═══
    return create_output(output_1min, output_5min, freq_features, tick);
}

void FilterPipeline::reset() {
    m_kf_1min.reset();
    m_kf_5min.reset();
    m_analyser.reset();
    m_initialized = false;
    
    // Reset bucket confidences to uniform
    // CHANGED: 20 buckets instead of 8, so uniform probability is 1/20
    double uniform_prob = 1.0 / 20.0;
    m_current_bucket_1min = hefkf_common::BucketConfidence20{};  // CHANGED: Use BucketConfidence20
    
    // Initialize all 20 UP buckets (indices 0-9)
    m_current_bucket_1min.up_000 = uniform_prob;
    m_current_bucket_1min.up_010 = uniform_prob;
    m_current_bucket_1min.up_020 = uniform_prob;
    m_current_bucket_1min.up_030 = uniform_prob;
    m_current_bucket_1min.up_040 = uniform_prob;
    m_current_bucket_1min.up_050 = uniform_prob;
    m_current_bucket_1min.up_060 = uniform_prob;
    m_current_bucket_1min.up_070 = uniform_prob;
    m_current_bucket_1min.up_080 = uniform_prob;
    m_current_bucket_1min.up_090 = uniform_prob;
    
    // Initialize all 20 DOWN buckets (indices 0-9)
    m_current_bucket_1min.dn_000 = uniform_prob;
    m_current_bucket_1min.dn_010 = uniform_prob;
    m_current_bucket_1min.dn_020 = uniform_prob;
    m_current_bucket_1min.dn_030 = uniform_prob;
    m_current_bucket_1min.dn_040 = uniform_prob;
    m_current_bucket_1min.dn_050 = uniform_prob;
    m_current_bucket_1min.dn_060 = uniform_prob;
    m_current_bucket_1min.dn_070 = uniform_prob;
    m_current_bucket_1min.dn_080 = uniform_prob;
    m_current_bucket_1min.dn_090 = uniform_prob;
    
    m_current_bucket_5min = m_current_bucket_1min;
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
    output.bucket_conf_1min = m_current_bucket_1min;
    output.bucket_conf_5min = m_current_bucket_5min;
    output.freq_ready = m_analyser.is_ready();
    output.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(tick.ts));
    
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
    // CHANGED: Using 20-bucket posterior extraction functions
    hefkf_common::BucketConfidence20 post_1min = hefkf_common::posterior_from_KF_20bucket(m_kf_1min, current_price, m_dt);
    hefkf_common::BucketConfidence20 post_5min = hefkf_common::posterior_from_5min_KF_20bucket(m_kf_5min, current_price, m_dt);
    
    // Use AnalyticScorer to enhance buckets based on market regime
    double price_velocity = m_kf_1min.get_state()(1);  // Get velocity from 1min filter
    
    // score_simple_20bucket() - CHANGED: Using 20-bucket scoring
    //   ├── Detect regime (Bull/Bear/Sideways)
    //   ├── Apply directional bias (redistribute probability)
    //   ├── Normalize the distribution
    //   └── Call sharpen_dirichlet_20bucket() to concentrate probabilities
    // Score and enhance 1min buckets
    auto scoring_result_1min = m_scorer.score_simple_20bucket(
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
                  // CHANGED: Sum all 10 UP buckets for total up probability
                  << ", raw_up=" << (post_1min.up_000 + post_1min.up_010 + 
                                    post_1min.up_020 + post_1min.up_030 +
                                    post_1min.up_040 + post_1min.up_050 +
                                    post_1min.up_060 + post_1min.up_070 +
                                    post_1min.up_080 + post_1min.up_090)
                  << ", enhanced_up=" << (scoring_result_1min.enhanced_buckets.up_000 + 
                                         scoring_result_1min.enhanced_buckets.up_010 +
                                         scoring_result_1min.enhanced_buckets.up_020 + 
                                         scoring_result_1min.enhanced_buckets.up_030 +
                                         scoring_result_1min.enhanced_buckets.up_040 + 
                                         scoring_result_1min.enhanced_buckets.up_050 +
                                         scoring_result_1min.enhanced_buckets.up_060 + 
                                         scoring_result_1min.enhanced_buckets.up_070 +
                                         scoring_result_1min.enhanced_buckets.up_080 + 
                                         scoring_result_1min.enhanced_buckets.up_090)
                  << std::endl;
        debug_count++;
    }
    
    // Score and enhance 5min buckets - CHANGED: Using 20-bucket scoring
    auto scoring_result_5min = m_scorer.score_simple_20bucket(
        freq_features,
        post_5min,
        price_velocity,
        false  // is_5min_filter
    );
    
    // Update current bucket confidences with enhanced versions
    m_current_bucket_1min = scoring_result_1min.enhanced_buckets;
    m_current_bucket_5min = scoring_result_5min.enhanced_buckets;
}

// ─────────────────────── ProcessingLoop Implementation ───────────────────────
ProcessingLoop::ProcessingLoop(size_t /* buffer_size */) : m_pipeline(1.0) {
    // buffer_size parameter is ignored since we're using fixed-size StaticRingBuffer
    // In a real implementation, you might make this configurable
}

void ProcessingLoop::add_tick(const KalmanTick& tick) {
    m_ring_buffer.push(tick);
}

bool ProcessingLoop::process_latest() {
    if (!m_running || m_ring_buffer.empty()) {
        return false;
    }
    
    try {
        const KalmanTick& latest_tick = m_ring_buffer.latest();
        
        // Initialize pipeline with first tick if needed
        if (!m_pipeline.is_initialized()) {
            m_pipeline.initialize(latest_tick);
            m_processed_count++;
            
            // Initialize timing tracking
            m_first_tick_time = std::chrono::steady_clock::now();
            m_last_tick_time = m_first_tick_time;
            return true;
        }
        
        // Process the latest tick through the complete pipeline
        m_latest_output = m_pipeline.process(latest_tick);
        m_processed_count++;
        
        // Calculate actual processing rate
        auto now = std::chrono::steady_clock::now();
        auto time_since_last = std::chrono::duration<double>(now - m_last_tick_time).count();
        auto total_elapsed = std::chrono::duration<double>(now - m_first_tick_time).count();
        
        // Instantaneous Hz (rate between last two ticks)
        double instant_hz = (time_since_last > 0) ? 1.0 / time_since_last : 0.0;
        
        // Average Hz (overall rate since start)
        double avg_hz = (total_elapsed > 0 && m_processed_count > 1) ? 
                        (m_processed_count - 1) / total_elapsed : 0.0;
        
        // Update timing
        m_last_tick_time = now;
        
        // Report Hz every 10 ticks or when debug mode is on
        if (m_debug_mode && (m_processed_count % 10 == 0)) {
            std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cout << "📊 [ProcessingLoop] Tick #" << m_processed_count << " processed\n";
            std::cout << "⏱️  Processing Rate: " << std::fixed << std::setprecision(3) 
                      << instant_hz << " Hz (instant), " 
                      << avg_hz << " Hz (average)\n";
            std::cout << "📈 Total elapsed: " << std::setprecision(2) << total_elapsed << " seconds\n";
            integration_utils::print_pipeline_output(m_latest_output);
            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        }
        
        // Emit warning if processing rate deviates significantly from expected
        if (m_processed_count > 10 && m_processed_count % 50 == 0) {
            const double expected_hz = 4.0;  // Could make this configurable
            const double tolerance = 0.1;    // 10% tolerance
            
            if (std::abs(avg_hz - expected_hz) / expected_hz > tolerance) {
                std::cerr << "⚠️  [ProcessingLoop] Rate deviation detected! "
                          << "Expected: " << expected_hz << " Hz, "
                          << "Actual: " << avg_hz << " Hz ("
                          << std::fixed << std::setprecision(1) 
                          << ((avg_hz / expected_hz - 1.0) * 100.0) << "% "
                          << (avg_hz > expected_hz ? "fast" : "slow") << ")\n";
            }
        }
        
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
    tick.ts = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    
    // Calculate bid/ask from mid and spread
    tick.bid = price - spread / 2.0;
    tick.ask = price + spread / 2.0;
    
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
    tick.ts = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    
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
    // CHANGED: Display all 20 buckets (10 UP + 10 DOWN)
    std::cout << "  UP: " << output.bucket_conf_1min.up_000 << " | "
              << output.bucket_conf_1min.up_010 << " | "
              << output.bucket_conf_1min.up_020 << " | "
              << output.bucket_conf_1min.up_030 << " | "
              << output.bucket_conf_1min.up_040 << " | "
              << output.bucket_conf_1min.up_050 << " | "
              << output.bucket_conf_1min.up_060 << " | "
              << output.bucket_conf_1min.up_070 << " | "
              << output.bucket_conf_1min.up_080 << " | "
              << output.bucket_conf_1min.up_090 << std::endl;
    std::cout << "  DN: " << output.bucket_conf_1min.dn_000 << " | "
              << output.bucket_conf_1min.dn_010 << " | "
              << output.bucket_conf_1min.dn_020 << " | "
              << output.bucket_conf_1min.dn_030 << " | "
              << output.bucket_conf_1min.dn_040 << " | "
              << output.bucket_conf_1min.dn_050 << " | "
              << output.bucket_conf_1min.dn_060 << " | "
              << output.bucket_conf_1min.dn_070 << " | "
              << output.bucket_conf_1min.dn_080 << " | "
              << output.bucket_conf_1min.dn_090 << std::endl;
    
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