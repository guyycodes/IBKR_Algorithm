#ifndef KALMAN_TICK_HPP
#define KALMAN_TICK_HPP

#include <chrono>
#include <cstdint>  // For uint64_t
#include <cmath>    // For std::isfinite

/**
 * KalmanTick
 * ----------
 * Simple POD (Plain Old Data) structure for storing tick data in Kalman ring buffers.
 * 
 * Design Principles:
 * - Trivially copyable for optimal memcpy performance
 * - Minimal memory footprint for cache efficiency
 * - Extensible for future Kalman filter state variables
 * 
 * Current Fields:
 * - ts: High-resolution timestamp for temporal ordering
 * - px: Price value for Kalman filtering
 * - volume: Trade volume
 * - spread: Bid-ask spread
 * - bid: Best bid price
 * - ask: Best ask price
 * - trade_count: Number of trades (usually 1 per tick)
 * 
 * Future Extensions (commented out for now):
 * - velocity: First derivative of price (dp/dt)
 * - acceleration: Second derivative of price (d²p/dt²)
 * - variance: Kalman filter uncertainty estimate
 * - prediction: Kalman predicted next value
 */
struct KalmanTick
{
    uint64_t                              ts{0};    // Unix timestamp in milliseconds
    double                                px{0.0}; // Price value for filtering (last price)
    double                                volume{0.0}; // Trade volume
    double                                spread{0.0}; // Bid-ask spread
    double                                bid{0.0};    // Best bid price
    double                                ask{0.0};    // Best ask price
    
    // will get these later
    // double                                vwap{0.0};          // Volume-weighted average price
    // double                                priceChange{0.0};   // Price change from previous period
    // double                                barRange{0.0};      // High-low range for bar data
    // double                                imbalance{0.0};     // Order book imbalance (bid vs ask size)
    // double                                momentum{0.0};      // Short-term price momentum
    
    // Validation method to check if tick data is valid
    bool is_valid() const {
        return ts > 0 && 
               px > 0.0 && 
               std::isfinite(px) &&
               volume >= 0.0 && 
               std::isfinite(volume) &&
               spread >= 0.0 && 
               std::isfinite(spread) &&
               bid > 0.0 && 
               std::isfinite(bid) &&
               ask > 0.0 && 
               std::isfinite(ask) &&
               ask >= bid;  // Spread should be non-negative
    }
};

// Compile-time verification that KalmanTick is POD for optimal performance
static_assert(std::is_trivially_copyable_v<KalmanTick>, 
              "KalmanTick must remain trivially copyable for memcpy optimization");

static_assert(std::is_standard_layout_v<KalmanTick>, 
              "KalmanTick must have standard layout for predictable memory access");

#endif // KALMAN_TICK_HPP 