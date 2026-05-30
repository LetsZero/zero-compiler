/**
 * @file test_runtime_diagnostics.cpp
 * @brief Acceptance tests for spec 010 — Reporter integration for runtime
 *        errors.
 *
 * Verifies that when a SourceManager is set, a failing tensor op emits a
 * "Frame & Focus" RUNTIME diagnostic to stderr (in addition to throwing),
 * and that when no SourceManager is set the behaviour is throw-only.
 */

#include "ir/ir.hpp"
#include "ir/lowering.hpp"
#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "source/source.hpp"
#include "backend/interpreter.hpp"

#include <iostream>
#include <sstream>
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

// RAII capture of std::cerr into a stringstream.
struct CerrCapture {
    std::stringstream buf;
    std::streambuf* old;
    CerrCapture()  : old(std::cerr.rdbuf(buf.rdbuf())) {}
    ~CerrCapture() { std::cerr.rdbuf(old); }
    std::string str() const { return buf.str(); }
};

static const char* kShapeMismatchSrc =
    "fn main() {\n"
    "    let a = tensor([1, 2, 3, 4]);\n"
    "    let b = tensor([1, 2, 3]);\n"
    "    let c = a - b;\n"
    "    capture(c);\n"
    "}\n";

static Module lower_src(SourceManager& sm, const char* src) {
    SourceID sid = sm.load_from_string("diag_test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    zero::sema::Sema sema; sema.analyze(prog);
    Lowering lowering;
    return lowering.lower(prog);
}

static void install_capture(Interpreter& interp) {
    interp.register_external("capture",
        [](const std::vector<RuntimeValue>&) { return RuntimeValue(); });
}

// ─────────────────────────────────────────────────────────────────────
// Test 1: with a SourceManager, the Reporter block is emitted.
// ─────────────────────────────────────────────────────────────────────

TEST(reporter_fires_when_source_manager_set) {
    SourceManager sm;
    Module mod = lower_src(sm, kShapeMismatchSrc);

    Interpreter interp;
    install_capture(interp);
    interp.set_source_manager(&sm);

    bool threw = false;
    std::string err;
    {
        CerrCapture cap;
        try {
            interp.execute(mod);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        err = cap.str();
    }

    assert(threw);                                          // control flow preserved
    assert(err.find("[ ERROR ]") != std::string::npos);     // Reporter block present
    assert(err.find("RuntimeError") != std::string::npos);  // correct category
    assert(err.find("diag_test.zero") != std::string::npos);  // filename resolved
}

// ─────────────────────────────────────────────────────────────────────
// Test 2: without a SourceManager, throw-only (no Reporter block).
// ─────────────────────────────────────────────────────────────────────

TEST(no_reporter_without_source_manager) {
    SourceManager sm;
    Module mod = lower_src(sm, kShapeMismatchSrc);

    Interpreter interp;
    install_capture(interp);
    // Deliberately do NOT call set_source_manager.

    bool threw = false;
    std::string err;
    {
        CerrCapture cap;
        try {
            interp.execute(mod);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        err = cap.str();
    }

    assert(threw);
    assert(err.find("[ ERROR ]") == std::string::npos);  // no Reporter output
}

// ─────────────────────────────────────────────────────────────────────
// Test 3: happy path — no throw, no Reporter output.
// ─────────────────────────────────────────────────────────────────────

TEST(happy_path_emits_nothing) {
    const char* ok_src =
        "fn main() {\n"
        "    let a = tensor([1, 2, 3, 4]);\n"
        "    let b = tensor([10, 10, 10, 10]);\n"
        "    capture(a + b);\n"
        "}\n";
    SourceManager sm;
    Module mod = lower_src(sm, ok_src);

    Interpreter interp;
    install_capture(interp);
    interp.set_source_manager(&sm);

    bool threw = false;
    std::string err;
    {
        CerrCapture cap;
        try {
            interp.execute(mod);
        } catch (...) {
            threw = true;
        }
        err = cap.str();
    }

    assert(!threw);
    assert(err.find("[ ERROR ]") == std::string::npos);
}

int main() {
    std::cout << "=== Spec 010 — runtime diagnostics ===\n";
    return run_all_tests();
}
