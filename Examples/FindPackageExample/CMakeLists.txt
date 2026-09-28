cmake_minimum_required(VERSION 3.16)
project(LoggingFindPackageExample)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Test find_package with installed Logging library
find_package(Logging REQUIRED)

# Example executable using installed Logging
add_executable(example_app main.cpp)
target_link_libraries(example_app PRIVATE Logging::Logging)

# Enable testing
enable_testing()
add_test(NAME example_app COMMAND example_app)
