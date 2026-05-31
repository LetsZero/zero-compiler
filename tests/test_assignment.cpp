/**
 * @file test_assignment.cpp
 * @brief Tests for variable assignment / mutation (`x = expr`).
 *
 * Assignment turns mutable state on: `while` loops can finally change their
 * own counters, and tensors can be reassigned. Mutated variables lower to
 * memory cells (alloca/load/store) so updates are visible across basic blocks;
 * non-mutated variables keep their direct-SSA binding (identical IR).
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

static bool sema_has_error(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    return sema.had_error();
}

static bool parse_has_error(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    return parser.had_error();
}

static int64_t cap_int() { assert(captured.is_int()); return captured.as_int(); }
static const float* cap_t() { assert(captured.is_tensor()); return static_cast<const float*>(captured.as_tensor()->data); }
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// ── scalar mutation ─────────────────────────────────────────────────────

TEST(simple_reassign) {
    run_ok("fn main() { let x = 1\n x = 42\n capture(x) }");
    assert(cap_int() == 42);
}

TEST(reassign_uses_old_value) {
    run_ok("fn main() { let x = 10\n x = x + 5\n capture(x) }");
    assert(cap_int() == 15);
}

// ── the headline: a while loop that mutates its counter ──────────────────

TEST(while_loop_counts_down) {
    // Was impossible before assignment — the condition variable could not
    // change, so the loop never terminated (or never ran).
    run_ok(
        "fn main() {\n"
        "  let count = 5\n"
        "  let total = 0\n"
        "  while count > 0 {\n"
        "    total = total + count\n"
        "    count = count - 1\n"
        "  }\n"
        "  capture(total)\n"   // 5+4+3+2+1 = 15
        "}");
    assert(cap_int() == 15);
}

TEST(while_loop_factorial) {
    run_ok(
        "fn main() {\n"
        "  let n = 5\n"
        "  let acc = 1\n"
        "  while n > 1 {\n"
        "    acc = acc * n\n"
        "    n = n - 1\n"
        "  }\n"
        "  capture(acc)\n"     // 120
        "}");
    assert(cap_int() == 120);
}

// ── mutation inside if branches ──────────────────────────────────────────

TEST(reassign_in_if_branch) {
    run_ok(
        "fn main() {\n"
        "  let x = 0\n"
        "  if 1 > 0 { x = 7 } else { x = 9 }\n"
        "  capture(x)\n"
        "}");
    assert(cap_int() == 7);
}

// ── mutated function parameter (gets a cell, initialised from the arg) ────

TEST(reassign_parameter) {
    run_ok(
        "fn bump(p: int) -> int { p = p + 100\n return p }\n"
        "fn main() { capture(bump(1)) }");
    assert(cap_int() == 101);
}

// ── tensor reassignment ──────────────────────────────────────────────────

TEST(tensor_reassign_chain) {
    run_ok("fn main() { let x = tensor([1.0, 2.0, 3.0])\n x = x * 2.0\n x = x + 1.0\n capture(x) }");
    const float* d = cap_t();
    assert(near(d[0], 3) && near(d[1], 5) && near(d[2], 7));
}

// ── sema guards ──────────────────────────────────────────────────────────

TEST(assign_to_undeclared_is_error) {
    assert(sema_has_error("fn main() { x = 5\n return 0 }"));
}

TEST(assign_type_mismatch_is_error) {
    // x is a tensor; assigning a scalar is a type mismatch.
    assert(sema_has_error("fn main() { let x = tensor([1.0])\n x = 5\n return 0 }"));
}

TEST(equality_is_not_assignment) {
    // `==` must still parse as a comparison, not be confused with `=`.
    assert(!parse_has_error("fn main() { let x = 1\n if x == 1 { return 1 }\n return 0 }"));
}

// ── non-mutated variables are unaffected (smoke) ─────────────────────────

TEST(non_mutated_var_still_works) {
    run_ok("fn main() { let a = tensor([2.0, 4.0])\n let b = a + a\n capture(b) }");
    const float* d = cap_t();
    assert(near(d[0], 4) && near(d[1], 8));
}

int main() {
    std::cout << "=== Assignment / mutation ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};
    return rc;
}
