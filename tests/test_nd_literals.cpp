/**
 * @file test_nd_literals.cpp
 * @brief Acceptance tests for spec 015 — 2-D tensor literals + source matmul.
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
        std::cout << "  Running " << t.name << "... " << std::flush;
        try { t.func(); std::cout << "\033[32mPASS\033[0m\n"; ++passed; }
        catch (const std::exception& e) { std::cout << "\033[31mFAIL\033[0m: " << e.what() << "\n"; ++failed; }
        catch (...) { std::cout << "\033[31mFAIL\033[0m: unknown\n"; ++failed; }
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

// Run source; leaves result in `captured`. Asserts no parse error.
static void run_src(const std::string& src) {
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
}

// Parse only; returns had_error.
static bool parse_has_error(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    return parser.had_error();
}

// ─────────────────────────────────────────────────────────────────────

TEST(twod_shape_and_row_major) {
    run_src("fn main() { capture(tensor([[1,2,3],[4,5,6]])); }");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 2);
    assert(t->shape[0] == 2 && t->shape[1] == 3);
    assert(t->is_contiguous());
    const float* d = static_cast<const float*>(t->data);
    for (int i = 0; i < 6; ++i) assert(d[i] == static_cast<float>(i + 1));
}

TEST(twod_negatives_and_floats) {
    run_src("fn main() { capture(tensor([[-1.0, 2.0],[3.0, -4.0]])); }");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 2 && t->shape[0] == 2 && t->shape[1] == 2);
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == -1.0f && d[1] == 2.0f && d[2] == 3.0f && d[3] == -4.0f);
}

TEST(oned_still_works) {
    run_src("fn main() { capture(tensor([1, 2, 3])); }");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 1 && t->shape[0] == 3);
}

TEST(ragged_rejected) {
    assert(parse_has_error("fn main() { let a = tensor([[1,2],[3]]); }"));
}

TEST(threed_rejected) {
    assert(parse_has_error("fn main() { let a = tensor([[[1]]]); }"));
}

TEST(empty_rejected) {
    assert(parse_has_error("fn main() { let a = tensor([]); }"));
    assert(parse_has_error("fn main() { let a = tensor([[]]); }"));
}

TEST(matmul_from_source) {
    // [2,3] x [3,2] -> [2,2] = {22,28,49,64} (runtime's own matmul values).
    run_src(
        "fn main() {\n"
        "  let a = tensor([[1,2,3],[4,5,6]]);\n"
        "  let b = tensor([[1,2],[3,4],[5,6]]);\n"
        "  capture(matmul(a, b));\n"
        "}\n");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 2 && t->shape[0] == 2 && t->shape[1] == 2);
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 22.0f && d[1] == 28.0f && d[2] == 49.0f && d[3] == 64.0f);
}

TEST(matmul_shape_mismatch_throws) {
    // [2,3] x [2,2] — inner dims disagree -> runtime error -> throw.
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() {\n"
        "  let a = tensor([[1,2,3],[4,5,6]]);\n"
        "  let b = tensor([[1,2],[3,4]]);\n"
        "  capture(matmul(a, b));\n"
        "}\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    zero::sema::Sema sema; sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);
    Interpreter interp;
    install_capture(interp);
    bool threw = false;
    try { interp.execute(mod); }
    catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

int main() {
    std::cout << "=== Spec 015 — 2-D literals + source matmul ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};   // deterministic teardown (spec 013)
    return rc;
}
