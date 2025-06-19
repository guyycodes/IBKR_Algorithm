#!/bin/bash

# Build script for self-contained HEFKF integration test
# No external dependencies outside of HEFKF directory

echo "🔨 Building HEFKF Integration Test..."
echo "=================================="

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    mkdir build
fi

cd build

# Configure with CMake
echo "📋 Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the integration test
echo "🔧 Compiling test_integration_pipeline..."
make test_integration_pipeline -j$(nproc)

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Build successful!"
    echo ""
    echo "📊 To run the test:"
    echo "   ./test_integration_pipeline [csv_path] [feed_hz] [duration_sec] [debug]"
    echo ""
    echo "Examples:"
    echo "   ./test_integration_pipeline                                        # Use defaults"
    echo "   ./test_integration_pipeline test_csv/test_data_downtrend.csv     # Custom CSV"
    echo "   ./test_integration_pipeline test_csv/test_data_downtrend.csv 4.0 # 4Hz feed rate"
    echo "   ./test_integration_pipeline test_csv/test_data_downtrend.csv 2.0 60 debug # Debug mode"
else
    echo ""
    echo "❌ Build failed!"
    exit 1
fi 