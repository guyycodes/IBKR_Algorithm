#!/bin/bash

# Build and Test Script for HEFKF Pipeline
# Compiles all targets and runs validation tests

set -e  # Exit on any error

echo "============================================================="
echo "HEFKF Pipeline Build and Test Script"
echo "============================================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check dependencies
print_status "Checking dependencies..."

# Check for required tools
if ! command -v cmake &> /dev/null; then
    print_error "CMake not found. Please install cmake."
    exit 1
fi

if ! command -v make &> /dev/null; then
    print_error "Make not found. Please install build-essential."
    exit 1
fi

# Check for FFTW3
if ! ldconfig -p | grep -q libfftw3; then
    print_warning "FFTW3 may not be installed. If build fails, install with:"
    print_warning "  sudo apt-get install libfftw3-dev"
fi

print_success "Dependencies check completed"

# Create build directory
print_status "Setting up build directory..."
rm -rf build
mkdir -p build
cd build

# Configure with CMake
print_status "Configuring build with CMake..."
if cmake ..; then
    print_success "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

# Build the project
print_status "Building HEFKF library and tests..."
if make -j$(nproc); then
    print_success "Build completed successfully"
else
    print_error "Build failed"
    exit 1
fi

# List built targets
print_status "Built targets:"
ls -la hefkf smoke_tests standalone_example unit_tests 2>/dev/null || true

echo ""
echo "============================================================="
echo "RUNNING TESTS"
echo "============================================================="

# Run smoke tests
print_status "Running smoke tests..."
if ./smoke_tests; then
    print_success "Smoke tests passed"
else
    print_error "Smoke tests failed"
    exit 1
fi

echo ""

# Run unit tests
print_status "Running comprehensive unit tests..."
if ./unit_tests; then
    print_success "Unit tests passed"
else
    print_error "Unit tests failed"
    exit 1
fi

echo ""

# Run standalone example (if it exists and is executable)
if [ -x "./standalone_example" ]; then
    print_status "Running standalone example..."
    if ./standalone_example; then
        print_success "Standalone example completed"
    else
        print_warning "Standalone example had issues (may be normal)"
    fi
else
    print_warning "Standalone example not found or not executable"
fi

echo ""
echo "============================================================="
echo "PERFORMANCE BENCHMARKS"
echo "============================================================="

# Basic performance test
print_status "Running basic performance benchmark..."

# Create a simple benchmark script
cat > benchmark_test.cpp << 'EOF'
#include "../frequency_analyser.hpp"
#include "../integration_loop.hpp"
#include <chrono>
#include <iostream>
#include <random>

int main() {
    const int N_SAMPLES = 1000;
    const int N_ITERATIONS = 100;
    
    std::mt19937 rng(42);
    std::normal_distribution<double> dist(100.0, 1.0);
    std::uniform_real_distribution<double> vol_dist(1000.0, 5000.0);
    
    // Benchmark FrequencyAnalyser
    FrequencyAnalyser analyser(1.0);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int iter = 0; iter < N_ITERATIONS; ++iter) {
        for (int i = 0; i < 256; ++i) {  // Fill buffer
            analyser.push(dist(rng), vol_dist(rng), 0.01);
        }
        
        hefkf_common::FrequencyFeatures features;
        analyser.compute(features);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_time_us = static_cast<double>(duration.count()) / N_ITERATIONS;
    
    std::cout << "FrequencyAnalyser performance:" << std::endl;
    std::cout << "  Average computation time: " << avg_time_us << " μs" << std::endl;
    std::cout << "  Target: < 50 μs" << std::endl;
    
    if (avg_time_us < 50.0) {
        std::cout << "  ✓ Performance target met!" << std::endl;
        return 0;
    } else {
        std::cout << "  ⚠ Performance target not met" << std::endl;
        return 1;
    }
}
EOF

# Compile and run benchmark
if g++ -std=c++17 -O3 -I.. benchmark_test.cpp ../frequency_analyser.cpp -lfftw3 -lfftw3_threads -o benchmark_test; then
    if ./benchmark_test; then
        print_success "Performance benchmark passed"
    else
        print_warning "Performance benchmark did not meet targets"
    fi
else
    print_warning "Could not compile performance benchmark"
fi

# Clean up
rm -f benchmark_test.cpp benchmark_test

echo ""
echo "============================================================="
echo "SUMMARY"
echo "============================================================="

print_success "All tests completed successfully!"
echo ""
echo "Built targets are available in the build directory:"
echo "  - hefkf (static library)"
echo "  - smoke_tests (basic functionality tests)"
echo "  - unit_tests (comprehensive pipeline validation)"
echo "  - standalone_example (usage example)"
echo ""
echo "To run tests again:"
echo "  cd build && ./unit_tests"
echo ""
echo "To use the library in your project:"
echo "  #include \"frequency_analyser.hpp\""
echo "  #include \"posterior.hpp\""
echo "  #include \"integration_loop.hpp\""

exit 0 