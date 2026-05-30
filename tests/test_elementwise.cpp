/**
 * @file test_elementwise.cpp
 * @brief Acceptance tests for spec 016 — exp/log/sqrt/tanh/sigmoid from source.
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

static const float* cap() { assert(captured.is_tensor()); return static_cast<const float*>(captured.as_tensor()->data); }
static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

// ─────────────────────────────────────────────────────────────────────

TEST(exp_basic) {
    run_src("fn main() { capture(exp(tensor([0.0, 1.0]))); }");
    assert(near(cap()[0], 1.0f) && near(cap()[1], 2.718281828f));
}

TEST(log_basic) {
    run_src("fn main() { capture(log(tensor([1.0, 2.718281828]))); }");
    assert(near(cap()[0], 0.0f) && near(cap()[1], 1.0f, 1e-4f));
}

TEST(sqrt_basic) {
    run_src("fn main() { capture(sqrt(tensor([4.0, 9.0]))); }");
    assert(near(cap()[0], 2.0f) && near(cap()[1], 3.0f));
}

TEST(sigmoid_zero) {
    run_src("fn main() { capture(sigmoid(tensor([0.0]))); }");
    assert(near(cap()[0], 0.5f));
}

TEST(tanh_zero) {
    run_src("fn main() { capture(tanh(tensor([0.0]))); }");
    assert(near(cap()[0], 0.0f));
}

TEST(shape_preserved_2d) {
    run_src("fn main() { capture(exp(tensor([[1,2],[3,4]]))); }");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 2 && t->shape[0] == 2 && t->shape[1] == 2);
}

TEST(scalar_paths_unaffected) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() { let x = 1 + 2; }");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    Lowering lowering;
    auto mod = lowering.lower(prog);
    for (const auto& fn : mod.functions)
        for (const auto& bb : fn.blocks)
            for (const auto& instr : bb.instrs) {
                assert(instr.op != OpCode::TENSOR_EXP);
                assert(instr.op != OpCode::TENSOR_LOG);
                assert(instr.op != OpCode::TENSOR_SQRT);
                assert(instr.op != OpCode::TENSOR_TANH);
                assert(instr.op != OpCode::TENSOR_SIGMOID);
            }
}

int main() {
    std::cout << "=== Spec 016 — elementwise from source ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};   // deterministic teardown (spec 013)
    return rc;
}
