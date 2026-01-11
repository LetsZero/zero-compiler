# Lexer Library
add_library(zerolex STATIC
    lexer.cpp
)

target_include_directories(zerolex PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to source management library
target_link_libraries(zerolex PUBLIC zerosrc)

# Set output directory
set_target_properties(zerolex PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
