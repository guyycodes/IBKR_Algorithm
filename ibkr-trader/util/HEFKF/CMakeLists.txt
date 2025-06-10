cmake_minimum_required(VERSION 3.12)
project(HybridExpForgettingKalmanFilter)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Eigen3 REQUIRED)

# Include directories
include_directories(${EIGEN3_INCLUDE_DIR})

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")

# Build library
add_library(hefkf STATIC
    hybrid_exp_forgetting_kalman_filter.cpp
)

target_link_libraries(hefkf ${EIGEN3_LIBRARIES})

# Build example
add_executable(hefkf_example
    hefkf_example.cpp
)

target_link_libraries(hefkf_example hefkf)

# Build smoke tests
add_executable(smoke_tests
    smoke_tests.cpp
)

target_link_libraries(smoke_tests hefkf)

# Install targets
install(TARGETS hefkf DESTINATION lib)
install(FILES hybrid_exp_forgetting_kalman_filter.hpp DESTINATION include)

# Optional: Build tests if we have them
if(BUILD_TESTS)
    enable_testing()
    # Add test targets here
endif() 