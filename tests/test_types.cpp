/**
 * @file test_types.cpp
 * @brief Unit tests for Zero Type System
 */

#include "types/types.hpp"

#include <iostream>
#include <vector>
#include <cassert>

using namespace zero::types;

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

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(test_type_factories) {
    Type t_int = Type::make_int();
    Type t_float = Type::make_float();
    Type t_void = Type::make_void();
    Type t_tensor = Type::make_tensor();
    Type t_unknown = Type::make_unknown();
    
    assert(t_int.kind == TypeKind::INT);
    assert(t_float.kind == TypeKind::FLOAT);
    assert(t_void.kind == TypeKind::VOID);
    assert(t_tensor.kind == TypeKind::TENSOR);
    assert(t_unknown.kind == TypeKind::UNKNOWN);
}

TEST(test_type_queries) {
    Type t_int = Type::make_int();
    Type t_float = Type::make_float();
    Type t_void = Type::make_void();
    
    assert(t_int.is_int());
    assert(!t_int.is_float());
    assert(t_int.is_numeric());
    
    assert(t_float.is_float());
    assert(t_float.is_numeric());
    
    assert(t_void.is_void());
    assert(!t_void.is_numeric());
}

TEST(test_type_equality) {
    Type a = Type::make_int();
    Type b = Type::make_int();
    Type c = Type::make_float();
    
    assert(a == b);
    assert(a != c);
}

TEST(test_type_names) {
    assert(std::string(Type::make_int().name()) == "int");
    assert(std::string(Type::make_float().name()) == "float");
    assert(std::string(Type::make_void().name()) == "void");
    assert(std::string(Type::make_tensor().name()) == "tensor");
}

TEST(test_types_compatible) {
    Type t_int = Type::make_int();
    Type t_float = Type::make_float();
    Type t_unknown = Type::make_unknown();
    
    assert(types_compatible(t_int, t_int));
    assert(!types_compatible(t_int, t_float));
    assert(types_compatible(t_int, t_unknown));  // Unknown is compatible
    assert(types_compatible(t_unknown, t_float));
}

TEST(test_binary_result_type) {
    Type t_int = Type::make_int();
    Type t_float = Type::make_float();
    
    // Same types
    assert(binary_result_type(t_int, t_int) == t_int);
    assert(binary_result_type(t_float, t_float) == t_float);
    
    // Promotion
    Type result = binary_result_type(t_int, t_float);
    assert(result.is_float());
}

TEST(test_parse_type) {
    assert(parse_type("int") == Type::make_int());
    assert(parse_type("float") == Type::make_float());
    assert(parse_type("void") == Type::make_void());
    assert(parse_type("tensor") == Type::make_tensor());
    assert(parse_type("invalid").is_unknown());
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  Zero Type System Tests\n";
    std::cout << "============================================\n\n";
    
    return run_all_tests();
}
