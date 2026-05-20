# Backend Library
add_library(zerobackend STATIC
    interpreter.cpp
)

target_include_directories(zerobackend PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to IR library; also pick up the frozen runtime via the submodule's
# zero-core interface library (spec 003 — RuntimeValue now holds zero::Tensor).
target_link_libraries(zerobackend PUBLIC zeroir zero-core)

# Set output directory
set_target_properties(zerobackend PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
