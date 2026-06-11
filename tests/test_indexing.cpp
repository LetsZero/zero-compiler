/**
 * @file test_indexing.cpp
 * @brief Element-level tensor indexing: `t[i]` (read) and `t[i] = x` (write).
 *
 * Unlocks List/Dict written in Zero (on Tensor+Struct) and the autograd tape.
 * Index is flat / row-major; reads yield a scalar float, writes mutate the
 * tensor's buffer in place (the tensor's identity / SSA value is unchanged).
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

static bool sema_has_error(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    return sema.had_error();
}

static const float* cap_t() { assert(captured.is_tensor()); return static_cast<const float*>(captured.as_tensor()->data); }
static double cap_f() { assert(captured.is_float()); return captured.as_float(); }
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// ── reads ────────────────────────────────────────────────────────────────

TEST(read_first_and_last) {
    run_ok("fn main() { let t = tensor([10.0, 20.0, 30.0])\n capture(t[0]) }");
    assert(near(static_cast<float>(cap_f()), 10));
    run_ok("fn main() { let t = tensor([10.0, 20.0, 30.0])\n capture(t[2]) }");
    assert(near(static_cast<float>(cap_f()), 30));
}

TEST(read_with_runtime_index) {
    run_ok("fn main() { let t = tensor([5.0, 6.0, 7.0])\n let i = 1\n capture(t[i + 1]) }");
    assert(near(static_cast<float>(cap_f()), 7));
}

TEST(read_2d_flat) {
    // row-major: t = [[1,2,3],[4,5,6]] -> flat [1,2,3,4,5,6]; t[4] = 5.
    run_ok("fn main() { let t = tensor([[1.0,2.0,3.0],[4.0,5.0,6.0]])\n capture(t[4]) }");
    assert(near(static_cast<float>(cap_f()), 5));
}

// ── writes ───────────────────────────────────────────────────────────────

TEST(write_one_element) {
    run_ok("fn main() { let t = tensor([1.0, 2.0, 3.0])\n t[1] = 99.0\n capture(t) }");
    assert(near(cap_t()[0], 1) && near(cap_t()[1], 99) && near(cap_t()[2], 3));
}

TEST(build_tensor_in_loop) {
    // The List-in-Zero pattern: allocate zeros, fill by index.
    run_ok(
        "fn main() {\n"
        "  let t = tensor([0.0, 0.0, 0.0, 0.0])\n"
        "  let i = 0\n"
        "  while i < 4 {\n"
        "    t[i] = 1.0 + i\n"
        "    i = i + 1\n"
        "  }\n"
        "  capture(t)\n"
        "}");
    assert(near(cap_t()[0],1)&&near(cap_t()[1],2)&&near(cap_t()[2],3)&&near(cap_t()[3],4));
}

TEST(write_preserves_tensor_identity) {
    // The mutation is visible through any other binding to the same tensor.
    // (We don't have aliasing syntax yet; loop test above is the proof.)
    run_ok(
        "fn main() {\n"
        "  let t = tensor([0.0, 0.0])\n"
        "  let i = 0\n"
        "  while i < 2 { t[i] = i + 1\n i = i + 1 }\n"
        "  capture(t[0] + t[1])\n"     // reads after writes see the new values
        "}");
    assert(near(static_cast<float>(cap_f()), 3));
}

// ── bounds and sema guards ───────────────────────────────────────────────

TEST(out_of_range_read_throws) {
    assert(run_throws("fn main() { let t = tensor([1.0, 2.0])\n capture(t[5]) }"));
}

TEST(out_of_range_write_throws) {
    assert(run_throws("fn main() { let t = tensor([1.0, 2.0])\n t[5] = 0.0\n return 0 }"));
}

TEST(index_non_tensor_is_sema_error) {
    assert(sema_has_error("fn main() { let x = 5\n capture(x[0]) }"));
}

TEST(float_index_is_accepted_and_truncates) {
    // Float indices are accepted (truncated), matching how tensor reads return
    // floats — e.g. `l.data[l.length[0]]` is a common List-in-Zero shape.
    run_ok("fn main() { let t = tensor([10.0, 20.0, 30.0])\n capture(t[1.7]) }");
    assert(near(static_cast<float>(cap_f()), 20));
}

TEST(write_to_non_tensor_is_sema_error) {
    assert(sema_has_error("fn main() { let x = 5\n x[0] = 1.0\n return 0 }"));
}

int main() {
    std::cout << "=== Element-level tensor indexing ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};
    return rc;
}
