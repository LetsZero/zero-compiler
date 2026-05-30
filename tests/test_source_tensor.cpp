/**
 * @file test_source_tensor.cpp
 * @brief Acceptance tests for spec 004 — source-level tensor literals
 *        and `+` dispatch.
 *
 * Compiles real .zero source through SourceManager -> Parser -> Sema ->
 * Lowering -> Interpreter and verifies tensor outputs via the `capture`
 * external registered with the interpreter.
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
        std::cout << "  Running " << t.name << "... " << std::flush;
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

// Captured RuntimeValue from the last interpreter run, set by the
// `capture` external. Reset per-test.
static RuntimeValue captured;
static void reset_captured() { captured = RuntimeValue(); }

static void install_capture(Interpreter& interp) {
    interp.register_external("capture",
        [](const std::vector<RuntimeValue>& args) {
            if (!args.empty()) captured = args[0];
            return RuntimeValue();
        });
}

// Compile + execute. Returns true if no parse / sema / runtime errors;
// false otherwise. Captures parse errors to `parse_had_error`.
static bool compile_and_run(const std::string& src, bool& parse_had_error) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    parse_had_error = parser.had_error();
    if (parse_had_error) return false;

    zero::sema::Sema sema;
    sema.analyze(prog);
    // We don't gate on sema errors here — some tests want sema-tolerant
    // behavior. Sema errors are reported but execution still proceeds.

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
// Test 1: happy path through real source
// ─────────────────────────────────────────────────────────────────────

TEST(source_tensor_add_produces_real_bytes) {
    reset_captured();
    bool parse_err = false;
    bool ok = compile_and_run(
        "fn main() {\n"
        "    let a = tensor([1.0, 2.0, 3.0, 4.0]);\n"
        "    let b = tensor([10.0, 10.0, 10.0, 10.0]);\n"
        "    let c = a + b;\n"
        "    capture(c);\n"
        "}\n",
        parse_err);
    assert(ok);
    assert(!parse_err);

    assert(captured.is_tensor());
    const auto& t = captured.as_tensor();
    assert(t != nullptr);
    assert(t->ndim == 1);
    assert(t->shape[0] == 4);
    assert(t->is_contiguous());
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 11.0f);
    assert(d[1] == 12.0f);
    assert(d[2] == 13.0f);
    assert(d[3] == 14.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Test 2: integer elements widen to float
// ─────────────────────────────────────────────────────────────────────

TEST(integer_elements_widen_to_float) {
    reset_captured();
    bool parse_err = false;
    bool ok = compile_and_run(
        "fn main() {\n"
        "    let a = tensor([1, 2, 3, 4]);\n"
        "    capture(a);\n"
        "}\n",
        parse_err);
    assert(ok);
    assert(!parse_err);

    assert(captured.is_tensor());
    const auto& t = captured.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    assert(d[0] == 1.0f);
    assert(d[3] == 4.0f);
}

// ─────────────────────────────────────────────────────────────────────
// Test 3: scalar + is unaffected (still lowers to OpCode::ADD)
// ─────────────────────────────────────────────────────────────────────

TEST(scalar_plus_is_unaffected) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() {\n"
        "    let x = 1 + 2;\n"
        "}\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());

    Lowering lowering;
    auto mod = lowering.lower(prog);

    // Walk the IR; assert we see OpCode::ADD and no OpCode::TENSOR_ADD.
    bool found_scalar_add = false;
    bool found_tensor_add = false;
    for (const auto& fn : mod.functions) {
        for (const auto& bb : fn.blocks) {
            for (const auto& instr : bb.instrs) {
                if (instr.op == OpCode::ADD) found_scalar_add = true;
                if (instr.op == OpCode::TENSOR_ADD) found_tensor_add = true;
            }
        }
    }
    assert(found_scalar_add);
    assert(!found_tensor_add);
}

// ─────────────────────────────────────────────────────────────────────
// Test 4: empty tensor literal is a parse error
// ─────────────────────────────────────────────────────────────────────

TEST(empty_tensor_literal_is_parse_error) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() {\n"
        "    let a = tensor([]);\n"
        "}\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(parser.had_error());
}

// ─────────────────────────────────────────────────────────────────────
// Test 5: `tensor` without parens is a parse error
// ─────────────────────────────────────────────────────────────────────

TEST(tensor_without_parens_is_parse_error) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() {\n"
        "    let x = tensor;\n"
        "}\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(parser.had_error());
}

// ─────────────────────────────────────────────────────────────────────
// Test 6: span attribution survives source -> IR
// ─────────────────────────────────────────────────────────────────────

TEST(tensor_ops_carry_spans_from_source) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero",
        "fn main() {\n"
        "    let a = tensor([1.0, 2.0]);\n"
        "    let b = tensor([3.0, 4.0]);\n"
        "    let c = a + b;\n"
        "}\n");
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    Lowering lowering;
    auto mod = lowering.lower(prog);

    bool tc_has_span = false, ta_has_span = false;
    for (const auto& fn : mod.functions) {
        for (const auto& bb : fn.blocks) {
            for (const auto& instr : bb.instrs) {
                if (instr.op == OpCode::TENSOR_CONST_F32 && instr.span.valid())
                    tc_has_span = true;
                if (instr.op == OpCode::TENSOR_ADD && instr.span.valid())
                    ta_has_span = true;
            }
        }
    }
    assert(tc_has_span);
    assert(ta_has_span);
}

int main() {
    std::cout << "=== Spec 004 — source-level tensor literals + dispatch ===\n";
    int rc = run_all_tests();
    // Spec 013: deterministic teardown — release any held TensorPtr now,
    // not during static destruction (which aborts on libstdc++).
    captured = RuntimeValue{};
    return rc;
}
