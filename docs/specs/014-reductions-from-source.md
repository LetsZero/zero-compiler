# Spec 014: Reductions from source (`sum` / `mean` / `argmax`)

**Status:** Implemented
**Depends on:** spec 005 (tensor-op dispatch pattern), spec 010 (diagnostics)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler
**Roadmap:** Phase 0, item 014.

---

## 1. Goal

Make `sum(t)`, `mean(t)`, and `argmax(t)` callable from `.zero` source. These are the first ops where **output shape ≠ input shape** — a reduction collapses a tensor to a single value. They unlock loss functions and the softmax denominator (`sum(exp(x))`), which the Phase-0 MLP capstone needs.

**v1 semantics: full reduction over all elements → a 1-element (`[1]`, F32, rank-1) tensor.**
- `sum(t)` → `[1]` tensor holding the sum of every element.
- `mean(t)` → `[1]` tensor holding the arithmetic mean of every element.
- `argmax(t)` → `[1]` tensor holding the flat index of the maximum element, **as a float** (everything in the language is an F32 tensor today; an index of 3 is stored as `3.0f`).

This is deliberately the simplest reduction semantics. Last-axis / per-axis reductions are a later concern; full reduction is what softmax needs and avoids an axis-argument design now.

## 2. Invariants

- Three new IR opcodes, appended to `OpCode`: `TENSOR_SUM`, `TENSOR_MEAN`, `TENSOR_ARGMAX`. Each is unary (one tensor operand), result type `tensor`. `opcode_name` returns `tensor.sum` / `tensor.mean` / `tensor.argmax`.
- `IRBuilder` gains `tensor_sum(Value)`, `tensor_mean(Value)`, `tensor_argmax(Value)`, each emitting via the existing private `tensor_unary(OpCode, Value)` helper.
- Sema registers `sum`, `mean`, `argmax` as variadic builtins returning `tensor` (same mechanism as `relu` in spec 005).
- Lowering: a `CallExpr` whose callee is `sum`/`mean`/`argmax` with exactly one tensor-typed argument lowers to the matching `TENSOR_*` opcode. Any other arity/type falls through to the normal `call` path (so a user function named `sum` taking non-tensors is unaffected — though none exists today).
- Interpreter: each opcode pulls its tensor operand, computes the reduction over the **flattened** tensor, allocates a fresh contiguous `[1]` F32 output tensor (caller-allocated, owns_data), writes the single result, and returns it as a `TensorPtr`.
  - `sum` / `mean`: computed via the runtime's `zero::ops::reduce_all(*t, ReduceOp::SUM | MEAN)` (returns `float`).
  - `argmax`: computed directly in the interpreter (the runtime's `reduce_all` has no argmax mode) — linear scan for the max element, store its flat index as `float`. Empty tensor (`numel()==0`) → index `0`.
- A non-tensor operand to any of the three throws `std::runtime_error` (consistent with the other tensor-op arms). No new `Status` path is needed — `reduce_all` does not return `Status`.
- All 20 pre-existing test binaries pass unchanged.

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — add `TENSOR_SUM`, `TENSOR_MEAN`, `TENSOR_ARGMAX` to `OpCode` (after the spec-005 additions); add their `opcode_name` cases.
2. `include/ir/builder.hpp` — add `tensor_sum` / `tensor_mean` / `tensor_argmax` (one line each, via `tensor_unary`).
3. `src/sema/sema.cpp` — register `sum` / `mean` / `argmax` builtins (return type `tensor`).
4. `src/ir/lowering.cpp` — in the `CallExpr` branch, dispatch `sum`/`mean`/`argmax` with one tensor arg to the new builder methods (alongside the existing `relu` dispatch).
5. `src/backend/interpreter.cpp` — add `case OpCode::TENSOR_SUM/TENSOR_MEAN/TENSOR_ARGMAX` to `exec_instruction`, plus `#include <zero/ops/reduce.hpp>`.

Files added:

6. `tests/test_reductions.cpp` — spec-014 unit tests.
7. `tests/test_phase0_e2e.cpp` — the growing Phase-0 end-to-end integration test (seeded here; extended by specs 015–019).

CMake: register both new test binaries (`zerobackend zerosema` link, matching the others).

### Sketch — interpreter

```cpp
case OpCode::TENSOR_SUM:
case OpCode::TENSOR_MEAN: {
    auto x = get_value(instr.operands[0]);
    if (!x.is_tensor()) throw std::runtime_error("reduction: non-tensor operand");
    const TensorPtr& t = x.as_tensor();
    zero::ops::ReduceOp op = (instr.op == OpCode::TENSOR_SUM)
        ? zero::ops::ReduceOp::SUM : zero::ops::ReduceOp::MEAN;
    float v = zero::ops::reduce_all(*t, op);
    int64_t one[1] = {1};
    TensorPtr out = alloc_output_like(one, 1);          // [1] F32, sentinel-filled
    static_cast<float*>(out->data)[0] = v;
    result = RuntimeValue(std::move(out));
    break;
}
case OpCode::TENSOR_ARGMAX: {
    auto x = get_value(instr.operands[0]);
    if (!x.is_tensor()) throw std::runtime_error("argmax: non-tensor operand");
    const TensorPtr& t = x.as_tensor();
    const float* d = static_cast<const float*>(t->data);
    int64_t n = t->numel(), best = 0;
    for (int64_t i = 1; i < n; ++i) if (d[i] > d[best]) best = i;
    int64_t one[1] = {1};
    TensorPtr out = alloc_output_like(one, 1);
    static_cast<float*>(out->data)[0] = static_cast<float>(best);
    result = RuntimeValue(std::move(out));
    break;
}
```

## 4. Acceptance tests

### `tests/test_reductions.cpp` (unit)

1. `sum(tensor([1,2,3,4]))` → `[1]` tensor, `data[0] == 10`.
2. `mean(tensor([1,2,3,4]))` → `data[0] == 2.5`.
3. `argmax(tensor([1,2,9,4]))` → `data[0] == 2.0` (index of the max).
4. `argmax(tensor([5,1,1,1]))` → `data[0] == 0.0`.
5. Output of a reduction is shape `[1]` (ndim 1, shape[0] == 1), F32, contiguous.
6. `sum` of a single-element tensor `tensor([7])` → `7`.
7. Scalar paths unchanged: a program with only scalar arithmetic still lowers to no `TENSOR_*` opcodes (grep the IR).

### `tests/test_phase0_e2e.cpp` (integration seed)

8. End-to-end through the full pipeline (`SourceManager → Parser → Sema → Lowering → Interpreter`) with a `capture` external: a program that computes `sum` and `mean` of a literal and captures each, asserting the numeric results. This file grows in later specs to culminate in the MLP capstone (spec 019).

All 20 pre-existing test binaries must pass unchanged.

## 5. Out of scope

- **Per-axis / last-axis reductions.** v1 is full reduction only. No `axis` argument. (The runtime's `reduce_last_axis` exists but is not exposed here.)
- **`min` / `max` / `prod` from source.** Only `sum`/`mean`/`argmax` this spec; the others are mechanical follow-ups if needed.
- **Integer/`I64` argmax output.** `argmax` returns a float index in a `[1]` F32 tensor, to keep everything in the single F32-tensor value model. A typed-index return is a future concern tied to multi-dtype.
- **Reductions in larger expressions requiring broadcast** (e.g. `t / sum(t)`). That needs tensor-scalar broadcast — spec 017. This spec only proves the reductions themselves.
- **Sema shape-checking of reduction results.** Spec 018.
- **Empty-tensor error semantics.** `sum`/`mean` of an empty tensor defer to `reduce_all` (returns 0); `argmax` returns 0. Not a focus; no literal can produce an empty tensor anyway (parser requires ≥1 element).

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) full-reduction → `[1]` F32 tensor (not last-axis, not rank-0) — simplest semantics, exactly what softmax needs, and composes with the coming broadcast spec; (b) three distinct opcodes rather than one `TENSOR_REDUCE`+mode — matches the established one-opcode-per-op pattern (spec 005) and keeps the interpreter switch greppable; (c) `argmax` returns a float index in an F32 tensor to stay within the single value model, deferring typed indices to the multi-dtype era; (d) seed `test_phase0_e2e.cpp` here as the integration net the roadmap calls for.
- *Implementation, verification* — `ctest` **22/22 passing** (two new binaries: `ZeroReductionsTest`, `ZeroPhase0E2ETest`). `sum`/`mean` reuse `reduce_all`; `argmax` is a direct interpreter scan; all three reuse the existing `tensor_unary` builder helper and `alloc_output_like`. The lowering dispatch and sema builtins slotted in beside `relu` exactly as the pattern predicted — no surprises. The e2e seed verifies reductions through user functions end-to-end. All 20 prior binaries unchanged.
