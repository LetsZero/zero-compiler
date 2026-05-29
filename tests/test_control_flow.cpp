/**
 * @file test_control_flow.cpp
 * @brief Acceptance tests for spec 009 — IRBuilder block-index refactor.
 *
 * Before spec 009, IRBuilder held a BasicBlock* and lower_if/lower_while
 * held BasicBlock& into fn.blocks; the second create_block() reallocated
 * the vector and dangled them, so control-flow branch blocks came out
 * empty (a silent miscompile of every if/while). These tests prove
 * control flow now lowers and executes correctly.
 */

#include "ir/ir.hpp"
#include "ir/lowering.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "source/source.hpp"
#include "backend/interpreter.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <string>

using namespace zero::ir;
using namespace zero::backend;
using namespace zero::parser;
using namespace zero::source;

#define TEST(name) void name(); \
    static struct name##_register { \
        name##_register() { tests.push_back({#name, name}); } \
    } name##_instance; \
    void name()

struct TestCase { const char* name; void (*func)(); };
static std::vector<TestCase> tests;

static int run_all_tests() {
    int passed = 0, failed = 0;
    for (const auto& t : tests) {
        std::cout << "  Running " << t.name << "... ";
        try {
            t.func();
            std::cout << "\033[32mPASS\033[0m\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "\033[31mFAIL\033[0m: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "\033[31mFAIL\033[0m: unknown\n";
            ++failed;
        }
    }
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

static RuntimeValue captured;
static void install_capture(Interpreter& interp) {
    interp.register_external("capture",
        [](const std::vector<RuntimeValue>& args) {
            if (!args.empty()) captured = args[0];
            return RuntimeValue();
        });
}

// Compile + run; returns captured int (asserts it's an int).
static int64_t run_capture_int(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    zero::sema::Sema sema; sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);
    Interpreter interp;
    install_capture(interp);
    captured = RuntimeValue();
    interp.execute(mod);
    assert(captured.is_int());
    return captured.as_int();
}

// ─────────────────────────────────────────────────────────────────────
// Test 1: if-true branch executes (the exact case that was wrong before)
// ─────────────────────────────────────────────────────────────────────

TEST(if_true_branch_executes) {
    int64_t r = run_capture_int(
        "fn f(n: int) -> int { if (n < 2) { return 99; } return 7; }\n"
        "fn main() { capture(f(0)); }\n");
    assert(r == 99);
}

// ─────────────────────────────────────────────────────────────────────
// Test 2: if-false falls through
// ─────────────────────────────────────────────────────────────────────

TEST(if_false_falls_through) {
    int64_t r = run_capture_int(
        "fn f(n: int) -> int { if (n < 2) { return 99; } return 7; }\n"
        "fn main() { capture(f(5)); }\n");
    assert(r == 7);
}

// ─────────────────────────────────────────────────────────────────────
// Test 3: if/else, both branches
// ─────────────────────────────────────────────────────────────────────

TEST(if_else_both_branches) {
    const char* prog =
        "fn g(n: int) -> int { if (n < 0) { return 1; } else { return 2; } }\n"
        "fn main() { capture(g(SUB)); }\n";

    {
        std::string s = prog;
        s.replace(s.find("SUB"), 3, "-1");
        assert(run_capture_int(s) == 1);
    }
    {
        std::string s = prog;
        s.replace(s.find("SUB"), 3, "3");
        assert(run_capture_int(s) == 2);
    }
}

// ─────────────────────────────────────────────────────────────────────
// Test 4: while loop body block is non-empty and the loop terminates.
//
// The language has no in-place mutation yet, so we can't easily build a
// classic accumulator. Instead we verify (a) the program with a while
// loop lowers and runs to completion without hanging or crashing, and
// (b) the while.body block contains at least one instruction.
// ─────────────────────────────────────────────────────────────────────

TEST(while_body_block_non_empty_and_terminates) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() {\n"
        "    let i = 0;\n"
        "    while (i < 0) {\n"        // condition false at entry: loop body
        "        let x = i + 1;\n"     // never executes, but must lower
        "    }\n"
        "    capture(1);\n"
        "}\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    zero::sema::Sema sema; sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);

    // The while.body block must contain at least one instruction (the
    // `let x = i + 1` lowering). Pre-spec-009 this block was empty.
    bool found_nonempty_body = false;
    for (const auto& fn : mod.functions) {
        for (const auto& bb : fn.blocks) {
            if (bb.label == "while.body" && !bb.instrs.empty()) {
                found_nonempty_body = true;
            }
        }
    }
    assert(found_nonempty_body);

    // And it runs to completion (no hang / crash); capture sees 1.
    Interpreter interp;
    install_capture(interp);
    captured = RuntimeValue();
    interp.execute(mod);
    assert(captured.is_int() && captured.as_int() == 1);
}

// ─────────────────────────────────────────────────────────────────────
// Test 5: recursive Fibonacci (carried over from spec 008)
// ─────────────────────────────────────────────────────────────────────

TEST(recursive_fibonacci) {
    int64_t r = run_capture_int(
        "fn fib(n: int) -> int {\n"
        "    if (n < 2) { return n; }\n"
        "    return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "fn main() { capture(fib(8)); }\n");
    assert(r == 21);
}

// ─────────────────────────────────────────────────────────────────────
// Test 6: IR-level regression guard — if branches are non-empty.
// ─────────────────────────────────────────────────────────────────────

TEST(if_branch_blocks_non_empty) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn f(n: int) -> int {\n"
        "    if (n < 0) { return 1; } else { return 2; }\n"
        "}\n"
        "fn main() { capture(f(0)); }\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    zero::sema::Sema sema; sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);

    bool then_nonempty = false, else_nonempty = false;
    for (const auto& fn : mod.functions) {
        for (const auto& bb : fn.blocks) {
            if (bb.label == "if.then" && !bb.instrs.empty()) then_nonempty = true;
            if (bb.label == "if.else" && !bb.instrs.empty()) else_nonempty = true;
        }
    }
    assert(then_nonempty);
    assert(else_nonempty);
}

int main() {
    std::cout << "=== Spec 009 — control flow (IRBuilder block-index) ===\n";
    return run_all_tests();
}
