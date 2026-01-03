# Zero Compiler - Main Build Configuration
cmake_minimum_required(VERSION 3.15)
project(Zero VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LLVM is temporarily disabled due to incomplete installation
# Uncomment when LLVM is properly installed
# find_package(LLVM CONFIG)
# if(LLVM_FOUND)
#     message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
#     include_directories(${LLVM_INCLUDE_DIRS})
#     separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
#     add_definitions(${LLVM_DEFINITIONS_LIST})
#     set(HAVE_LLVM TRUE)
# else()
#     set(HAVE_LLVM FALSE)
# endif()

message(STATUS "")
message(STATUS "==============================================")
message(STATUS "Zero Compiler - Runtime Library Build")
message(STATUS "==============================================")
message(STATUS "LLVM support: DISABLED (enable when LLVM is fixed)")
message(STATUS "Building: Runtime library only")
message(STATUS "")

# Add project include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/runtime
)

# Add subdirectories
add_subdirectory(runtime)
add_subdirectory(tests)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Print configuration summary
message(STATUS "")
message(STATUS "Build Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "")

