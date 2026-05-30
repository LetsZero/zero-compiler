# Zero Compiler — Phase 0 Roadmap

> **The one file to read for finishing Phase 0.** Self-contained: current state, the
> definition of "done", the ordered spec sequence, and the testing plan. Other docs
> (`COMPILER_OVERVIEW.md`, `CURRENT_STATE.md`, `DEFERRED.md`, the runtime's specs) are
> optional depth — you should not need them to execute this plan.

---

## 0. How to use this file

- Work the specs in **section 5** top to bottom. Each is one spec under `docs/specs/NNN-*.md`, built with the standard workflow (spec → tests-from-spec → implement → verify → commit).
- Tick the checklist in **section 6** as each lands.
- When the capstone (spec 019) is green on CI, **Phase 0 is complete** — stop and re-plan for Phase 1 (autograd/LLVM), which is deliberately *not* in this file.

---

## 1. Where we are now (compact recap)

**Runtime** (`external/core-runtime`, v1.4.0, FROZEN): tensors/scalars/structs, Status-returning ops, streams, RNG state, contiguity contract. Ops available: elementwise (add/sub/mul/div/neg/relu/sigmoid/tanh/exp/log/sqrt/…), matmul, reductions (sum/max/mean/argmax), reshape/views, gather/scatter, contiguous. F32-on-CPU in practice. **Done; do not modify from this repo.**

**Compiler** (this repo): lexer → parser → sema → lower → ZIR → tree-walking interpreter. Working from source today: scalar math, `if`/`while`, functions with tensor params/returns, **1-D** F32 tensor literals, and `+ - * / -unary relu` on tensors. Source-spanned IR, Reporter-based runtime diagnostics, CI green on Linux + macOS (20 test binaries). All known bugs drained (specs 007–013).

**The gap to Phase 0 done:** the language can't yet express a model's forward pass — no reductions, no 2-D tensors, no `exp`/`log`/etc. from source, no tensor-scalar broadcast.

---

## 2. Definition of "Phase 0 complete" (falsifiable)

> **Phase 0 is complete when a 2-layer MLP with a softmax output can be written in `.zero`
> source and its forward pass runs correctly through the interpreter**, verified by an
> automated end-to-end test and green on CI (Linux + macOS).

Concretely, this `.zero` program (or equivalent) compiles and produces the correct numbers:

```zero
fn softmax(x: tensor) -> tensor {
    let e = exp(x);            # needs spec 016 (exp from source)
    let s = sum(e);            # needs spec 014 (reductions)
    return e / s;              # needs spec 017 (tensor / scalar broadcast)
}

fn mlp(x: tensor, w1: tensor, w2: tensor) -> tensor {
    let h = relu(matmul(x, w1));   # needs spec 015 (2-D literals + source matmul)
    let o = matmul(h, w2);
    return softmax(o);
}

fn main() {
    # 2-D weights need spec 015; this is the demo that closes Phase 0.
    let x  = tensor([[1.0, 2.0]]);          # [1, 2]
    let w1 = tensor([[0.1, 0.2, 0.3],
                     [0.4, 0.5, 0.6]]);     # [2, 3]
    let w2 = tensor([[0.1, 0.2],
                     [0.3, 0.4],
                     [0.5, 0.6]]);          # [3, 2]
    capture(mlp(x, w1, w2));
}
```

This single program exercises every Phase-0 gap. When it runs and the output matches a hand-computed reference, Phase 0 is done.

---

## 3. Explicitly OUT of scope for Phase 0

Deferred to Phase 1+ — do **not** pull these in while finishing Phase 0:

- **Autograd / training.** Forward pass only. (`NEXT_STEPS.md` warning: don't make autograd a compiler pass first — many projects die there. We finish the forward-pass interpreter first.)
- **LLVM / native codegen.** Interpreter only.
- **GPU / MLIR.**
- **Multi-dtype compute** (fp8/bf16 actually computing). F32 only.
- **gather/scatter, Generator/RNG, optimizers, full stdlib.** Not needed for an MLP forward pass.
- The `zero::ir::` rename and gtest migration (see `DEFERRED.md` — "won't do unless needed").

---

## 4. Guiding constraints (inherited)

- Spec-driven: every item below is one approved spec before any code.
- Erosion rule: ML semantics (softmax, layernorm) are written in Zero/stdlib, **lowered** to runtime primitives — never added to the runtime or invented as IR ops.
- Tests written from the spec, before implementation. Out-of-scope sections are binding.

---

## 5. The plan — ordered spec sequence

Each row is one spec. Sizes: S ≈ half-day mechanical, M ≈ 1–2 days with new logic.

### Spec 014 — Reductions from source  ·  S
- **Goal:** `sum(t)`, `mean(t)`, `argmax(t)` callable from source, lowering to the runtime's reduce ops. New IR opcodes `TENSOR_SUM`/`TENSOR_MEAN`/`TENSOR_ARGMAX` (or one `TENSOR_REDUCE` with a mode), interpreter wiring, sema builtins.
- **New shape semantics:** output rank = input rank − 1 (or scalar for full reduction). This is the first op family where output shape ≠ input shape — the spec must pin down whether `sum` is full-reduction (→ scalar tensor) or last-axis. **Recommendation:** full-reduction to a 1-element tensor for v1 (simplest, and what softmax needs).
- **Depends on:** nothing new.
- **Test:** `sum(tensor([1,2,3,4])) == 10`, `mean == 2.5`, `argmax == 3`; mixed in an expression.

### Spec 015 — N-D tensor literals + source `matmul`  ·  S–M
- **Goal:** nested-bracket literals `tensor([[1,2],[3,4]])` (parser recurses, infers shape, rejects ragged); a source-callable `matmul(a, b)` builtin dispatching to the already-wired `TENSOR_MATMUL`.
- **Depends on:** the existing matmul IR/interpreter path (done in spec 005).
- **Test:** 2×3 literal has shape `[2,3]` and row-major bytes; `matmul` of `[2,3]`×`[3,2]` → `[2,2]` with correct values; ragged literal is a parse error (and does not hang — spec 007 recovery covers it).

### Spec 016 — Remaining elementwise from source  ·  S
- **Goal:** `exp`, `log`, `sqrt`, `tanh`, `sigmoid` (and `abs`) callable from source. Add IR opcodes + interpreter arms (runtime already supports them via `ElementwiseOp`), register as sema builtins.
- **Depends on:** the unary-op dispatch pattern (spec 005's `relu`/`neg`).
- **Test:** `exp(tensor([0,1])) ≈ {1, e}`; `sigmoid(tensor([0])) == 0.5`; shape preserved.

### Spec 017 — Tensor–scalar broadcast  ·  S
- **Goal:** `t / s` and `t * s` etc. where one side is a scalar (rank-0 / 1-element). Lowering constructs a 1-element tensor from the scalar and uses the runtime's `b.numel()==1` broadcast path, OR adds a dedicated scalar-op lowering. **Recommendation:** lower the scalar operand to a `tensor([s])` and reuse the existing binary path — least new machinery.
- **Depends on:** 014 (softmax's `e / s` divides a vector by a 1-element sum).
- **Test:** `tensor([2,4,6]) / sum(tensor([1,1,1])) == {0.66.., 1.33.., 2.0}`; `tensor([1,2,3]) * 2.0 == {2,4,6}`.

### Spec 018 — Sema shape-checking  ·  M
- **Goal:** reject statically-knowable shape mismatches at compile time with a Reporter diagnostic, instead of only at runtime. E.g. `tensor([1,2]) + tensor([1,2,3])`, or `matmul` with non-conforming inner dims, caught in sema. Requires sema to track tensor shapes through `let`/exprs where they're literal-derived.
- **Depends on:** 014–017 (so the checker covers the ops the demo uses).
- **Scope guard:** only *statically-known* shapes (literal-derived). Dynamic/unknown shapes pass through to the runtime check as today. Don't build a full shape-inference engine — just propagate known literal shapes.
- **Test:** mismatched add/matmul from literals → sema error with span; the valid MLP program → no sema error.

### Spec 019 — Stdlib-in-Zero `softmax` + the MLP capstone  ·  M
- **Goal:** write `softmax` (and optionally `layernorm`) as a `.zero` function — proving the erosion model (ML semantics compose from primitives in Zero, not in the runtime). Then the full MLP forward-pass program from section 2 runs end-to-end.
- **Depends on:** 014, 015, 016, 017 (018 strongly recommended first).
- **Test:** **the Phase-0 capstone** — the MLP program runs and `capture` yields the hand-computed reference output, bit-close (ULP/epsilon bounded). This is the falsifiable "Phase 0 complete" signal.

---

## 6. Progress checklist

- [x] **014** Reductions from source (`sum`/`mean`/`argmax`) — done, ctest 22/22
- [x] **015** N-D tensor literals + source `matmul` — done (1-D/2-D), ctest 23/23
- [x] **016** Remaining elementwise from source (`exp`/`log`/`sqrt`/`tanh`/`sigmoid`) — done, ctest 24/24
- [x] **017** Tensor–scalar broadcast — done, ctest 25/25 (softmax's `e/sum(e)` already worked via tensor÷tensor[1]; 017 added `x * 2.0`-style scalar broadcast)
- [ ] **018** Sema shape-checking (static, literal-derived)
- [ ] **019** Stdlib `softmax` + MLP forward-pass capstone  → **Phase 0 COMPLETE**

---

## 7. Testing plan (the "small testing" ask)

Two layers, both automated and CI-gated (Linux + macOS):

1. **Per-spec unit tests** — every spec ships its own `tests/test_*.cpp` written from the spec's acceptance section (the existing pattern). Each new op/feature gets ok-path + error-path + a regression guard.
2. **One growing end-to-end test, `tests/test_phase0_e2e.cpp`** — added in spec 014 and extended by each subsequent spec, culminating in the full MLP program in spec 019. It compiles real `.zero` source through the whole pipeline and checks `capture`d numeric results against hand-computed references. This is the integration safety net that proves the pieces compose, not just that each works alone.

No new test framework — the existing custom `TEST()` macro + `capture` external is sufficient. Numeric comparisons use an epsilon bound (floats).

---

## 8. After Phase 0 (pointer only — not planned here)

Once the capstone is green, the next mountains each get their own roadmap when reached:
- **Phase 1 — Autograd** (runtime-tape, forward-pass first → backward), then **LLVM CPU codegen**.
- **Phase 3 — MLIR / GPU.**

Do not start these while Phase 0 is open. This file ends at "Phase 0 complete."
