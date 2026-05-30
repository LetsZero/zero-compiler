/**
 * @file test_reductions.cpp
 * @brief Acceptance tests for spec 014 — reductions from source.
 *
 * sum/mean/argmax over the whole tensor -> a [1] F32 tensor.
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

// Compile + run a source string, leaving the captured value in `captured`.
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

static const float* cap_data() {
    assert(captured.is_tensor());
    return static_cast<const float*>(captured.as_tensor()->data);
}

// ─────────────────────────────────────────────────────────────────────

TEST(sum_basic) {
    run_src("fn main() { capture(sum(tensor([1, 2, 3, 4]))); }");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 1 && t->shape[0] == 1 && t->is_contiguous());
    assert(cap_data()[0] == 10.0f);
}

TEST(mean_basic) {
    run_src("fn main() { capture(mean(tensor([1, 2, 3, 4]))); }");
    assert(cap_data()[0] == 2.5f);
}

TEST(argmax_middle) {
    run_src("fn main() { capture(argmax(tensor([1, 2, 9, 4]))); }");
    assert(cap_data()[0] == 2.0f);
}

TEST(argmax_first) {
    run_src("fn main() { capture(argmax(tensor([5, 1, 1, 1]))); }");
    assert(cap_data()[0] == 0.0f);
}

TEST(reduction_shape_is_one) {
    run_src("fn main() { capture(sum(tensor([1, 2, 3, 4, 5]))); }");
    const auto& t = captured.as_tensor();
    assert(t->ndim == 1);
    assert(t->shape[0] == 1);
    assert(t->dtype == zero::DType::F32);
    assert(t->is_contiguous());
}

TEST(sum_single_element) {
    run_src("fn main() { capture(sum(tensor([7]))); }");
    assert(cap_data()[0] == 7.0f);
}

TEST(scalar_paths_unaffected) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() { let x = 1 + 2; let y = x * 3; }");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    Lowering lowering;
    auto mod = lowering.lower(prog);
    for (const auto& fn : mod.functions)
        for (const auto& bb : fn.blocks)
            for (const auto& instr : bb.instrs) {
                assert(instr.op != OpCode::TENSOR_SUM);
                assert(instr.op != OpCode::TENSOR_MEAN);
                assert(instr.op != OpCode::TENSOR_ARGMAX);
            }
}

int main() {
    std::cout << "=== Spec 014 — reductions from source ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};   // deterministic teardown (spec 013)
    return rc;
}
