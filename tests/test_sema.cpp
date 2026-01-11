/**
 * @file test_sema.cpp
 * @brief Unit tests for Zero Semantic Analysis
 */

#include "sema/sema.hpp"
#include "parser/parser.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>

using namespace zero::sema;
using namespace zero::parser;
using namespace zero::source;

// ─────────────────────────────────────────────────────────────────────────────
// Test utilities
// ─────────────────────────────────────────────────────────────────────────────

#define TEST(name) void name(); \
    static struct name##_register { \
        name##_register() { tests.push_back({#name, name}); } \
    } name##_instance; \
    void name()

struct TestCase {
    const char* name;
    void (*func)();
};

static std::vector<TestCase> tests;

static int run_all_tests() {
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : tests) {
        std::cout << "  Running " << test.name << "... ";
        try {
            test.func();
            std::cout << "\033[32mPASS\033[0m\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "\033[31mFAIL\033[0m: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "\033[31mFAIL\033[0m: unknown exception\n";
            ++failed;
        }
    }
    
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

// Helper to parse and analyze
static std::pair<bool, std::vector<SemanticError>> analyze_code(const std::string& code) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", code);
    Parser parser(sm, id);
    auto program = parser.parse();
    
    Sema sema;
    sema.analyze(program);
    return {sema.had_error(), sema.errors()};
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(test_valid_program) {
    auto [had_error, errors] = analyze_code("fn main() { return 0; }");
    assert(!had_error);
}

TEST(test_undefined_variable) {
    auto [had_error, errors] = analyze_code("fn main() { return x; }");
    assert(had_error);
    assert(errors.size() >= 1);
    assert(errors[0].kind == ErrorKind::UNDEFINED_VARIABLE);
}

TEST(test_defined_variable) {
    auto [had_error, errors] = analyze_code("fn main() { let x = 10; return x; }");
    assert(!had_error);
}

TEST(test_undefined_function) {
    auto [had_error, errors] = analyze_code("fn main() { foo(); }");
    assert(had_error);
    assert(errors[0].kind == ErrorKind::UNDEFINED_FUNCTION);
}

TEST(test_defined_function) {
    auto [had_error, errors] = analyze_code(
        "fn foo() { }\n"
        "fn main() { foo(); }"
    );
    assert(!had_error);
}

TEST(test_wrong_arg_count) {
    auto [had_error, errors] = analyze_code(
        "fn foo(a, b) { }\n"
        "fn main() { foo(1); }"
    );
    assert(had_error);
    assert(errors[0].kind == ErrorKind::WRONG_ARG_COUNT);
}

TEST(test_correct_arg_count) {
    auto [had_error, errors] = analyze_code(
        "fn foo(a, b) { }\n"
        "fn main() { foo(1, 2); }"
    );
    assert(!had_error);
}

TEST(test_duplicate_variable) {
    auto [had_error, errors] = analyze_code(
        "fn main() { let x = 1; let x = 2; }"
    );
    assert(had_error);
    assert(errors[0].kind == ErrorKind::DUPLICATE_DEFINITION);
}

TEST(test_duplicate_function) {
    auto [had_error, errors] = analyze_code(
        "fn foo() { }\n"
        "fn foo() { }"
    );
    assert(had_error);
    assert(errors[0].kind == ErrorKind::DUPLICATE_DEFINITION);
}

TEST(test_scoped_variable) {
    // Variable in if block should not be visible outside
    auto [had_error, errors] = analyze_code(
        "fn main() {\n"
        "  if 1 { let x = 10; }\n"
        "  return x;\n"
        "}"
    );
    assert(had_error);
    assert(errors[0].kind == ErrorKind::UNDEFINED_VARIABLE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  Zero Semantic Analysis Tests\n";
    std::cout << "============================================\n\n";
    
    return run_all_tests();
}
