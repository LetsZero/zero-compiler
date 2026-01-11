# Test executable for the Zero runtime library
add_executable(test_runtime
    test_runtime.cpp
)

# Link against the runtime library
target_link_libraries(test_runtime PRIVATE zerort)

# Set output directory
set_target_properties(test_runtime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for error reporter
add_executable(test_errors
    test_errors.cpp
)

# Link against the diagnostics library
target_link_libraries(test_errors PRIVATE zerodiag)

# Set output directory
set_target_properties(test_errors PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for enhanced print functions
add_executable(test_print_enhanced
    test_print_enhanced.cpp
)

# Link against the runtime library
target_link_libraries(test_print_enhanced PRIVATE zerort)

# Set output directory
set_target_properties(test_print_enhanced PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for source management
add_executable(test_source
    test_source.cpp
)

# Link against the source library
target_link_libraries(test_source PRIVATE zerosrc)

# Set output directory
set_target_properties(test_source PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for lexer
add_executable(test_lexer
    test_lexer.cpp
)

# Link against the lexer library (which links zerosrc)
target_link_libraries(test_lexer PRIVATE zerolex)

# Set output directory
set_target_properties(test_lexer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for parser
add_executable(test_parser
    test_parser.cpp
)

# Link against the parser library
target_link_libraries(test_parser PRIVATE zeroparse)

# Set output directory
set_target_properties(test_parser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
