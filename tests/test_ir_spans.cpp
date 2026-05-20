/**
 * @file test_ir_spans.cpp
 * @brief Acceptance tests for spec 002 — source spans on IR instructions.
 *
 * Tests derived from docs/specs/002-ir-source-spans.md §4.
 *
 * Two halves:
 *   - Builder-level: default span is invalid; set_current_span overwrites
 *     cleanly; emit() stamps the current span onto every Instruction.
 *   - Lowering-level: instructions emitted by Lowering carry the span of
 *     the AST node they were lowered from.
 */

#include "ir/ir.hpp"
#include "ir/builder.hpp"
#include "ir/lowering.hpp"
#include "parser/parser.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>
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

// ─────────────────────────────────────────────────────────────────────
// Builder-level tests
// ─────────────────────────────────────────────────────────────────────

TEST(default_instruction_span_is_invalid) {
    Instruction instr;
    assert(!instr.span.valid());
}

TEST(builder_default_current_span_is_invalid) {
    zero::ir::Module mod;
    Function& fn = mod.add_function("f", {}, zero::types::Type::make_void());
    IRBuilder b(fn);
    assert(!b.current_span().valid());
}

TEST(set_current_span_round_trip) {
    zero::ir::Module mod;
    Function& fn = mod.add_function("f", {}, zero::types::Type::make_void());
    IRBuilder b(fn);
    Span s = Span::range(SourceID{7}, 10, 20);
    b.set_current_span(s);
    Span got = b.current_span();
    assert(got.source_id == s.source_id);
    assert(got.start_offset == s.start_offset);
    assert(got.end_offset == s.end_offset);
}

TEST(emit_stamps_current_span) {
    zero::ir::Module mod;
    Function& fn = mod.add_function("f", {}, zero::types::Type::make_void());
    IRBuilder b(fn);

    Span s = Span::range(SourceID{3}, 100, 105);
    b.set_current_span(s);
    Value v = b.const_int(42);
    (void)v;

    const Instruction& tail = fn.blocks[0].instrs.back();
    assert(tail.op == OpCode::CONST_INT);
    assert(tail.span.source_id == s.source_id);
    assert(tail.span.start_offset == 100);
    assert(tail.span.end_offset == 105);
}

TEST(second_set_current_span_overwrites) {
    zero::ir::Module mod;
    Function& fn = mod.add_function("f", {}, zero::types::Type::make_void());
    IRBuilder b(fn);

    Span s1 = Span::range(SourceID{1}, 0, 5);
    Span s2 = Span::range(SourceID{1}, 50, 60);
    b.set_current_span(s1);
    b.const_int(1);
    b.set_current_span(s2);
    b.const_int(2);

    const auto& instrs = fn.blocks[0].instrs;
    assert(instrs.size() >= 2);
    assert(instrs[0].span.start_offset == 0);
    assert(instrs[1].span.start_offset == 50);
}

TEST(instruction_emitted_without_set_has_invalid_span) {
    zero::ir::Module mod;
    Function& fn = mod.add_function("f", {}, zero::types::Type::make_void());
    IRBuilder b(fn);
    // Never call set_current_span. Default state should yield invalid span.
    b.const_int(99);
    assert(!fn.blocks[0].instrs[0].span.valid());
}

// ─────────────────────────────────────────────────────────────────────
// Dumper tests
// ─────────────────────────────────────────────────────────────────────

TEST(print_omits_comment_when_span_invalid) {
    Instruction instr;
    instr.op = OpCode::NOP;
    std::string s = print_instruction(instr);
    assert(s.find("; @") == std::string::npos);
}

TEST(print_includes_comment_when_span_valid) {
    Instruction instr;
    instr.op = OpCode::NOP;
    instr.span = Span::range(SourceID{5}, 12, 18);
    std::string s = print_instruction(instr);
    assert(s.find("; @5:12-18") != std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────
// Lowering-level tests
// ─────────────────────────────────────────────────────────────────────

// Helper: parse a source string and lower it; return the resulting module.
static zero::ir::Module parse_and_lower(SourceManager& sm, const std::string& src) {
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    Lowering lowering;
    return lowering.lower(prog);
}

TEST(lowered_add_carries_binaryexpr_span) {
    SourceManager sm;
    auto mod = parse_and_lower(sm, "fn f() { let x = 1 + 2; }");

    // Find the ADD instruction (or its CONST predecessors — they should also
    // have valid spans).
    bool found_add = false;
    for (const auto& fn : mod.functions) {
        for (const auto& bb : fn.blocks) {
            for (const auto& instr : bb.instrs) {
                if (instr.op == OpCode::ADD) {
                    found_add = true;
                    assert(instr.span.valid());
                    // The BinaryExpr span should cover at least the "1 + 2"
                    // substring, so end_offset > start_offset and source_id
                    // matches our test buffer.
                    assert(instr.span.end_offset > instr.span.start_offset);
                }
            }
        }
    }
    assert(found_add);
}

TEST(two_statements_get_distinct_spans) {
    SourceManager sm;
    auto mod = parse_and_lower(sm,
        "fn f() {\n"
        "  let x = 1 + 2;\n"
        "  let y = 3 + 4;\n"
        "}");

    std::vector<Span> add_spans;
    for (const auto& fn : mod.functions) {
        for (const auto& bb : fn.blocks) {
            for (const auto& instr : bb.instrs) {
                if (instr.op == OpCode::ADD) add_spans.push_back(instr.span);
            }
        }
    }
    assert(add_spans.size() >= 2);
    assert(add_spans[0].start_offset != add_spans[1].start_offset);
}

// ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Spec 002 — IR source spans ===\n";
    return run_all_tests();
}
