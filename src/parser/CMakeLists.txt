# Parser Library
add_library(zeroparse STATIC
    parser.cpp
)

target_include_directories(zeroparse PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to lexer library (which links zerosrc)
target_link_libraries(zeroparse PUBLIC zerolex)

# Set output directory
set_target_properties(zeroparse PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
