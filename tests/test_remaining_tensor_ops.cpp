/**
 * @file test_remaining_tensor_ops.cpp
 * @brief Acceptance tests for spec 005 — wire the remaining tensor ops.
 *
 * Covers source-level dispatch for -, *, /, unary -, and relu(...),
 * plus IR-level coverage for matmul (no source path until 2-D
 * tensor literals land).
 */

#include "ir/ir.hpp"
#include "ir/builder.hpp"
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
#include <stdexcept>

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
        std::cout << "  Running " << t.name << "... ";
        try {
            t.func();
            std::cout << "\033[32mPASS\033[0m\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "\033[31mFAIL\033[0m: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "\033[31mFAIL\033[0m: unknown\n";
            ++failed;
        }
    }
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

static RuntimeValue captured;
static void reset_captured() { captured = RuntimeValue(); }

static void install_capture(Interpreter& interp) {
    interp.register_external("capture",
        [](const std::vector<RuntimeValue>& args) {
            if (!args.empty()) captured = args[0];
            return RuntimeValue();
        });
}

// Compile and run a source string through the full pipeline. Returns
// true on success; the captured RuntimeValue is in `captured`.
static bool compile_and_run(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    if (parser.had_error()) return false;
    zero::sema::Sema sema;
    sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);
    Interpreter interp;
    install_capture(interp);
    try {
        interp.execute(mod);
    } catch (...) {
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────
// Source-level dispatch tests
// ─────────────────────────────────────────────────────────────────────

TEST(tensor_sub_source) {
    reset_captured();
    assert(compile_and_run(
        "fn main() { let a = tensor([5, 4, 3, 2]); "
        "let b = tensor([1, 1, 1, 1]); capture(a - b); }"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 4.0f && d[1] == 3.0f && d[2] == 2.0f && d[3] == 1.0f);
}

TEST(tensor_mul_source) {
    reset_captured();
    assert(compile_and_run(
        "fn main() { let a = tensor([1, 2, 3, 4]); "
        "let b = tensor([2, 2, 2, 2]); capture(a * b); }"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 2.0f && d[1] == 4.0f && d[2] == 6.0f && d[3] == 8.0f);
}

TEST(tensor_div_source) {
    reset_captured();
    assert(compile_and_run(
        "fn main() { let a = tensor([10, 20, 30, 40]); "
        "let b = tensor([2, 4, 6, 8]); capture(a / b); }"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 5.0f);
    assert(d[1] == 5.0f);
    assert(d[2] == 5.0f);
    assert(d[3] == 5.0f);
}

TEST(tensor_neg_source) {
    reset_captured();
    assert(compile_and_run(
        "fn main() { let a = tensor([1, 2, 3, 4]); capture(-a); }"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == -1.0f && d[1] == -2.0f && d[2] == -3.0f && d[3] == -4.0f);
}

TEST(tensor_relu_source) {
    reset_captured();
    assert(compile_and_run(
        "fn main() { let a = tensor([-1.0, 0.0, 2.0, -3.0]); "
        "capture(relu(a)); }"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 0.0f);
    assert(d[1] == 0.0f);
    assert(d[2] == 2.0f);
    assert(d[3] == 0.0f);
}

TEST(mixed_expression) {
    // relu(a - b) * c where a={1,2,3,4}, b={3,3,3,3}, c={2,2,2,2}.
    // a - b = {-2, -1, 0, 1}; relu(.) = {0, 0, 0, 1}; * c = {0, 0, 0, 2}.
    reset_captured();
    assert(compile_and_run(
        "fn main() {\n"
        "  let a = tensor([1, 2, 3, 4]);\n"
        "  let b = tensor([3, 3, 3, 3]);\n"
        "  let c = tensor([2, 2, 2, 2]);\n"
        "  capture(relu(a - b) * c);\n"
        "}\n"));
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f);
    assert(d[3] == 2.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Scalar paths must remain unchanged
// ─────────────────────────────────────────────────────────────────────

TEST(scalar_arithmetic_no_tensor_opcodes) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() { let x = 5 - 2; let y = x * 3; "
        "let z = y / 2; let w = -z; }");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    Lowering lowering;
    auto mod = lowering.lower(prog);

    for (const auto& fn : mod.functions) {
        for (const auto& bb : fn.blocks) {
            for (const auto& instr : bb.instrs) {
                assert(instr.op != OpCode::TENSOR_ADD);
                assert(instr.op != OpCode::TENSOR_SUB);
                assert(instr.op != OpCode::TENSOR_MUL);
                assert(instr.op != OpCode::TENSOR_DIV);
                assert(instr.op != OpCode::TENSOR_NEG);
                assert(instr.op != OpCode::TENSOR_RELU);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// matmul via hand-constructed IR
// ─────────────────────────────────────────────────────────────────────
//
// Same inputs as the runtime's own basic_test.cpp: A=[[1,2,3],[4,5,6]],
// B=[[1,2],[3,4],[5,6]]. Expected C=[[22,28],[49,64]].

TEST(tensor_matmul_via_ir) {
    reset_captured();
    zero::ir::Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    IRBuilder b(fn);

    Value A = b.tensor_const_f32({2, 3}, {1, 2, 3, 4, 5, 6});
    Value B = b.tensor_const_f32({3, 2}, {1, 2, 3, 4, 5, 6});
    Value C = b.tensor_matmul(A, B);
    b.call("capture", {C}, zero::types::Type::make_void());
    b.ret();

    Interpreter interp;
    install_capture(interp);
    interp.execute(mod);

    assert(captured.is_tensor());
    const auto& t = captured.as_tensor();
    assert(t->ndim == 2 && t->shape[0] == 2 && t->shape[1] == 2);
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 22.0f);
    assert(d[1] == 28.0f);
    assert(d[2] == 49.0f);
    assert(d[3] == 64.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Error path: shape mismatch on tensor_sub throws with span
// ─────────────────────────────────────────────────────────────────────

TEST(tensor_sub_shape_mismatch_throws_with_span) {
    reset_captured();
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() { let a = tensor([1, 2, 3, 4]); "
        "let b = tensor([1, 2, 3]); capture(a - b); }");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    zero::sema::Sema sema; sema.analyze(prog);
    Lowering lowering;
    auto mod = lowering.lower(prog);
    Interpreter interp;
    install_capture(interp);

    bool threw = false;
    std::string msg;
    try {
        interp.execute(mod);
    } catch (const std::runtime_error& e) {
        threw = true;
        msg = e.what();
    }
    assert(threw);
    assert(msg.find("tensor_sub failed") != std::string::npos);
    // Span fragment present (we don't know exact offsets but format is @id:start-end).
    assert(msg.find("@") != std::string::npos);
    assert(msg.find("-") != std::string::npos);
}

int main() {
    std::cout << "=== Spec 005 — remaining tensor ops ===\n";
    return run_all_tests();
}
