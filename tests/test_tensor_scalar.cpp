/**
 * @file test_tensor_scalar.cpp
 * @brief Acceptance tests for spec 017 — tensor-scalar broadcast.
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
#include <cmath>
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

// Build module from src. Returns the module + keeps SourceManager alive
// via out-params is overkill; instead just run and capture.
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

// Run expecting a runtime throw; returns true if it threw. (Retained for
// future negative cases; the scalar-left ops no longer throw — see fix #5.)
[[maybe_unused]] static bool run_expect_throw(const std::string& src) {
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
    try { interp.execute(mod); } catch (const std::runtime_error&) { return true; }
    return false;
}

static const float* cap() { assert(captured.is_tensor()); return static_cast<const float*>(captured.as_tensor()->data); }
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// ─────────────────────────────────────────────────────────────────────

TEST(tensor_times_scalar) {
    run_src("fn main() { capture(tensor([1,2,3]) * 2.0); }");
    assert(near(cap()[0],2)&&near(cap()[1],4)&&near(cap()[2],6));
}

TEST(tensor_plus_scalar) {
    run_src("fn main() { capture(tensor([1,2,3]) + 10.0); }");
    assert(near(cap()[0],11)&&near(cap()[1],12)&&near(cap()[2],13));
}

TEST(tensor_div_scalar) {
    run_src("fn main() { capture(tensor([10,20,30]) / 2.0); }");
    assert(near(cap()[0],5)&&near(cap()[1],10)&&near(cap()[2],15));
}

TEST(tensor_minus_scalar) {
    run_src("fn main() { capture(tensor([5,6,7]) - 1.0); }");
    assert(near(cap()[0],4)&&near(cap()[1],5)&&near(cap()[2],6));
}

TEST(scalar_left_commutative) {
    run_src("fn main() { capture(2.0 * tensor([1,2,3])); }");
    assert(near(cap()[0],2)&&near(cap()[1],4)&&near(cap()[2],6));
    run_src("fn main() { capture(10.0 + tensor([1,2,3])); }");
    assert(near(cap()[0],11)&&near(cap()[1],12)&&near(cap()[2],13));
}

TEST(scalar_left_noncommutative) {
    // Robustness fix: `scalar - tensor` / `scalar / tensor` used to throw
    // ("not supported"). They now lift the scalar to the tensor's shape and
    // compute s OP t[i] per element. See PHASE0_ROBUSTNESS_FINDINGS #5.
    run_src("fn main() { capture(2.0 - tensor([1,2,3])); }");      // {1, 0, -1}
    assert(near(cap()[0],1)&&near(cap()[1],0)&&near(cap()[2],-1));
    run_src("fn main() { capture(6.0 / tensor([1,2,3])); }");      // {6, 3, 2}
    assert(near(cap()[0],6)&&near(cap()[1],3)&&near(cap()[2],2));
}

TEST(integer_scalar_literal) {
    run_src("fn main() { capture(tensor([1,2,3]) * 2); }");
    assert(near(cap()[0],2)&&near(cap()[1],4)&&near(cap()[2],6));
}

TEST(tensor_div_one_element_tensor_still_works) {
    // Regression: tensor / tensor[1] (the softmax path) unaffected.
    run_src("fn main() { capture(tensor([2,4,6]) / sum(tensor([1,1,1]))); }");
    assert(near(cap()[0], 2.0f/3.0f) && near(cap()[1], 4.0f/3.0f) && near(cap()[2], 2.0f));
}

TEST(twod_scaling) {
    run_src("fn main() { capture(tensor([[1,2],[3,4]]) * 3.0); }");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 2 && t->shape[0] == 2 && t->shape[1] == 2);
    assert(near(cap()[0],3)&&near(cap()[1],6)&&near(cap()[2],9)&&near(cap()[3],12));
}

int main() {
    std::cout << "=== Spec 017 — tensor-scalar broadcast ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};   // deterministic teardown (spec 013)
    return rc;
}
