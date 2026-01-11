/**
 * @file test_ir.cpp
 * @brief Unit tests for Zero IR
 */

#include "ir/ir.hpp"
#include "ir/builder.hpp"
#include "ir/lowering.hpp"
#include "parser/parser.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>

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

TEST(test_value) {
    Value v1{1, zero::types::Type::make_int()};
    Value v2{2, zero::types::Type::make_float()};
    
    assert(v1.valid());
    assert(v1.id == 1);
    assert(v1.type.is_int());
    assert(v2.type.is_float());
    assert(v1 != v2);
}

TEST(test_module_and_function) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    
    assert(mod.functions.size() == 1);
    assert(fn.name == "main");
    assert(mod.get_function("main") == &fn);
    assert(mod.get_function("nonexistent") == nullptr);
}

TEST(test_basic_block) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    
    BasicBlock& entry = fn.entry();
    assert(entry.label == "entry");
    
    BasicBlock& bb1 = fn.new_block("test");
    assert(bb1.label == "test");
    assert(fn.blocks.size() == 2);
}

TEST(test_builder_constants) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    IRBuilder builder(fn);
    
    Value v1 = builder.const_int(42);
    Value v2 = builder.const_float(3.14);
    
    assert(v1.valid());
    assert(v2.valid());
    assert(fn.entry().instrs.size() == 2);
    assert(fn.entry().instrs[0].op == OpCode::CONST_INT);
    assert(fn.entry().instrs[0].imm_int == 42);
}

TEST(test_builder_arithmetic) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    IRBuilder builder(fn);
    
    Value a = builder.const_int(10);
    Value b = builder.const_int(20);
    Value sum = builder.add(a, b);
    Value diff = builder.sub(sum, a);
    
    assert(sum.valid());
    assert(diff.valid());
    assert(fn.entry().instrs.size() == 4);
}

TEST(test_builder_ret) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value v = builder.const_int(0);
    builder.ret(v);
    
    assert(fn.entry().instrs.size() == 2);
    assert(fn.entry().instrs[1].op == OpCode::RET);
}

TEST(test_lowering_simple) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { return 42; }");
    Parser parser(sm, id);
    auto prog = parser.parse();
    
    Lowering lowering;
    Module mod = lowering.lower(prog);
    
    assert(mod.functions.size() == 1);
    assert(mod.functions[0].name == "main");
    assert(!mod.functions[0].blocks.empty());
}

TEST(test_lowering_arithmetic) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { return 1 + 2 * 3; }");
    Parser parser(sm, id);
    auto prog = parser.parse();
    
    Lowering lowering;
    Module mod = lowering.lower(prog);
    
    // Should have: const 1, const 2, const 3, mul, add, ret
    assert(!mod.functions[0].blocks.empty());
    assert(mod.functions[0].blocks[0].instrs.size() >= 5);
}

TEST(test_lowering_variables) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn main() { let x = 10; return x; }");
    Parser parser(sm, id);
    auto prog = parser.parse();
    
    Lowering lowering;
    Module mod = lowering.lower(prog);
    
    assert(mod.functions.size() == 1);
}

TEST(test_print_module) {
    Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_int());
    IRBuilder builder(fn);
    
    Value v = builder.const_int(42);
    builder.ret(v);
    
    std::string output = print_module(mod);
    assert(!output.empty());
    assert(output.find("main") != std::string::npos);
    assert(output.find("const.i64 42") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  Zero IR Tests\n";
    std::cout << "============================================\n\n";
    
    return run_all_tests();
}
