/**
 * @file test_multiline.cpp
 * @brief Acceptance tests for spec 018b — newlines inside (...)/[...] are
 *        insignificant, and the parser never hangs (no-progress guard).
 */

#include "ir/ir.hpp"
#include "ir/lowering.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <string>

using namespace zero::ir;
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

struct ParseResult { bool had_error; size_t fn_count; };

// Parse under a 1s wall-clock budget (catches a regression of the hang).
static ParseResult parse_budget(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto start = std::chrono::steady_clock::now();
    auto prog = parser.parse();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (ms > 1000) throw std::runtime_error("parse() exceeded 1s budget (hang)");
    return { parser.had_error(), prog.functions.size() };
}

// Count IR instructions of a given opcode across a module (for AST-equivalence).
static int count_op(const std::string& src, OpCode op) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    Lowering lowering;
    auto mod = lowering.lower(prog);
    int n = 0;
    for (const auto& fn : mod.functions)
        for (const auto& bb : fn.blocks)
            for (const auto& instr : bb.instrs)
                if (instr.op == op) ++n;
    return n;
}

// ─────────────────────────────────────────────────────────────────────

TEST(multiline_call) {
    auto r = parse_budget(
        "fn main() {\n"
        "  let c = matmul(\n"
        "    tensor([[1,2,3],[4,5,6]]),\n"
        "    tensor([[1,2],[3,4],[5,6]])\n"
        "  );\n"
        "}\n");
    assert(!r.had_error);
    assert(r.fn_count == 1);
}

TEST(multiline_2d_literal) {
    auto r = parse_budget(
        "fn main() {\n"
        "  let w = tensor([[1, 2],\n"
        "                  [3, 4]]);\n"
        "}\n");
    assert(!r.had_error);
}

TEST(multiline_1d_literal) {
    auto r = parse_budget(
        "fn main() {\n"
        "  let v = tensor([1,\n"
        "                  2,\n"
        "                  3]);\n"
        "}\n");
    assert(!r.had_error);
}

TEST(multiline_group) {
    auto r = parse_budget(
        "fn main() {\n"
        "  let x = (\n"
        "    1 + 2\n"
        "  );\n"
        "}\n");
    assert(!r.had_error);
}

TEST(multiline_equals_singleline) {
    const char* multi =
        "fn main() { let c = matmul(\n  tensor([[1,2,3]]),\n  tensor([[1],[2],[3]])\n); }";
    const char* single =
        "fn main() { let c = matmul(tensor([[1,2,3]]), tensor([[1],[2],[3]])); }";
    assert(count_op(multi, OpCode::TENSOR_MATMUL) == count_op(single, OpCode::TENSOR_MATMUL));
    assert(count_op(multi, OpCode::TENSOR_CONST_F32) == count_op(single, OpCode::TENSOR_CONST_F32));
    assert(count_op(multi, OpCode::TENSOR_MATMUL) == 1);
}

TEST(malformed_input_terminates) {
    // Stray garbage inside a function body must not hang.
    auto r = parse_budget("fn main() { ] ) , tensor([[[1]]]) @ }");
    assert(r.had_error);          // errors reported
    // and it returned within budget (else parse_budget would have thrown)
}

TEST(statement_newlines_unchanged) {
    // Normal multi-statement function still parses as separate statements.
    auto r = parse_budget(
        "fn main() {\n"
        "  let a = 1;\n"
        "  let b = 2;\n"
        "  let c = a + b;\n"
        "}\n");
    assert(!r.had_error);
    assert(r.fn_count == 1);
}

int main() {
    std::cout << "=== Spec 018b — multi-line parsing + hang-proofing ===\n";
    return run_all_tests();
}
