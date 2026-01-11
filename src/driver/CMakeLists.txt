# CLI Driver
add_executable(zeroc
    main.cpp
)

target_include_directories(zeroc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link against all required libraries
target_link_libraries(zeroc PRIVATE 
    zerobackend
    zeroir
    zerosema
    zeroparse
    zerolex
    zerosrc
)

# Set output directory
set_target_properties(zeroc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
