# # Cross-Architecture Network Bridge: x86 Container ↔ ARM MacBook
# cmake_minimum_required(VERSION 3.16)

# # Create the network bridge library
# add_library(network_bridge STATIC
#     network_bridge.cpp
#     network_bridge.hpp
# )

# # Link required libraries
# target_link_libraries(network_bridge PRIVATE 
#     pthread  # For std::thread
#     # nlohmann_json (will need to be installed)
# )

# # Include directories
# target_include_directories(network_bridge PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/..  # Access to ibkr-trader root
#     ${CMAKE_CURRENT_SOURCE_DIR}     # Access to bridge folder
# )

# # C++20 for modern features
# target_compile_features(network_bridge PRIVATE cxx_std_20)

# # Compiler optimizations for performance
# target_compile_options(network_bridge PRIVATE
#     -O3              # Maximum optimization
#     -march=native    # CPU-specific optimizations
#     -DNDEBUG         # Remove debug checks
# )

# # Optional: Create example executable
# add_executable(bridge_example EXCLUDE_FROM_ALL
#     example_usage.cpp
# )

# target_link_libraries(bridge_example PRIVATE
#     network_bridge
# )

# message(STATUS "🌉 Cross-Architecture Network Bridge configured")
# message(STATUS "📡 x86 Container → ARM MacBook communication")
# message(STATUS "🔄 Build with: make network_bridge")
# message(STATUS "🐍 Python client: python_client.py") 