cmake_minimum_required(VERSION 3.12)
project(HybridExpForgettingKalmanFilter)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use local Eigen3 (header-only library)
set(EIGEN3_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/eigen-3.4.0")

# Verify Eigen3 exists
if(EXISTS "${EIGEN3_INCLUDE_DIR}/Eigen/Core")
    message(STATUS "Found local Eigen3 at: ${EIGEN3_INCLUDE_DIR}")
    set(Eigen3_FOUND TRUE)
else()
    message(FATAL_ERROR "Eigen3 not found at ${EIGEN3_INCLUDE_DIR}. Please check the path!")
endif()

# Include directories
include_directories(${EIGEN3_INCLUDE_DIR})

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")

# Find FFTW3 for signal processing (Welch PSD & coherence)
find_library(FFTW3_LIB fftw3)
find_library(FFTW3_THREADS_LIB fftw3_threads)

if(NOT FFTW3_LIB OR NOT FFTW3_THREADS_LIB)
    message(FATAL_ERROR "FFTW3 libraries not found! Please install libfftw3-dev")
endif()

message(STATUS "Found FFTW3: ${FFTW3_LIB}")
message(STATUS "Found FFTW3 threads: ${FFTW3_THREADS_LIB}")

# Add FFTW3 definitions
add_definitions(-DUSE_FFTW3)

# Build library
add_library(hefkf STATIC
    1min_HEFKF.cpp
    5min_HEFKF.cpp
    frequency_analyser.cpp
    posterior.cpp
    # integration_loop.cpp
    analytic_scorer.cpp
    # bucket_convergence.cpp
)

target_link_libraries(hefkf ${FFTW3_LIB} ${FFTW3_THREADS_LIB})

# Build smoke tests
# add_executable(smoke_tests
#     smoke_tests.cpp
# )

# target_link_libraries(smoke_tests hefkf)

# Build test data generator
# add_executable(test_data_generator
#     test_data_generator.cpp
# )

# target_link_libraries(test_data_generator)

# Build enhanced unit tests
# add_executable(enhanced_unit_tests
#     enhanced_unit_tests.cpp
# )

# target_link_libraries(enhanced_unit_tests hefkf)

# Build 20-bucket integration test
add_executable(test_20bucket_integration
    test_20bucket_integration.cpp
)

target_link_libraries(test_20bucket_integration hefkf)

# Build comprehensive test suite
add_executable(test_comprehensive
    test_comprehensive.cpp
)

target_link_libraries(test_comprehensive hefkf)

# Install targets
install(TARGETS hefkf test_20bucket_integration test_comprehensive DESTINATION bin)
install(FILES 
    1min_HEFKF.hpp 
    5min_HEFKF.hpp 
    frequency_analyser.hpp
    posterior.hpp
    integration_loop.hpp
    analytic_scorer.hpp
    bucket_convergence.hpp
    DESTINATION include)

# Optional: Build tests if we have them
if(BUILD_TESTS)
    enable_testing()
    # Add test targets here
endif() 