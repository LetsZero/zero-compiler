/**
 * @file test_tensor_add_e2e.cpp
 * @brief Acceptance test for spec 003 — end-to-end tensor add.
 *
 * Builds an IR module by hand (no parser involvement), runs it through
 * the interpreter, and asserts that TENSOR_ADD calls zero::ops::add from
 * the frozen runtime and produces real, bit-exact output bytes.
 *
 * Also exercises the error path: a shape-mismatched TENSOR_ADD must throw
 * a std::runtime_error whose message embeds the IR instruction's source
 * span (from spec 002).
 */

#include "ir/ir.hpp"
#include "ir/builder.hpp"
#include "backend/interpreter.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace zero::ir;
using namespace zero::backend;
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

// Helper: build a module that constructs two F32 tensors of shapes
// `a_shape` and `b_shape`, adds them, and returns void. The final tensor
// produced by TENSOR_ADD is stored on the named test_result global below
// (via a trick: we look it up post-execution by SSA id).
//
// We can't easily reach inside the interpreter post-run, so we use a
// register_external callback approach: the test module calls back into
// the test with the final tensor right before returning. Simpler approach:
// expose the values map. Even simpler: just structure the IR so the LAST
// op is the tensor_add, and after execute() returns we know the highest
// SSA id corresponds to its result. But the interpreter doesn't expose
// the values map publicly.
//
// Cleanest workable solution: use an external function registered as a
// "capture" sink. The module calls it with the result Value as a CALL
// operand, the external grabs the RuntimeValue and stashes it.

static zero::backend::RuntimeValue captured;
static void install_capture(Interpreter& interp) {
    interp.register_external("__capture",
        [](const std::vector<zero::backend::RuntimeValue>& args) {
            if (!args.empty()) captured = args[0];
            return zero::backend::RuntimeValue();
        });
}

// Reset between tests.
static void reset_captured() {
    captured = zero::backend::RuntimeValue();
}

// ─────────────────────────────────────────────────────────────────────
// Test 1: happy path
// ─────────────────────────────────────────────────────────────────────

TEST(tensor_add_happy_path_produces_real_bytes) {
    reset_captured();

    zero::ir::Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    IRBuilder b(fn);

    Value a = b.tensor_const_f32({4}, {1.0f, 2.0f, 3.0f, 4.0f});
    Value bv = b.tensor_const_f32({4}, {10.0f, 10.0f, 10.0f, 10.0f});
    Value c = b.tensor_add(a, bv);
    b.call("__capture", {c}, zero::types::Type::make_void());
    b.ret();

    Interpreter interp;
    install_capture(interp);
    interp.execute(mod);

    // The captured RuntimeValue must be a tensor with bytes {11, 12, 13, 14}.
    assert(captured.is_tensor());
    const auto& t = captured.as_tensor();
    assert(t != nullptr);
    assert(t->ndim == 1);
    assert(t->shape[0] == 4);
    assert(t->is_contiguous());
    const float* data = static_cast<const float*>(t->data);
    assert(data[0] == 11.0f);
    assert(data[1] == 12.0f);
    assert(data[2] == 13.0f);
    assert(data[3] == 14.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Test 2: shape mismatch surfaces as a throw with span
// ─────────────────────────────────────────────────────────────────────

TEST(tensor_add_shape_mismatch_throws_with_span) {
    reset_captured();

    zero::ir::Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    IRBuilder b(fn);

    Value a = b.tensor_const_f32({4}, {1.0f, 2.0f, 3.0f, 4.0f});
    Value bv = b.tensor_const_f32({3}, {10.0f, 10.0f, 10.0f});

    // Mark the TENSOR_ADD with a known source span so we can assert it
    // appears in the error message.
    Span s = Span::range(SourceID{42}, 100, 113);
    b.set_current_span(s);
    Value c = b.tensor_add(a, bv);
    (void)c;
    b.set_current_span(Span::invalid());
    b.ret();

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
    // Message must mention the failing op.
    assert(msg.find("tensor_add failed") != std::string::npos);
    // Message must embed the span as @<id>:<start>-<end>.
    assert(msg.find("@42:100-113") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────
// Test 3: writes-zero-bytes-on-error contract is preserved across the bridge
// ─────────────────────────────────────────────────────────────────────
//
// The interpreter pre-fills the output with 0xAB sentinel before the
// runtime call. After a shape-mismatched call, the runtime guarantees no
// writes occurred, so the sentinel must still be intact. We can't observe
// the output directly here (it was thrown away in the exception path),
// but the runtime's own ZeroOpStatusTest already proves this for the
// underlying op. This test is the bridge-side analogue: confirm the
// exception's msg references the runtime's error category, not garbled
// state, by checking the message contains "code=" (i.e. structured
// error info, not just a crash).

TEST(tensor_add_error_message_is_structured) {
    reset_captured();

    zero::ir::Module mod;
    Function& fn = mod.add_function("main", {}, zero::types::Type::make_void());
    IRBuilder b(fn);

    Value a = b.tensor_const_f32({4}, {1, 2, 3, 4});
    Value bv = b.tensor_const_f32({3}, {1, 2, 3});
    b.tensor_add(a, bv);
    b.ret();

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
    assert(msg.find("code=") != std::string::npos);
}

int main() {
    std::cout << "=== Spec 003 — tensor add end-to-end ===\n";
    return run_all_tests();
}
