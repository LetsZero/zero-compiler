/**
 * @file test_function_tensor_io.cpp
 * @brief Acceptance tests for spec 006 — function-level tensor I/O.
 *
 * Exercises user-defined functions with tensor parameters and tensor
 * return types via the full compile pipeline.
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
static void reset_captured() { captured = RuntimeValue(); }

static void install_capture(Interpreter& interp) {
    interp.register_external("capture",
        [](const std::vector<RuntimeValue>& args) {
            if (!args.empty()) captured = args[0];
            return RuntimeValue();
        });
}

static bool compile_and_run(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    if (parser.had_error()) return false;
    zero::sema::Sema sema;
    sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);
    Interpreter interp;
    install_capture(interp);
    try {
        interp.execute(mod);
    } catch (...) {
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────
// Test 1: single user function — tensor in, tensor out
// ─────────────────────────────────────────────────────────────────────

TEST(user_fn_double_it) {
    reset_captured();
    assert(compile_and_run(
        "fn double_it(x: tensor) -> tensor { return x + x; }\n"
        "fn main() {\n"
        "    let a = tensor([1.0, 2.0, 3.0, 4.0]);\n"
        "    capture(double_it(a));\n"
        "}\n"));
    assert(captured.is_tensor());
    const auto& t = captured.as_tensor();
    assert(t->ndim == 1 && t->shape[0] == 4);
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 2.0f && d[1] == 4.0f && d[2] == 6.0f && d[3] == 8.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Test 2: function composition with builtins inside (-, relu)
// ─────────────────────────────────────────────────────────────────────

TEST(user_fn_neg_then_relu) {
    reset_captured();
    assert(compile_and_run(
        "fn neg_relu(x: tensor) -> tensor { return relu(-x); }\n"
        "fn main() {\n"
        "    let a = tensor([1.0, -2.0, 3.0, -4.0]);\n"
        "    capture(neg_relu(a));\n"
        "}\n"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    // -a = {-1, 2, -3, 4}; relu = {0, 2, 0, 4}
    assert(d[0] == 0.0f);
    assert(d[1] == 2.0f);
    assert(d[2] == 0.0f);
    assert(d[3] == 4.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Test 3: multiple tensor parameters
// ─────────────────────────────────────────────────────────────────────

TEST(user_fn_two_params) {
    reset_captured();
    assert(compile_and_run(
        "fn weighted_add(a: tensor, b: tensor) -> tensor {\n"
        "    return a + b + b;\n"
        "}\n"
        "fn main() {\n"
        "    let x = tensor([1, 1, 1, 1]);\n"
        "    let y = tensor([10, 10, 10, 10]);\n"
        "    capture(weighted_add(x, y));\n"
        "}\n"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    // 1 + 10 + 10 = 21 for each element.
    for (int i = 0; i < 4; ++i) assert(d[i] == 21.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Test 4: scalar parameter regression — per-frame storage didn't
// break the int path
// ─────────────────────────────────────────────────────────────────────

TEST(user_fn_scalar_param) {
    // No tensor here; just confirm scalar int param/return still works.
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn id(x: int) -> int { return x; }\n"
        "fn main() {\n"
        "    let r = id(7);\n"
        "}\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    zero::sema::Sema sema; sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);
    Interpreter interp;
    // Don't need capture for this — we just need execution to not
    // throw and to terminate.
    interp.execute(mod);
    // No assertion on the value (we have no capture path for scalars
    // in this test file); the success criterion is "doesn't crash".
}

int main() {
    std::cout << "=== Spec 006 — function-level tensor I/O ===\n";
    return run_all_tests();
}
