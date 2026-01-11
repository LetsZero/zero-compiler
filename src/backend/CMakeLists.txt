# Backend Library
add_library(zerobackend STATIC
    interpreter.cpp
)

target_include_directories(zerobackend PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to IR library
target_link_libraries(zerobackend PUBLIC zeroir)

# Set output directory
set_target_properties(zerobackend PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
