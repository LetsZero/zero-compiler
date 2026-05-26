/**
 * @file test_parser_recovery.cpp
 * @brief Acceptance tests for spec 007 — parser error-recovery hardening.
 *
 * Exercises the tensor-literal bracket loop with inputs that previously
 * hung the parser (and caused 255s / 339s ctest timeouts in specs 005
 * and 006 respectively).
 */

#include "parser/parser.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <string>

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

// Run the parser on a source string. Asserts it returns within 1 second
// — the spec 007 budget that would have caught the original 255s/339s
// hangs. Returns the parser's had_error() and the function count.
struct ParseResult { bool had_error; size_t fn_count; };
static ParseResult parse_with_timeout(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);

    auto start = std::chrono::steady_clock::now();
    auto prog = parser.parse();
    auto elapsed = std::chrono::steady_clock::now() - start;

    auto secs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (secs > 1000) {
        throw std::runtime_error("parse() exceeded 1s wall-clock budget");
    }
    return { parser.had_error(), prog.functions.size() };
}

// ─────────────────────────────────────────────────────────────────────
// Test 1: unrecognised token inside brackets reports an error AND
// returns within the budget.
// ─────────────────────────────────────────────────────────────────────

TEST(bad_token_in_tensor_literal_does_not_hang) {
    auto r = parse_with_timeout(
        "fn main() {\n"
        "    let a = tensor([1, \"x\", 2]);\n"
        "}\n");
    assert(r.had_error);
    // The function is still recognised at the top level (the parser
    // didn't infinite-loop and gave up — it did finish).
    assert(r.fn_count == 1);
}

// ─────────────────────────────────────────────────────────────────────
// Test 2: garbled bracket contents in one function don't prevent
// later functions from being parsed.
// ─────────────────────────────────────────────────────────────────────

TEST(garbled_brackets_still_parse_other_functions) {
    auto r = parse_with_timeout(
        "fn main() {\n"
        "    let a = tensor([1, 2, 3]);\n"
        "    let b = tensor([1, while, 3]);\n"
        "}\n"
        "fn other() {\n"
        "    let x = 7;\n"
        "}\n");
    assert(r.had_error);
    assert(r.fn_count == 2);  // both `main` and `other` parsed
}

// ─────────────────────────────────────────────────────────────────────
// Test 3: well-formed tensor literal still parses cleanly.
// ─────────────────────────────────────────────────────────────────────

TEST(well_formed_tensor_literal_parses_clean) {
    auto r = parse_with_timeout(
        "fn main() {\n"
        "    let a = tensor([1.0, 2.0, 3.0]);\n"
        "}\n");
    assert(!r.had_error);
    assert(r.fn_count == 1);
}

// ─────────────────────────────────────────────────────────────────────
// Test 4: spec 005's specific repro (negative literals) — already
// fixed in spec 005, regression-guarded here.
// ─────────────────────────────────────────────────────────────────────

TEST(negative_literals_parse_clean_regression) {
    auto r = parse_with_timeout(
        "fn main() {\n"
        "    let a = tensor([-1.0, 0.0, 2.0, -3.0]);\n"
        "}\n");
    assert(!r.had_error);
    assert(r.fn_count == 1);
}

int main() {
    std::cout << "=== Spec 007 — parser error-recovery ===\n";
    return run_all_tests();
}
