# Zero Compiler - Main Build Configuration
cmake_minimum_required(VERSION 3.15)
project(Zero VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

message(STATUS "")
message(STATUS "==============================================")
message(STATUS "Zero Compiler Build")
message(STATUS "==============================================")

message(STATUS "Building: Runtime + Diagnostics libraries")
message(STATUS "")

# Add project include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/runtime
)

# Add subdirectories
add_subdirectory(runtime)
add_subdirectory(src/source)
add_subdirectory(src/lexer)
add_subdirectory(src/parser)
add_subdirectory(src/sema)
add_subdirectory(src/diagnostics)
add_subdirectory(tests)

# Add core-runtime submodule
if(EXISTS "${CMAKE_SOURCE_DIR}/external/core-runtime/CMakeLists.txt")
    message(STATUS "Found core-runtime submodule")
    add_subdirectory(external/core-runtime)
else()
    message(WARNING "core-runtime submodule not found. Run: git submodule update --init --recursive")
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Build Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
