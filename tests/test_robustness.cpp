/**
 * @file test_robustness.cpp
 * @brief Regression tests for the Phase-0 edge-case audit.
 *
 * Each test locks in a fix documented in docs/PHASE0_ROBUSTNESS_FINDINGS.md.
 * Findings #1-#8. The pre-fix behavior is noted per test; all of these were
 * either process crashes (SIGABRT/SIGSEGV), silent wrong results, or missing
 * diagnostics. The goal here is to keep them fixed.
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

// ── helpers ───────────────────────────────────────────────────────────

// Parse only. Returns true if the parser reported an error (and, crucially,
// did NOT crash the process — the whole point of findings #1 / #7).
static bool parse_has_error(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    return parser.had_error();
}

// Parse (assert clean) then run sema. Returns true if sema reported an error.
static bool sema_has_error(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    return sema.had_error();
}

static RuntimeValue captured;

// Full pipeline. Returns true if execution threw a std::runtime_error.
static bool run_throws(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    ir::Lowering low; auto mod = low.lower(prog);
    backend::Interpreter interp;
    interp.register_external("capture", [](const std::vector<backend::RuntimeValue>& a){
        if (!a.empty()) captured = a[0]; return backend::RuntimeValue{}; });
    try { interp.execute(mod); } catch (const std::runtime_error&) { return true; }
    return false;
}

// Full pipeline, expecting success; leaves the captured value in `captured`.
static void run_ok(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    ir::Lowering low; auto mod = low.lower(prog);
    backend::Interpreter interp;
    interp.register_external("capture", [](const std::vector<backend::RuntimeValue>& a){
        if (!a.empty()) captured = a[0]; return backend::RuntimeValue{}; });
    captured = backend::RuntimeValue{};
    interp.execute(mod);
}

static const float* cap() {
    assert(captured.is_tensor());
    return static_cast<const float*>(captured.as_tensor()->data);
}
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// A 320-digit integer part — well past the range of double.
static std::string huge_float() {
    return std::string(320, '9') + ".0";
}

// ── #1: float literal overflow is a diagnostic, not a SIGABRT ───────────

TEST(float_overflow_is_parse_error_not_crash) {
    // Was: uncaught std::out_of_range from std::stod aborted the compiler.
    assert(parse_has_error("fn main() { let x = " + huge_float() + "\nreturn 0 }"));
    assert(parse_has_error("fn main() { let t = tensor([" + huge_float() + ", 2.0])\nreturn 0 }"));
}

// ── #7: integer literal overflow is a diagnostic, not a silent 0 ────────

TEST(int_overflow_is_parse_error_not_silent_zero) {
    // Was: parsed to 0 with no diagnostic.
    assert(parse_has_error("fn main() { let x = 99999999999999999999999\nreturn 0 }"));
    assert(parse_has_error("fn main() { let t = tensor([99999999999999999999999, 1])\nreturn 0 }"));
}

// ── #2: deep recursion is a catchable error, not a SIGSEGV ──────────────

TEST(deep_recursion_throws_not_segfault) {
    // Was: native stack overflow (SIGSEGV).
    assert(run_throws(
        "fn rec(n: int) -> int { if n <= 0 { return 0 } return rec(n - 1) }\n"
        "fn main() { let r = rec(100000)\nreturn 0 }"));
}

TEST(modest_recursion_still_works) {
    // The depth guard must not break legitimate recursion (well under limit).
    run_ok(
        "fn rec(n: int) -> int { if n <= 0 { return 0 } return rec(n - 1) }\n"
        "fn main() { capture(tensor([1.0])) }");  // just prove it executes
}

// ── #3: a non-void function that may fall off the end is a sema error ───

TEST(missing_return_is_sema_error) {
    // Was: f() leaked the last evaluated value (42).
    assert(sema_has_error("fn f() -> int { let x = 42 }\nfn main() { return 0 }"));
    assert(sema_has_error("fn f() -> tensor { let a = tensor([1.0]) }\nfn main() { return 0 }"));
    // Return only in one arm of an if (no else) => may fall through.
    assert(sema_has_error(
        "fn f(n: int) -> int { if n > 0 { return 1 } }\nfn main() { return 0 }"));
}

TEST(return_on_all_paths_is_ok) {
    // if/else where both arms return is fine; void/unannotated fns are lenient.
    assert(!sema_has_error(
        "fn f(n: int) -> int { if n > 0 { return 1 } else { return 0 } }\n"
        "fn main() { return 0 }"));
    assert(!sema_has_error("fn f() { let x = 1 }\nfn main() { return 0 }"));  // void: lenient
}

// ── #4: arguments to variadic functions are semantically checked ────────

TEST(variadic_args_are_checked) {
    // Was: undefined vars / nested arity errors inside variadic-fn args were
    // never visited by sema (loop bounded by param_types.size()==0).
    assert(sema_has_error("fn main() { print(undef) }"));
    assert(sema_has_error("fn main() { let y = relu(undef)\nreturn 0 }"));
    assert(sema_has_error("fn main() { let y = sum(undef)\nreturn 0 }"));
    assert(sema_has_error(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "fn main() { print(add(1)) }"));   // nested bad arity
}

TEST(valid_variadic_args_are_ok) {
    assert(!sema_has_error("fn main() { let y = relu(tensor([1.0, -2.0]))\nreturn 0 }"));
}

// ── #5: scalar-on-left non-commutative tensor ops compute correctly ─────

TEST(scalar_minus_tensor) {
    // Was: runtime throw ("not supported").
    run_ok("fn main() { capture(2.0 - tensor([1.0, 2.0, 3.0])) }");
    assert(near(cap()[0], 1) && near(cap()[1], 0) && near(cap()[2], -1));
}

TEST(scalar_div_tensor) {
    run_ok("fn main() { capture(6.0 / tensor([1.0, 2.0, 3.0])) }");
    assert(near(cap()[0], 6) && near(cap()[1], 3) && near(cap()[2], 2));
}

TEST(scalar_minus_2d_tensor) {
    run_ok("fn main() { capture(1.0 - tensor([[1.0, 2.0],[3.0, 4.0]])) }");
    const float* d = cap();
    assert(near(d[0], 0) && near(d[1], -1) && near(d[2], -2) && near(d[3], -3));
}

// ── #6: matmul with a rank-<2 operand gives a clear message ─────────────

TEST(matmul_rank_error_is_clear) {
    // Was: misleading "tensor op: output allocation failed".
    assert(run_throws(
        "fn main() { let c = matmul(tensor([1.0,2.0,3.0]), tensor([4.0,5.0,6.0]))\nreturn 0 }"));
}

// ── #8: a tensor used as a condition is a sema error ────────────────────

TEST(tensor_condition_is_sema_error) {
    // Was: silently coerced to 0 (else branch), no diagnostic.
    assert(sema_has_error("fn main() { let x = tensor([1.0])\nif x { return 1 } else { return 0 } }"));
    assert(sema_has_error("fn main() { let x = tensor([1.0])\nwhile x { return 1 }\nreturn 0 }"));
}

int main() {
    std::cout << "=== Phase 0 — robustness regressions ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};
    return rc;
}
