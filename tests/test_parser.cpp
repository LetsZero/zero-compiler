/**
 * @file test_parser.cpp
 * @brief Unit tests for Zero Parser
 */

#include "parser/parser.hpp"
#include "source/source.hpp"
#include "ast/ast.hpp"

#include <iostream>
#include <vector>
#include <cassert>

using namespace zero::parser;
using namespace zero::source;
using namespace zero::ast;

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

TEST(test_empty_program) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(prog.functions.empty());
    assert(!parser.had_error());
}

TEST(test_simple_function) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    assert(prog.functions.size() == 1);
    assert(prog.functions[0].name == "main");
    assert(prog.functions[0].params.empty());
    assert(prog.functions[0].body.empty());
}

TEST(test_function_with_return) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { return 42; }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    assert(prog.functions.size() == 1);
    assert(prog.functions[0].body.size() == 1);
    
    auto& stmt = prog.functions[0].body[0];
    assert(stmt->is<ReturnStmt>());
    
    auto& ret = stmt->as<ReturnStmt>();
    assert(ret.value != nullptr);
    assert(ret.value->is<IntLiteral>());
    assert(ret.value->as<IntLiteral>().value == 42);
}

TEST(test_let_statement) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { let x = 10; }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& stmt = prog.functions[0].body[0];
    assert(stmt->is<LetStmt>());
    
    auto& let = stmt->as<LetStmt>();
    assert(let.name == "x");
    assert(let.init->is<IntLiteral>());
}

TEST(test_binary_expression) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { return 1 + 2 * 3; }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& ret = prog.functions[0].body[0]->as<ReturnStmt>();
    assert(ret.value->is<BinaryExpr>());
    
    // Should be (1 + (2 * 3)) due to precedence
    auto& add = ret.value->as<BinaryExpr>();
    assert(add.op == BinOp::ADD);
    assert(add.left->is<IntLiteral>());
    assert(add.right->is<BinaryExpr>());
    
    auto& mul = add.right->as<BinaryExpr>();
    assert(mul.op == BinOp::MUL);
}

TEST(test_function_call) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { foo(1, 2); }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& expr_stmt = prog.functions[0].body[0]->as<ExprStmt>();
    assert(expr_stmt.expr->is<CallExpr>());
    
    auto& call = expr_stmt.expr->as<CallExpr>();
    assert(call.callee == "foo");
    assert(call.args.size() == 2);
}

TEST(test_if_statement) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn main() { if x { return 1; } else { return 2; } }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& stmt = prog.functions[0].body[0];
    assert(stmt->is<IfStmt>());
    
    auto& if_stmt = stmt->as<IfStmt>();
    assert(if_stmt.condition->is<Identifier>());
    assert(if_stmt.then_branch.size() == 1);
    assert(if_stmt.else_branch.size() == 1);
}

TEST(test_while_statement) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn main() { while x { return 0; } }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& stmt = prog.functions[0].body[0];
    assert(stmt->is<WhileStmt>());
}

TEST(test_function_params) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn add(a, b) { return a + b; }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& fn = prog.functions[0];
    assert(fn.name == "add");
    assert(fn.params.size() == 2);
    assert(fn.params[0].name == "a");
    assert(fn.params[1].name == "b");
}

TEST(test_multiple_functions) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn foo() { }\n"
        "fn bar() { }\n"
        "fn main() { }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    assert(prog.functions.size() == 3);
}

TEST(test_comparison_operators) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn main() { return a < b == c > d; }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
}

TEST(test_unary_operators) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn main() { return -x; }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& ret = prog.functions[0].body[0]->as<ReturnStmt>();
    assert(ret.value->is<UnaryExpr>());
    assert(ret.value->as<UnaryExpr>().op == UnaryOp::NEG);
}

TEST(test_grouped_expression) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", 
        "fn main() { return (1 + 2) * 3; }");
    Parser parser(sm, id);
    
    Program prog = parser.parse();
    assert(!parser.had_error());
    
    auto& ret = prog.functions[0].body[0]->as<ReturnStmt>();
    assert(ret.value->is<BinaryExpr>());
    
    auto& mul = ret.value->as<BinaryExpr>();
    assert(mul.op == BinOp::MUL);
    assert(mul.left->is<GroupExpr>());
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  Zero Parser Tests\n";
    std::cout << "============================================\n\n";
    
    return run_all_tests();
}
