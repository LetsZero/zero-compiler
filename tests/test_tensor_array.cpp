/**
 * @file test_tensor_array.cpp
 * @brief The tensor-array primitive: a runtime-indexable collection of tensors.
 *
 * This is the one autograd substrate that genuinely cannot be composed from
 * Tensor + Struct (a tensor holds floats, not tensors; struct fields are
 * compile-time indices). See docs/AUTOGRAD_DESIGN.md. Surface in Zero:
 *   let pool = tensorarray(n)     // n null slots
 *   ta_set(pool, k, some_tensor)  // store (any shape) at runtime index k
 *   let t = ta_get(pool, k)       // fetch
 * Reference semantics: passing a pool into a function and mutating it there is
 * visible to the caller — this is what an autograd tape/pool relies on.
 */

#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "ir/lowering.hpp"
#include "backend/interpreter.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>

using namespace zero;
using namespace zero::parser;
using namespace zero::source;
using namespace zero::backend;

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
        std::cout << "  Running " << t.name << "... " << std::flush;
        try { t.func(); std::cout << "\033[32mPASS\033[0m\n"; ++passed; }
        catch (const std::exception& e) { std::cout << "\033[31mFAIL\033[0m: " << e.what() << "\n"; ++failed; }
        catch (...) { std::cout << "\033[31mFAIL\033[0m: unknown\n"; ++failed; }
    }
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

static RuntimeValue captured;

static void run_ok(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    assert(!sema.had_error());
    ir::Lowering low; auto mod = low.lower(prog);
    Interpreter interp;
    interp.register_external("capture", [](const std::vector<RuntimeValue>& a){
        if (!a.empty()) captured = a[0]; return RuntimeValue{}; });
    captured = RuntimeValue{};
    interp.execute(mod);
}

static bool run_throws(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    ir::Lowering low; auto mod = low.lower(prog);
    Interpreter interp;
    interp.register_external("capture", [](const std::vector<RuntimeValue>& a){
        if (!a.empty()) captured = a[0]; return RuntimeValue{}; });
    try { interp.execute(mod); } catch (const std::runtime_error&) { return true; }
    return false;
}

static const float* cap_t() { assert(captured.is_tensor()); return static_cast<const float*>(captured.as_tensor()->data); }
static double cap_f() { assert(captured.is_float()); return captured.as_float(); }
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// ── store / fetch ──────────────────────────────────────────────────────────

TEST(set_then_get_roundtrips) {
    run_ok("fn main() {\n"
           "  let p = tensorarray(2)\n"
           "  ta_set(p, 0, tensor([1.0, 2.0]))\n"
           "  capture(ta_get(p, 0))\n"
           "}");
    assert(near(cap_t()[0], 1) && near(cap_t()[1], 2));
}

TEST(holds_heterogeneous_shapes) {
    // The whole point: a 1-D and a 2-D tensor coexist in one array.
    run_ok("fn main() {\n"
           "  let p = tensorarray(2)\n"
           "  ta_set(p, 0, tensor([7.0]))\n"
           "  ta_set(p, 1, tensor([[1.0, 2.0], [3.0, 4.0]]))\n"
           "  capture(ta_get(p, 1))\n"
           "}");
    assert(near(cap_t()[0],1)&&near(cap_t()[1],2)&&near(cap_t()[2],3)&&near(cap_t()[3],4));
}

TEST(get_returns_tensor_usable_in_ops) {
    // ta_get's result is a first-class tensor — feed it straight into matmul.
    run_ok("fn main() {\n"
           "  let p = tensorarray(2)\n"
           "  ta_set(p, 0, tensor([[1.0, 2.0]]))\n"
           "  ta_set(p, 1, tensor([[3.0], [4.0]]))\n"
           "  capture(matmul(ta_get(p, 0), ta_get(p, 1)))\n"
           "}");
    assert(near(cap_t()[0], 11));   // 1*3 + 2*4
}

TEST(runtime_index_set_and_get) {
    // Fill slots by a loop variable, read one back by a computed index.
    run_ok("fn main() {\n"
           "  let p = tensorarray(4)\n"
           "  let i = 0\n"
           "  while i < 4 {\n"
           "    ta_set(p, i, tensor([0.0]))\n"
           "    i = i + 1\n"
           "  }\n"
           "  ta_set(p, 2, tensor([42.0]))\n"
           "  let k = 1\n"
           "  capture(ta_get(p, k + 1))\n"
           "}");
    assert(near(cap_t()[0], 42));
}

TEST(overwrite_slot) {
    run_ok("fn main() {\n"
           "  let p = tensorarray(1)\n"
           "  ta_set(p, 0, tensor([1.0]))\n"
           "  ta_set(p, 0, tensor([9.0]))\n"
           "  capture(ta_get(p, 0))\n"
           "}");
    assert(near(cap_t()[0], 9));
}

// ── reference semantics (load-bearing for the autograd tape) ────────────────

TEST(mutation_through_function_is_visible) {
    // A pool passed into a function and mutated there is seen by the caller.
    run_ok("fn fill(p: tensorarray) {\n"
           "  ta_set(p, 0, tensor([123.0]))\n"
           "}\n"
           "fn main() {\n"
           "  let p = tensorarray(1)\n"
           "  fill(p)\n"
           "  capture(ta_get(p, 0))\n"
           "}");
    assert(near(cap_t()[0], 123));
}

TEST(pool_in_struct_field) {
    // A struct may carry a tensorarray field; mutating it in place works even
    // though struct fields can't be reassigned (the autograd Tape relies on this).
    run_ok("struct Box { pool: tensorarray }\n"
           "fn main() {\n"
           "  let b = Box(tensorarray(1))\n"
           "  ta_set(b.pool, 0, tensor([5.0]))\n"
           "  capture(ta_get(b.pool, 0))\n"
           "}");
    assert(near(cap_t()[0], 5));
}

// ── bounds / guards ─────────────────────────────────────────────────────────

TEST(out_of_range_get_throws) {
    assert(run_throws("fn main() {\n"
                      "  let p = tensorarray(2)\n"
                      "  ta_set(p, 0, tensor([1.0]))\n"
                      "  capture(ta_get(p, 5))\n"
                      "}"));
}

TEST(out_of_range_set_throws) {
    assert(run_throws("fn main() {\n"
                      "  let p = tensorarray(2)\n"
                      "  ta_set(p, 9, tensor([1.0]))\n"
                      "  return 0\n"
                      "}"));
}

TEST(get_empty_slot_throws) {
    // Reading a never-set slot is an error, not a silent null.
    assert(run_throws("fn main() {\n"
                      "  let p = tensorarray(2)\n"
                      "  capture(ta_get(p, 1))\n"
                      "}"));
}

int main() {
    std::cout << "=== Tensor-array primitive ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};
    return rc;
}
