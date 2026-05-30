/**
 * @file test_phase0_e2e.cpp
 * @brief Phase-0 end-to-end integration test (the growing safety net).
 *
 * Seeded by spec 014; extended by specs 015–019 until it culminates in
 * the MLP forward-pass capstone that marks "Phase 0 complete"
 * (see docs/ROADMAP.md). Compiles real .zero source through the whole
 * pipeline and checks captured numeric results against hand-computed
 * references.
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

static void run_src(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("phase0.zero", src);
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

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// ─────────────────────────────────────────────────────────────────────
// Spec 014 seed: reductions through the full pipeline.
// Later specs extend this file toward the MLP capstone.
// ─────────────────────────────────────────────────────────────────────

TEST(e2e_sum_then_capture) {
    run_src(
        "fn total(t: tensor) -> tensor { return sum(t); }\n"
        "fn main() { capture(total(tensor([2.0, 4.0, 6.0, 8.0]))); }\n");
    assert(captured.is_tensor());
    assert(near(static_cast<const float*>(captured.as_tensor()->data)[0], 20.0f));
}

TEST(e2e_mean_through_user_fn) {
    run_src(
        "fn avg(t: tensor) -> tensor { return mean(t); }\n"
        "fn main() { capture(avg(tensor([1.0, 2.0, 3.0]))); }\n");
    assert(near(static_cast<const float*>(captured.as_tensor()->data)[0], 2.0f));
}

int main() {
    std::cout << "=== Phase 0 — end-to-end integration ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};   // deterministic teardown (spec 013)
    return rc;
}
