# Spec 019: Stdlib `softmax` + MLP forward-pass capstone (Phase 0 complete)

**Status:** Implemented
**Depends on:** specs 014–018b (every Phase-0 primitive)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler
**Roadmap:** Phase 0, item 019 — **the milestone that marks Phase 0 complete.**

---

## 1. Goal

Run a complete **2-layer MLP forward pass with a softmax output, written in `.zero` source**, end-to-end through the interpreter, and verify the result against an independent numeric oracle. This is the falsifiable definition of "Phase 0 complete" from `docs/ROADMAP.md §2`.

`softmax` is written **in Zero**, composed from `exp` / `sum` / `/` — proving the erosion thesis: ML semantics live in the language, not the runtime. The runtime only ever sees its frozen primitives.

The reference program (verified working during spec authoring — output matches a C++ oracle to 6 decimals):

```zero
fn softmax(x: tensor) -> tensor {
    return exp(x) / sum(exp(x));
}

fn mlp(x: tensor, w1: tensor, w2: tensor) -> tensor {
    let h = relu(matmul(x, w1));
    let o = matmul(h, w2);
    return softmax(o);
}

fn main() {
    let x  = tensor([[1.0, 2.0]]);
    let w1 = tensor([[0.1, 0.2, 0.3],
                     [0.4, 0.5, 0.6]]);
    let w2 = tensor([[0.1, 0.2],
                     [0.3, 0.4],
                     [0.5, 0.6]]);
    capture(mlp(x, w1, w2));
}
```

Expected output: `[1, 2]` tensor `{0.410960, 0.589040}` (sums to 1.0).

## 2. Invariants

- The reference program above compiles (no parse error, no sema error) and executes through the interpreter, producing a `[1, 2]` F32 tensor whose values match an **independent in-test C++ oracle** (recompute relu/matmul/softmax in plain C++) to within `1e-5`, and which sums to `1.0` within `1e-5`.
- `softmax` is defined in `.zero` source (composed from `exp`, `sum`, `/`) — **no `softmax` primitive exists in the runtime or as an IR opcode**. (Grep guard: the compiler has no `TENSOR_SOFTMAX`.)
- `mlp` exercises: a user function with three tensor parameters, nested user-function calls (`mlp` → `softmax`), `matmul`, `relu`, and multi-line 2-D weight literals.
- The capstone runs with **no new compiler code** — it is composition of specs 014–018b. (If implementation reveals a gap, that is a finding to address; none is expected, as the program was verified during authoring.)
- A canonical artifact `stdlib/nn.zero` contains the `softmax` definition (and is referenced by the spec), documenting the intended stdlib even though a module/import mechanism is not yet built (post-Phase-0).
- `docs/ROADMAP.md` is updated to mark Phase 0 **COMPLETE**.
- All 27 pre-existing test binaries pass unchanged; the capstone is added to `tests/test_phase0_e2e.cpp`.

## 3. API surface

Files added:

1. `stdlib/nn.zero` — the canonical `softmax` (and a comment noting it is not yet importable; for now programs define it inline). Documentation/artifact, not compiled by any test.

Files modified:

2. `tests/test_phase0_e2e.cpp` — add `e2e_mlp_capstone`: compile + run the reference program, assert the output matches an in-test C++ oracle and sums to 1.0. Also assert `!sema.had_error()` and `!parser.had_error()`.
3. `docs/ROADMAP.md` — check off item 019 and add a "Phase 0 COMPLETE" marker with the date/commit.

No source code under `src/`, `include/`, or `runtime/` changes. No new test binary (extends the existing Phase-0 e2e net, which is the right home for the capstone).

### Oracle (in-test)

```cpp
// Independent recomputation — must match the interpreter's output.
float h0 = 0.9f, h1 = 1.2f, h2 = 1.5f;             // relu(x · w1), all > 0
float o0 = h0*0.1f + h1*0.3f + h2*0.5f;            // = 1.20
float o1 = h0*0.2f + h1*0.4f + h2*0.6f;            // = 1.56
float e0 = std::exp(o0), e1 = std::exp(o1), s = e0 + e1;
float ref0 = e0/s, ref1 = e1/s;                    // ≈ {0.41096, 0.58904}
```

## 4. Acceptance tests

### `tests/test_phase0_e2e.cpp` — `e2e_mlp_capstone` (THE Phase-0 milestone)

1. Compile the reference program; assert `!parser.had_error()` and `!sema.had_error()`.
2. Execute; assert `capture` received a `[1, 2]` F32 tensor.
3. Assert `out[0] ≈ ref0` and `out[1] ≈ ref1` (in-test oracle, `1e-5`).
4. Assert `out[0] + out[1] ≈ 1.0` (`1e-5`) — softmax is a probability distribution.

### Grep guard (in the same test file or a comment-documented assertion)

5. The build contains no `TENSOR_SOFTMAX` opcode — softmax is pure Zero composition. (Enforced by its absence in `OpCode`; no test asserts a negative, but the spec records it.)

All 27 pre-existing test binaries pass unchanged.

## 5. Out of scope

- **A module / import system.** `stdlib/nn.zero` is a documentation artifact; programs still define `softmax` inline. Real imports are post-Phase-0.
- **Training / autograd / a backward pass.** Forward pass only — this is the Phase-0 boundary.
- **Numerically-stable softmax** (`exp(x - max(x))`). The plain form is correct for these inputs; stability is a stdlib refinement, not a Phase-0 requirement.
- **Batched / multi-row input.** `x` is a single `[1, 2]` row. Batched MLP (a `[N, 2]` input with per-row softmax) needs last-axis reductions (deferred — spec 014 chose full reduction).
- **Bias terms, more layers, other activations.** The 2-layer relu→linear→softmax is sufficient to mark Phase 0; richer nets are Phase-1+ material.
- **Performance.** Interpreter speed is irrelevant to the milestone.

## 6. Open questions

(none — all resolved; the program was verified end-to-end during spec authoring)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) the capstone is added to the existing `test_phase0_e2e.cpp` net rather than a new binary — it is the culmination that net was built for; (b) the numeric check uses an **independent in-test C++ oracle** (recomputing relu/matmul/softmax) rather than hard-coded constants, so the assertion can't drift from a typo; (c) `softmax` is defined inline in the program (no import system yet) with a canonical `stdlib/nn.zero` artifact for documentation; (d) full-reduction softmax over the single `[1,2]` row is correct — batched per-row softmax is explicitly deferred (needs last-axis reductions).
- *Implementation, verification — PHASE 0 COMPLETE.* `e2e_mlp_capstone` passes: the MLP forward pass produces `{0.410960, 0.589040}` (sums to 1.0), matching the in-test C++ oracle to within 1e-5; parse and sema are clean on the multi-line source. **No new compiler code was needed** — pure composition of specs 014–018b, exactly as predicted. `softmax` is Zero source (no `TENSOR_SOFTMAX` opcode exists). `stdlib/nn.zero` artifact added. `ctest` **27/27**. This is the falsifiable Phase-0 milestone from `ROADMAP §2`; Phase 0 is done.
