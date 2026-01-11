# IR Library
add_library(zeroir STATIC
    ir.cpp
    lowering.cpp
)

target_include_directories(zeroir PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to parser and sema libraries
target_link_libraries(zeroir PUBLIC zeroparse)

# Set output directory
set_target_properties(zeroir PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
