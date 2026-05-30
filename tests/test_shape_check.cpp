/**
 * @file test_shape_check.cpp
 * @brief Acceptance tests for spec 018 — static (literal-derived) shape
 *        checking in sema.
 */

#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>
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
        std::cout << "  Running " << t.name << "... " << std::flush;
        try { t.func(); std::cout << "\033[32mPASS\033[0m\n"; ++passed; }
        catch (const std::exception& e) { std::cout << "\033[31mFAIL\033[0m: " << e.what() << "\n"; ++failed; }
        catch (...) { std::cout << "\033[31mFAIL\033[0m: unknown\n"; ++failed; }
    }
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

// Parse + analyze; return whether sema reported any error.
static bool sema_has_error(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("test.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());   // these programs are syntactically valid
    zero::sema::Sema sema;
    sema.analyze(prog);
    return sema.had_error();
}

// ─────────────────────────────────────────────────────────────────────

TEST(elementwise_mismatch_literals) {
    assert(sema_has_error("fn main() { let c = tensor([1,2]) + tensor([1,2,3]); }"));
}

TEST(elementwise_mismatch_via_variables) {
    assert(sema_has_error(
        "fn main() { let a = tensor([1,2]); let b = tensor([1,2,3]); let c = a + b; }"));
}

TEST(matmul_inner_dim_mismatch) {
    // [1,3] x [1,2] : inner 3 != 1
    assert(sema_has_error("fn main() { let c = matmul(tensor([[1,2,3]]), tensor([[1,2]])); }"));
}

TEST(valid_elementwise_no_error) {
    assert(!sema_has_error("fn main() { let c = tensor([1,2,3]) + tensor([4,5,6]); }"));
}

TEST(valid_matmul_no_error) {
    assert(!sema_has_error(
        "fn main() { let c = matmul(tensor([[1,2,3],[4,5,6]]), tensor([[1,2],[3,4],[5,6]])); }"));
}

TEST(broadcast_not_flagged) {
    assert(!sema_has_error(
        "fn main() { let c = tensor([1,2,3]) / sum(tensor([1,1,1])); }"));
}

TEST(scalar_broadcast_not_flagged) {
    assert(!sema_has_error("fn main() { let c = tensor([1,2,3]) * 2.0; }"));
}

TEST(unknown_param_shape_not_flagged) {
    assert(!sema_has_error(
        "fn f(x: tensor) -> tensor { return x + x; }\n"
        "fn main() { let r = f(tensor([1,2,3])); }\n"));
}

TEST(shape_preserving_propagation_flags_mismatch) {
    // b = exp(a) is [3]; b + [2] mismatches.
    assert(sema_has_error(
        "fn main() { let a = tensor([1,2,3]); let b = exp(a); let c = b + tensor([1,2]); }"));
}

TEST(reduction_result_broadcasts) {
    assert(!sema_has_error(
        "fn main() { let a = tensor([1,2,3]); let s = sum(a); let r = s + tensor([9]); }"));
    assert(!sema_has_error(
        "fn main() { let a = tensor([1,2,3]); let s = sum(a); let r = s + tensor([1,2,3]); }"));
}

int main() {
    std::cout << "=== Spec 018 — static shape checking ===\n";
    return run_all_tests();
}
