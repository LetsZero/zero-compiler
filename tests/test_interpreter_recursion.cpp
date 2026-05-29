/**
 * @file test_interpreter_recursion.cpp
 * @brief Acceptance tests for spec 008 — interpreter call-stack
 *        frame-index refactor.
 *
 * Exercises patterns that would have crashed under the previous
 * "auto& current = call_stack_.back()" code once nested calls
 * forced the std::vector to reallocate.
 */

#include "ir/ir.hpp"
#include "ir/builder.hpp"
#include "ir/lowering.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "source/source.hpp"
#include "backend/interpreter.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <string>
#include <sstream>

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

// ─────────────────────────────────────────────────────────────────────
// Test 1: deep linear call chain — f0 → f1 → ... → fN.
//
// Each fk returns fk+1() + 1; the terminal f200 returns 0.
// With N = 200 the call stack grows past any reasonable initial
// vector capacity, forcing multiple reallocations. Under the old
// code this would corrupt the held reference and produce wrong
// results or a crash. The expected result is 200.
// ─────────────────────────────────────────────────────────────────────

TEST(deep_linear_chain_survives_reallocation) {
    constexpr int N = 200;

    std::ostringstream src;
    src << "fn f" << N << "() -> int { return 0; }\n";
    for (int i = N - 1; i >= 0; --i) {
        src << "fn f" << i << "() -> int { return f" << (i + 1) << "() + 1; }\n";
    }
    src << "fn main() {\n";
    src << "    let r = f0();\n";
    src << "    capture(r);\n";
    src << "}\n";

    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src.str());
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
    assert(captured.as_int() == N);
}

// ─────────────────────────────────────────────────────────────────────
// Test 2: regression — simple single-call still works
// ─────────────────────────────────────────────────────────────────────

TEST(single_user_function_still_works) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn add_one(x: int) -> int { return x; }\n"
        "fn main() { let r = add_one(41); capture(r); }\n");
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
    // The function as written returns x directly; expected 41.
    assert(captured.is_int());
    assert(captured.as_int() == 41);
}

// ─────────────────────────────────────────────────────────────────────
// Test 3: recursive Fibonacci — nested same-function calls.
//
// fib(8) = 21. Many recursive frames stack, exercising the
// per-iteration `call_stack_[my_frame_idx]` re-derivation pattern
// at every nested call.
// ─────────────────────────────────────────────────────────────────────

// Note: a recursive-Fibonacci test was originally planned for this spec
// but surfaced a *separate* bug in `IRBuilder` — `create_block`
// invalidated the builder's block pointer and any held `BasicBlock&`
// references when `fn.blocks` reallocated, miscompiling every if/while.
// That bug was fixed in spec 009; the Fibonacci test now lives in
// tests/test_control_flow.cpp.

int main() {
    std::cout << "=== Spec 008 — interpreter frame-index refactor ===\n";
    return run_all_tests();
}
