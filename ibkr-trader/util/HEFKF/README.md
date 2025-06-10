# Hybrid Exponential Forgetting Kalman Filter (HEFKF)

A C++ implementation of a Hybrid Exponential Forgetting Kalman Filter optimized for high-frequency stock trading applications. This filter combines the benefits of exponential forgetting (finite memory) with bucket-based control inputs and frequency-domain signal processing.

## Features

### Core Functionality
- **Exponential Forgetting**: Implements the λ-KF algorithm with configurable forgetting factor
- **Adaptive Lambda**: Automatically adjusts forgetting factor based on market volatility
- **Bucket Control**: Incorporates directional confidence signals from trading buckets
- **Frequency Domain Processing**: Coherence-based signal quality assessment
- **Numerical Stability**: Joseph form stabilization and positive definite covariance enforcement

### Key Capabilities
- **Finite Memory**: Effective memory window of ~1/(1-λ) ticks
- **Real-time Processing**: Optimized for sub-millisecond latency
- **Multi-modal Integration**: Price, volume, and spread filtering
- **Adaptive Noise Scaling**: Market-condition-aware noise parameters

## Mathematical Foundation

The filter implements the core λ-KF prediction equation:

```
P_{k|k-1} = (1/λ) * F * P_{k-1|k-1} * F^T + Q
```

Where:
- `λ ∈ (0,1]` is the forgetting factor
- `1/λ > 1` inflates the prediction covariance
- Smaller λ → more aggressive forgetting

### Memory Characteristics
- λ = 0.99 → ~100-tick memory
- λ = 0.95 → ~20-tick memory  
- λ = 0.995 → ~200-tick memory

## Usage

### Basic Example

```cpp
#include "hybrid_exp_forgetting_kalman_filter.hpp"
using namespace hefkf;

// Configure the filter
HKFConfig config;
config.lambda_fixed = 0.99;        // 100-tick memory
config.lambda_adaptive = true;     // Enable adaptive lambda
config.bucket_weight = 0.5;        // Moderate bucket influence

// Create and initialize filter
HybridExpForgettingKalmanFilter filter(config);

MarketData initial_data;
initial_data.price = 100.0;
initial_data.volume = 1000.0;
initial_data.spread = 0.01;
initial_data.timestamp = std::chrono::system_clock::now();

filter.initialize(initial_data, 1.0);  // 1-second timestep

// Process new data
MarketData new_data;
new_data.price = 100.05;
new_data.volume = 1200.0;
new_data.spread = 0.009;
new_data.timestamp = std::chrono::system_clock::now();

auto output = filter.process(new_data);

std::cout << "Smoothed Price: " << output.price_smoothed << std::endl;
std::cout << "Price Velocity: " << output.price_velocity << std::endl;
std::cout << "Lambda Used: " << output.lambda_used << std::endl;
```

### With Bucket Confidence

```cpp
// Add bucket confidence data
new_data.bucket_conf = std::make_unique<BucketConfidence>();
new_data.bucket_conf->up_001_002 = 0.4;  // 40% confidence in 0.1-0.2% up move
new_data.bucket_conf->up_002_005 = 0.3;  // 30% confidence in 0.2-0.5% up move
new_data.bucket_conf->dn_001_002 = 0.2;  // 20% confidence in 0.1-0.2% down move
new_data.bucket_conf->dn_002_005 = 0.1;  // 10% confidence in 0.2-0.5% down move

auto output = filter.process(new_data);
```

### Configuration Options

```cpp
struct HKFConfig {
    // Existing hybrid filter parameters
    double time_domain_weight = 0.7;        // KF vs. raw observation weight
    double frequency_domain_weight = 0.3;    // Frequency nudging strength
    bool adaptive_noise = true;              // Enable adaptive noise scaling
    double bucket_weight = 0.5;              // Bucket influence weight
    
    // Exponential forgetting parameters
    double lambda_fixed = 0.99;              // Fixed forgetting factor
    bool lambda_adaptive = true;             // Enable adaptive lambda
    double lambda_min = 0.95;                // Minimum lambda (fast adaptation)
    double lambda_max = 0.995;               // Maximum lambda (slow adaptation)
    double vol_threshold = 0.002;            // Volatility threshold (0.2%)
};
```

## State Vector

The filter maintains a 4-dimensional state:

```
x = [price, velocity, volume, spread]^T
```

With corresponding observations:

```
z = [price, volume, spread]^T
```

## Building

### Prerequisites
- CMake 3.12+
- C++17 compatible compiler
- Eigen3 library

### Build Instructions

```bash
mkdir build && cd build
cmake ..
make

# Run example
./hefkf_example
```

### Integration with Existing Projects

```cmake
# In your CMakeLists.txt
find_package(Eigen3 REQUIRED)
add_subdirectory(path/to/HEFKF)

target_link_libraries(your_target hefkf)
```

## Performance Characteristics

### Computational Complexity
- **Per update**: O(n³) where n = 4 (state dimension)
- **Memory**: O(n²) for covariance storage
- **Typical latency**: <1ms for single update

### Memory Usage
- **Base footprint**: ~2KB per filter instance
- **No historical data storage** required
- **Constant memory** regardless of data history

## Theoretical Properties

### Convergence
- Converges to steady-state covariance under mild conditions
- Tracks time-varying parameters better than standard Kalman filter
- Maintains numerical stability for λ > 0.95

### Adaptive Behavior
- **High volatility** → Lower λ → Faster adaptation
- **Low volatility** → Higher λ → Smoother tracking
- **Bucket signals** → Directional bias in predictions

## Integration with Trading Systems

### Real-time Processing
```cpp
// Typical trading loop integration
while (market_open) {
    MarketData tick = get_next_tick();
    
    // Add bucket confidence if available
    if (has_bucket_signal()) {
        tick.bucket_conf = get_bucket_confidence();
    }
    
    // Process through filter
    auto filtered = filter.process(tick);
    
    // Use filtered values for trading decisions
    make_trading_decision(filtered);
}
```

### Risk Management
```cpp
// Monitor filter uncertainty for position sizing
double uncertainty = filter.get_covariance().trace();
double position_scale = std::exp(-uncertainty / risk_tolerance);
```

## Troubleshooting

### Common Issues

1. **Numerical Instability**
   - Ensure λ > 0.95 for stability
   - Enable Joseph stabilization
   - Check for reasonable input ranges

2. **Poor Tracking**
   - Reduce λ for faster adaptation
   - Increase bucket_weight for stronger signals
   - Verify bucket confidence normalization

3. **High Latency**
   - Use optimized BLAS libraries
   - Consider state dimension reduction
   - Profile matrix operations

### Debug Information
```cpp
// Access internal state for debugging
auto state = filter.get_state();
auto covariance = filter.get_covariance();
bool initialized = filter.is_initialized();
```

## License

[Your License Here]

## References

1. Exponential Forgetting Kalman Filters for Time-Varying Parameter Estimation
2. Hybrid Signal Processing for High-Frequency Trading
3. Bucket-Based Directional Confidence in Financial Markets 