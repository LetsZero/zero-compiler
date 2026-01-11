/**
 * @file test_backend.cpp
 * @brief Unit tests for Zero CPU Backend
 */

#include "backend/interpreter.hpp"
#include "ir/ir.hpp"
#include "ir/builder.hpp"
#include "ir/lowering.hpp"
#include "parser/parser.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>

using namespace zero::backend;
using namespace zero::ir;
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

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(test_const_int) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value v = builder.const_int(42);
    builder.ret(v);
    
    Interpreter interp;
    RuntimeValue result = interp.execute(mod);
    
    assert(result.is_int());
    assert(result.as_int() == 42);
}

TEST(test_arithmetic) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value a = builder.const_int(10);
    Value b = builder.const_int(3);
    Value sum = builder.add(a, b);      // 13
    Value diff = builder.sub(sum, b);    // 10
    Value prod = builder.mul(diff, b);   // 30
    Value quot = builder.div(prod, a);   // 3
    builder.ret(quot);
    
    Interpreter interp;
    RuntimeValue result = interp.execute(mod);
    
    assert(result.is_int());
    assert(result.as_int() == 3);
}

TEST(test_comparison) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value a = builder.const_int(5);
    Value b = builder.const_int(10);
    Value cmp = builder.cmp_lt(a, b);  // 5 < 10 = 1 (true)
    builder.ret(cmp);
    
    Interpreter interp;
    RuntimeValue result = interp.execute(mod);
    
    assert(result.is_int());
    assert(result.as_int() == 1);
}

TEST(test_negation) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value a = builder.const_int(42);
    Value neg = builder.neg(a);
    builder.ret(neg);
    
    Interpreter interp;
    RuntimeValue result = interp.execute(mod);
    
    assert(result.is_int());
    assert(result.as_int() == -42);
}

TEST(test_external_function) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value result = builder.call("external_fn", {}, zero::types::Type::make_int());
    builder.ret(result);
    
    Interpreter interp;
    
    // Register external function
    interp.register_external("external_fn", [](const std::vector<RuntimeValue>&) {
        return RuntimeValue(static_cast<int64_t>(99));
    });
    
    RuntimeValue res = interp.execute(mod);
    assert(res.is_int());
    assert(res.as_int() == 99);
}

TEST(test_lowering_and_execute) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { return 1 + 2 * 3; }");
    Parser parser(sm, id);
    auto prog = parser.parse();
    
    Lowering lowering;
    Module mod = lowering.lower(prog);
    
    Interpreter interp;
    RuntimeValue result = interp.execute(mod);
    
    // 1 + 2 * 3 = 1 + 6 = 7
    assert(result.is_int());
    assert(result.as_int() == 7);
}

TEST(test_exit_code) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value v = builder.const_int(0);
    builder.ret(v);
    
    Interpreter interp;
    interp.execute(mod);
    
    assert(interp.exit_code() == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  Zero CPU Backend Tests\n";
    std::cout << "============================================\n\n";
    
    return run_all_tests();
}
