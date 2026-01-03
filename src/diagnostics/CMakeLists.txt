# Diagnostics Library Build Configuration
cmake_minimum_required(VERSION 3.15)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the diagnostics library
add_library(zerodiag STATIC
    reporter.cpp
)

# Set output directory
set_target_properties(zerodiag PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Include directories
target_include_directories(zerodiag PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Compiler flags for optimization
target_compile_options(zerodiag PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)
