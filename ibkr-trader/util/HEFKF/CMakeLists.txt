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
    integration_loop.cpp
)

target_link_libraries(hefkf ${FFTW3_LIB} ${FFTW3_THREADS_LIB})

# Build smoke tests
add_executable(smoke_tests
    smoke_tests.cpp
)

target_link_libraries(smoke_tests hefkf)

# Build standalone example
add_executable(standalone_example
    standalone_example.cpp
)

target_link_libraries(standalone_example hefkf)

# Build unit tests for the pipeline
add_executable(unit_tests
    unit_tests.cpp
)

target_link_libraries(unit_tests hefkf)

# Install targets
install(TARGETS hefkf smoke_tests standalone_example unit_tests DESTINATION bin)
install(FILES 
    1min_HEFKF.hpp 
    5min_HEFKF.hpp 
    frequency_analyser.hpp
    posterior.hpp
    integration_loop.hpp
    DESTINATION include)

# Optional: Build tests if we have them
if(BUILD_TESTS)
    enable_testing()
    # Add test targets here
endif() 