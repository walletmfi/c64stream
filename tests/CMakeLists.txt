# C64U Plugin Tests
cmake_minimum_required(VERSION 3.28)

project(c64u-tests)

# Enable testing
enable_testing()

# Find required packages
find_package(Threads REQUIRED)

# VIC color unit tests
add_executable(test_vic_colors test_vic_colors.c)
add_test(NAME VICColors COMMAND test_vic_colors)

# C64U mock server (for manual testing)
add_executable(c64u_mock_server c64u_mock_server.c)
target_link_libraries(c64u_mock_server Threads::Threads m)

# Integration test (plugin + mock server)
add_executable(test_integration test_integration.c)
target_link_libraries(test_integration Threads::Threads)
add_test(NAME Integration COMMAND test_integration)

# Add compiler flags for tests
target_compile_options(test_vic_colors PRIVATE -Wall -Wextra -std=c17)
target_compile_options(c64u_mock_server PRIVATE -Wall -Wextra -std=c17)
target_compile_options(test_integration PRIVATE -Wall -Wextra -std=c17)

# Install test binaries to build directory
install(TARGETS test_vic_colors c64u_mock_server test_integration RUNTIME DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
