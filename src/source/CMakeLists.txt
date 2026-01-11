# Source Management Library
add_library(zerosrc STATIC
    source.cpp
)

target_include_directories(zerosrc PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Set output directory
set_target_properties(zerosrc PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
