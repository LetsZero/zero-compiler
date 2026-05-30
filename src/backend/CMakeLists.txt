# Backend Library
add_library(zerobackend STATIC
    interpreter.cpp
)

target_include_directories(zerobackend PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to IR library; also pick up the frozen runtime via the submodule's
# zero-core interface library (spec 003 — RuntimeValue now holds zero::Tensor).
# Spec 010: zerodiag (Reporter) + zerosrc (SourceManager) for rich runtime
# diagnostics. zeroir pulls zerosrc transitively; listed explicitly here.
target_link_libraries(zerobackend PUBLIC zeroir zero-core zerodiag zerosrc)

# Set output directory
set_target_properties(zerobackend PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
