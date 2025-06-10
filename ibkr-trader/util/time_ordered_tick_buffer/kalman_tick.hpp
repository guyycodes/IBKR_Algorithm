#ifndef KALMAN_TICK_HPP
#define KALMAN_TICK_HPP

#include <chrono>

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
 * 
 * Future Extensions (commented out for now):
 * - velocity: First derivative of price (dp/dt)
 * - acceleration: Second derivative of price (d²p/dt²)
 * - variance: Kalman filter uncertainty estimate
 * - prediction: Kalman predicted next value
 */
struct KalmanTick
{
    std::chrono::steady_clock::time_point ts{};   // Timestamp for temporal ordering
    double                                px{0.0}; // Price value for filtering
    
    // Future Kalman state extensions:
    // double velocity{0.0};      // dp/dt - price velocity
    // double acceleration{0.0};  // d²p/dt² - price acceleration  
    // double variance{0.0};      // Kalman uncertainty estimate
    // double prediction{0.0};    // Kalman predicted next value
};

// Compile-time verification that KalmanTick is POD for optimal performance
static_assert(std::is_trivially_copyable_v<KalmanTick>, 
              "KalmanTick must remain trivially copyable for memcpy optimization");

static_assert(std::is_standard_layout_v<KalmanTick>, 
              "KalmanTick must have standard layout for predictable memory access");

#endif // KALMAN_TICK_HPP 