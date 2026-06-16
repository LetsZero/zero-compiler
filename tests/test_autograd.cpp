/**
 * @file test_autograd.cpp
 * @brief Reverse-mode autograd written in Zero — gradient correctness.
 *
 * The tape/Var machinery (examples/autograd_mlp.zero) is embedded here as a
 * library string; each test appends a `main` that builds a tiny graph, runs
 * backward, and asserts the resulting gradients against hand-derived values.
 * This pins the backward dispatch for every rule (mul, sum, sub, matmul, relu)
 * plus the end-to-end convergence property, with no hand-written backward pass.
 */

#include "parser/parser.hpp"
#include "sema/sema.hpp"
#include "ir/lowering.hpp"
#include "backend/interpreter.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>

using namespace zero;
using namespace zero::parser;
using namespace zero::source;
using namespace zero::backend;

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

static void run_ok(const std::string& src) {
    SourceManager sm;
    SourceID sid = sm.load_from_string("t.zero", src);
    Parser parser(sm, sid);
    auto prog = parser.parse();
    assert(!parser.had_error());
    sema::Sema sema; sema.analyze(prog);
    assert(!sema.had_error());
    ir::Lowering low; auto mod = low.lower(prog);
    Interpreter interp;
    interp.register_external("capture", [](const std::vector<RuntimeValue>& a){
        if (!a.empty()) captured = a[0]; return RuntimeValue{}; });
    captured = RuntimeValue{};
    interp.execute(mod);
}

static const float* cap_t() { assert(captured.is_tensor()); return static_cast<const float*>(captured.as_tensor()->data); }
static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// The autograd library, in Zero (mirrors examples/autograd_mlp.zero, sans main).
static const char* LIB = R"Z(
struct Tape {
    vals: tensorarray, grads: tensorarray,
    op: tensor, ia: tensor, ib: tensor, out: tensor,
    nvars: tensor, nops: tensor
}
fn make_tape() -> Tape {
    return Tape(tensorarray(16), tensorarray(16),
                tensor([0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]),
                tensor([0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]),
                tensor([0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]),
                tensor([0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]),
                tensor([0.0]), tensor([0.0]))
}
fn new_var(t: Tape, value: tensor) -> float {
    let h = t.nvars[0]
    ta_set(t.vals, h, value)
    ta_set(t.grads, h, value * 0.0)
    t.nvars[0] = h + 1.0
    return h
}
fn record_op(t: Tape, tag: float, a: float, b: float, out: float) {
    let k = t.nops[0]
    t.op[k] = tag
    t.ia[k] = a
    t.ib[k] = b
    t.out[k] = out
    t.nops[0] = k + 1.0
}
fn accum(t: Tape, h: float, g: tensor) {
    ta_set(t.grads, h, ta_get(t.grads, h) + g)
}
fn ad_matmul(t: Tape, ha: float, hb: float) -> float {
    let out = new_var(t, matmul(ta_get(t.vals, ha), ta_get(t.vals, hb)))
    record_op(t, 1.0, ha, hb, out)
    return out
}
fn ad_relu(t: Tape, ha: float) -> float {
    let out = new_var(t, relu(ta_get(t.vals, ha)))
    record_op(t, 2.0, ha, 0.0, out)
    return out
}
fn ad_sub(t: Tape, ha: float, hb: float) -> float {
    let out = new_var(t, ta_get(t.vals, ha) - ta_get(t.vals, hb))
    record_op(t, 3.0, ha, hb, out)
    return out
}
fn ad_mul(t: Tape, ha: float, hb: float) -> float {
    let out = new_var(t, ta_get(t.vals, ha) * ta_get(t.vals, hb))
    record_op(t, 4.0, ha, hb, out)
    return out
}
fn ad_sum(t: Tape, ha: float) -> float {
    let out = new_var(t, sum(ta_get(t.vals, ha)))
    record_op(t, 5.0, ha, 0.0, out)
    return out
}
fn backward(t: Tape, hloss: float) {
    ta_set(t.grads, hloss, tensor([1.0]))
    let k = t.nops[0] - 1.0
    while k >= 0.0 {
        let tag = t.op[k]
        let ah = t.ia[k]
        let bh = t.ib[k]
        let oh = t.out[k]
        let go = ta_get(t.grads, oh)
        if tag == 1.0 {
            let va = ta_get(t.vals, ah)
            let vb = ta_get(t.vals, bh)
            accum(t, ah, matmul(go, transpose(vb)))
            accum(t, bh, matmul(transpose(va), go))
        } else { if tag == 2.0 {
            accum(t, ah, go * step(ta_get(t.vals, ah)))
        } else { if tag == 3.0 {
            accum(t, ah, go)
            accum(t, bh, -go)
        } else { if tag == 4.0 {
            let va = ta_get(t.vals, ah)
            let vb = ta_get(t.vals, bh)
            accum(t, ah, go * vb)
            accum(t, bh, go * va)
        } else { if tag == 5.0 {
            let va = ta_get(t.vals, ah)
            accum(t, ah, va * 0.0 + go[0])
        } } } } }
        k = k - 1.0
    }
}
)Z";

