# Semantic Analysis Library
add_library(zerosema STATIC
    sema.cpp
)

target_include_directories(zerosema PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to parser library (for AST types)
target_link_libraries(zerosema PUBLIC zeroparse)

# Set output directory
set_target_properties(zerosema PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
