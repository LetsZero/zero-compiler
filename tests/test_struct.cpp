/**
 * @file test_struct.cpp
 * @brief Tests for structs — declaration, positional construction, field read.
 *
 * First slice (field mutation deferred): a struct groups named tensor/scalar
 * fields; constructed positionally (`Name(a, b)`), fields read with `.field`.
 * Interpreter represents a struct as an ordered list of field values.
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

static const float* cap_t() { assert(captured.is_tensor()); return static_cast<const float*>(captured.as_tensor()->data); }
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// ── construction + field read ────────────────────────────────────────────

TEST(construct_and_read_fields) {
    run_ok("struct P { a: tensor, b: tensor }\n"
           "fn main() { let p = P(tensor([1.0, 2.0]), tensor([3.0, 4.0]))\n capture(p.a) }");
    assert(near(cap_t()[0], 1) && near(cap_t()[1], 2));
    run_ok("struct P { a: tensor, b: tensor }\n"
           "fn main() { let p = P(tensor([1.0, 2.0]), tensor([3.0, 4.0]))\n capture(p.b) }");
    assert(near(cap_t()[0], 3) && near(cap_t()[1], 4));
}

TEST(field_in_arithmetic) {
    run_ok("struct P { a: tensor, b: tensor }\n"
           "fn main() { let p = P(tensor([1.0, 2.0]), tensor([10.0, 20.0]))\n capture(p.a + p.b) }");
    assert(near(cap_t()[0], 11) && near(cap_t()[1], 22));
}

// ── structs through functions (param + return) ───────────────────────────

TEST(struct_param_and_return) {
    run_ok(
        "struct P { a: tensor, b: tensor }\n"
        "fn swap(p: P) -> P { return P(p.b, p.a) }\n"
        "fn main() { let p = swap(P(tensor([1.0]), tensor([2.0])))\n capture(p.a) }");
    assert(near(cap_t()[0], 2));
}

// ── struct in a mutable cell, reassigned in a loop (SGD-style) ────────────

TEST(struct_cell_updates_in_loop) {
    run_ok(
        "struct P { value: tensor, grad: tensor }\n"
        "fn main() {\n"
        "  let p = P(tensor([0.0]), tensor([2.0]))\n"
        "  let i = 0\n"
        "  while i < 3 {\n"
        "    p = P(p.value - p.grad * tensor([0.1]), p.grad)\n"
        "    i = i + 1\n"
        "  }\n"
        "  capture(p.value)\n"   // 0 - 0.2*3 = -0.6
        "}");
    assert(near(cap_t()[0], -0.6f, 1e-4f));
}

// ── sema guards ───────────────────────────────────────────────────────────

TEST(wrong_field_count_is_error) {
    assert(sema_has_error("struct P { a: tensor, b: tensor }\n fn main() { let p = P(tensor([1.0]))\n return 0 }"));
}

TEST(unknown_field_is_error) {
    assert(sema_has_error("struct P { a: tensor }\n fn main() { let p = P(tensor([1.0]))\n capture(p.b) }"));
}

TEST(field_access_on_non_struct_is_error) {
    assert(sema_has_error("fn main() { let x = tensor([1.0])\n capture(x.foo) }"));
}

TEST(duplicate_struct_is_error) {
    assert(sema_has_error("struct P { a: tensor }\n struct P { b: tensor }\n fn main() { return 0 }"));
}

int main() {
    std::cout << "=== Structs ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};
    return rc;
}