static std::string prog(const std::string& main) { return std::string(LIB) + main; }

// ── per-rule gradient checks ────────────────────────────────────────────────

TEST(grad_of_sum_of_square) {
    // f = sum(a*a);  df/da = 2a.  a = [3,4] -> grad [6,8]. (mul + sum backward)
    run_ok(prog(
        "fn main() {\n"
        "  let t = make_tape()\n"
        "  let ha = new_var(t, tensor([3.0, 4.0]))\n"
        "  let hsq = ad_mul(t, ha, ha)\n"
        "  let hl = ad_sum(t, hsq)\n"
        "  backward(t, hl)\n"
        "  capture(ta_get(t.grads, ha))\n"
        "}"));
    assert(near(cap_t()[0], 6) && near(cap_t()[1], 8));
}

TEST(grad_of_matmul_weight) {
    // f = sum(x @ W);  grad_W = x^T @ ones.  x=[[1,2]], W=[[1],[1]] -> [[1],[2]].
    run_ok(prog(
        "fn main() {\n"
        "  let t = make_tape()\n"
        "  let hx = new_var(t, tensor([[1.0, 2.0]]))\n"
        "  let hw = new_var(t, tensor([[1.0], [1.0]]))\n"
        "  let ho = ad_matmul(t, hx, hw)\n"
        "  let hl = ad_sum(t, ho)\n"
        "  backward(t, hl)\n"
        "  capture(ta_get(t.grads, hw))\n"
        "}"));
    assert(near(cap_t()[0], 1) && near(cap_t()[1], 2));
}

TEST(grad_through_relu_gates_dead_units) {
    // f = sum(relu(a));  grad_a = step(a).  a=[[-1, 2]] -> [0, 1].
    run_ok(prog(
        "fn main() {\n"
        "  let t = make_tape()\n"
        "  let ha = new_var(t, tensor([[-1.0, 2.0]]))\n"
        "  let hr = ad_relu(t, ha)\n"
        "  let hl = ad_sum(t, hr)\n"
        "  backward(t, hl)\n"
        "  capture(ta_get(t.grads, ha))\n"
        "}"));
    assert(near(cap_t()[0], 0) && near(cap_t()[1], 1));
}

TEST(grad_of_sub) {
    // f = sum(a - b);  grad_a = +1, grad_b = -1 (elementwise).
    run_ok(prog(
        "fn main() {\n"
        "  let t = make_tape()\n"
        "  let ha = new_var(t, tensor([5.0, 5.0]))\n"
        "  let hb = new_var(t, tensor([2.0, 2.0]))\n"
        "  let hd = ad_sub(t, ha, hb)\n"
        "  let hl = ad_sum(t, hd)\n"
        "  backward(t, hl)\n"
        "  capture(ta_get(t.grads, hb))\n"
        "}"));
    assert(near(cap_t()[0], -1) && near(cap_t()[1], -1));
}

// ── end-to-end: the 2-layer MLP converges with autograd-produced grads ──────

TEST(mlp_converges_to_target) {
    run_ok(prog(
        "fn main() {\n"
        "  let x = tensor([[1.0, 2.0]])\n"
        "  let target = tensor([[1.0]])\n"
        "  let w1 = tensor([[0.1, -0.5], [0.3, -0.5]])\n"
        "  let w2 = tensor([[0.1], [0.2]])\n"
        "  let lr = 0.02\n"
        "  let steps = 800\n"
        "  while steps > 0 {\n"
        "    let t = make_tape()\n"
        "    let hx = new_var(t, x)\n"
        "    let htg = new_var(t, target)\n"
        "    let hw1 = new_var(t, w1)\n"
        "    let hw2 = new_var(t, w2)\n"
        "    let hpre = ad_matmul(t, hx, hw1)\n"
        "    let hh = ad_relu(t, hpre)\n"
        "    let ho = ad_matmul(t, hh, hw2)\n"
        "    let hdiff = ad_sub(t, ho, htg)\n"
        "    let hsq = ad_mul(t, hdiff, hdiff)\n"
        "    let hloss = ad_sum(t, hsq)\n"
        "    backward(t, hloss)\n"
        "    w1 = w1 - lr * ta_get(t.grads, hw1)\n"
        "    w2 = w2 - lr * ta_get(t.grads, hw2)\n"
        "    steps = steps - 1\n"
        "  }\n"
        "  capture(matmul(relu(matmul(x, w1)), w2))\n"
        "}"));
    assert(near(cap_t()[0], 1.0, 1e-2f));   // pred -> target 1.0
}

int main() {
    std::cout << "=== Autograd (reverse-mode, in Zero) ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};
    return rc;
}
