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

# Test executable for types
add_executable(test_types
    test_types.cpp
)

# Header-only, just need include path
target_include_directories(test_types PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Set output directory
set_target_properties(test_types PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for semantic analysis
add_executable(test_sema
    test_sema.cpp
)

# Link against sema library
target_link_libraries(test_sema PRIVATE zerosema)

# Set output directory
set_target_properties(test_sema PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for IR
add_executable(test_ir
    test_ir.cpp
)

# Link against IR library
target_link_libraries(test_ir PRIVATE zeroir)

# Set output directory
set_target_properties(test_ir PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Test executable for backend
add_executable(test_backend
    test_backend.cpp
)

# Link against backend library
target_link_libraries(test_backend PRIVATE zerobackend)

# Set output directory
set_target_properties(test_backend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# IR source-span tests (spec 002).
add_executable(test_ir_spans
    test_ir_spans.cpp
)
target_link_libraries(test_ir_spans PRIVATE zeroir zeroparse)
set_target_properties(test_ir_spans PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Runtime-bridge smoke test (spec 001).
# This test #includes <zero/zero.hpp> from the external/core-runtime
# submodule and exercises one runtime op directly — the first end-to-end
# proof that the compiler tree can build against frozen runtime v1.4.0.
add_executable(test_runtime_bridge
    test_runtime_bridge.cpp
)
target_link_libraries(test_runtime_bridge PRIVATE zero-core)
set_target_properties(test_runtime_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ─────────────────────────────────────────────────────────────────────
# ctest registration (spec 001 side-cleanup).
# Test binaries were built but never registered with ctest before.
# ─────────────────────────────────────────────────────────────────────
add_test(NAME ZeroRuntimeTest      COMMAND test_runtime)
add_test(NAME ZeroErrorsTest       COMMAND test_errors)
add_test(NAME ZeroPrintTest        COMMAND test_print_enhanced)
add_test(NAME ZeroSourceTest       COMMAND test_source)
add_test(NAME ZeroLexerTest        COMMAND test_lexer)
add_test(NAME ZeroParserTest       COMMAND test_parser)
add_test(NAME ZeroTypesTest        COMMAND test_types)
add_test(NAME ZeroSemaTest         COMMAND test_sema)
add_test(NAME ZeroIRTest           COMMAND test_ir)
add_test(NAME ZeroBackendTest      COMMAND test_backend)
add_test(NAME ZeroIRSpansTest      COMMAND test_ir_spans)
add_test(NAME ZeroRuntimeBridgeTest COMMAND test_runtime_bridge)
