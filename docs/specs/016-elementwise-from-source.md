# Spec 016: Remaining elementwise ops from source

**Status:** Implemented
**Depends on:** spec 005 (unary tensor-op dispatch + interpreter unary block)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler
**Roadmap:** Phase 0, item 016.

---

## 1. Goal

Make `exp`, `log`, `sqrt`, `tanh`, `sigmoid` callable from `.zero` source. These are the activation/math toolkit; `exp` specifically is needed for softmax in the MLP capstone. The runtime already provides all of them (`zero::ops::exp/log/sqrt/tanh/sigmoid`, each `Status (const Tensor&, Tensor&, Stream* = nullptr)`); this spec only wires the compiler — the most mechanical of the Phase-0 items, identical in shape to how `relu` was wired in spec 005.

## 2. Invariants

- Five new IR opcodes appended to `OpCode`: `TENSOR_EXP`, `TENSOR_LOG`, `TENSOR_SQRT`, `TENSOR_TANH`, `TENSOR_SIGMOID`. `opcode_name` returns `tensor.exp` / `tensor.log` / `tensor.sqrt` / `tensor.tanh` / `tensor.sigmoid`.
- `IRBuilder` gains `tensor_exp`, `tensor_log`, `tensor_sqrt`, `tensor_tanh`, `tensor_sigmoid`, each emitting via the existing `tensor_unary` helper (one line each).
- Sema registers `exp`, `log`, `sqrt`, `tanh`, `sigmoid` as tensor-returning builtins (joining `relu`, `sum`, `mean`, `argmax`, `matmul`).
- Lowering: a `CallExpr` whose callee is one of these names with exactly one tensor-typed argument lowers to the matching opcode (alongside the existing `relu` dispatch). Output shape = input shape (the runtime's shape-preserving contract).
- Interpreter: the existing unary block (currently `TENSOR_NEG` / `TENSOR_RELU`) is extended with the five new opcodes, each mapping to its `zero::ops::*` call, with the same `Status` → `throw_with_span` error path and `alloc_output_like` output allocation.
- Scalar paths and all prior behaviour are unchanged.
- All 23 pre-existing test binaries pass unchanged.

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — five opcodes + five `opcode_name` cases.
2. `include/ir/builder.hpp` — five `tensor_*` methods via `tensor_unary`.
3. `src/sema/sema.cpp` — add the five names to the tensor-returning builtin registration loop.
4. `src/ir/lowering.cpp` — extend the `CallExpr` unary-op dispatch with the five names.
5. `src/backend/interpreter.cpp` — add the five opcodes to the unary block's `case` labels and its inner `switch` (mapping to `zero::ops::exp/log/sqrt/tanh/sigmoid`).

Files added:

6. `tests/test_elementwise.cpp` — spec-016 unit tests.

Files modified (tests):

7. `tests/test_phase0_e2e.cpp` — extend with a softmax-shaped numerator step (`exp` over a vector, checked element-wise), prefiguring the spec-019 softmax.

No interpreter structural change (the unary block already exists), no runtime change.

### Interpreter dispatch (extended switch)

```cpp
case OpCode::TENSOR_EXP:     s = zero::ops::exp(*x, *out);     op_name = "tensor_exp";     break;
case OpCode::TENSOR_LOG:     s = zero::ops::log(*x, *out);     op_name = "tensor_log";     break;
case OpCode::TENSOR_SQRT:    s = zero::ops::sqrt(*x, *out);    op_name = "tensor_sqrt";    break;
case OpCode::TENSOR_TANH:    s = zero::ops::tanh(*x, *out);    op_name = "tensor_tanh";    break;
case OpCode::TENSOR_SIGMOID: s = zero::ops::sigmoid(*x, *out); op_name = "tensor_sigmoid"; break;
```

## 4. Acceptance tests

### `tests/test_elementwise.cpp` (unit)

1. `exp(tensor([0.0, 1.0]))` → `{1.0, e}` (ε-close; `e ≈ 2.718281828`).
2. `log(tensor([1.0, 2.718281828]))` → `{0.0, 1.0}` (ε-close).
3. `sqrt(tensor([4.0, 9.0]))` → `{2.0, 3.0}`.
4. `sigmoid(tensor([0.0]))` → `{0.5}`.
5. `tanh(tensor([0.0]))` → `{0.0}`.
6. Shape preserved: `exp(tensor([[1,2],[3,4]]))` → `ndim==2`, `shape=={2,2}`.
7. Scalar paths unchanged: a scalar-only program lowers to no `TENSOR_*` opcodes.

### `tests/test_phase0_e2e.cpp` (integration, extended)

8. The softmax numerator through a user function: `exp` of `[1, 2, 3]` captured and checked element-wise against `{e^1, e^2, e^3}` (ε-close). (The full softmax — dividing by `sum` — waits on spec 017's broadcast and spec 019.)

All 23 pre-existing test binaries pass unchanged. Float comparisons use an epsilon bound.

## 5. Out of scope

- **`abs`, `sin`, `cos`.** The runtime's `ElementwiseOp` supports them, but they aren't needed for the capstone and aren't wired here. Trivial follow-ups if ever needed.
- **Numerical-stability variants** (e.g. stable softmax's `exp(x - max)`). That composition is written in Zero in spec 019; this spec only exposes raw `exp`.
- **Tensor–scalar broadcast** (`exp(x) / s`). Spec 017.
- **Domain checks** (`log` of negatives, `sqrt` of negatives). The runtime computes `std::log`/`std::sqrt` as-is (NaN for invalid input); no compiler-side guard. Out of scope.
- **Sema type/shape checking beyond "arg is a tensor."** Spec 018.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) wire exactly the five the capstone path needs (`exp`/`log`/`sqrt`/`tanh`/`sigmoid`); `abs`/`sin`/`cos` deferred as trivial follow-ups; (b) reuse the spec-005 unary block and `tensor_unary` helper verbatim — no structural change; (c) extend the Phase-0 e2e with the softmax numerator now, deferring the divide-by-sum to specs 017/019.
- *Implementation, verification* — Landed first try (the predicted "most mechanical" item — five enum values, five one-line builder methods, five names in the sema loop, five lowering branches, five interpreter switch arms). `ctest` **24/24** (`ZeroElementwiseTest` + the extended `test_phase0_e2e` softmax-numerator case). All 23 prior binaries unchanged. No surprises.
