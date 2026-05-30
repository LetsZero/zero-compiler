# Spec 017: Tensor–scalar broadcast

**Status:** Approved
**Depends on:** spec 005 (binary tensor-op interpreter block), spec 014 (reductions, for the e2e softmax)
**PR:** (pending)
**Author:** Ritwik
**Repo:** zero-compiler
**Roadmap:** Phase 0, item 017.

---

## 1. Goal

Make `tensor OP scalar` work — e.g. `x * 2.0`, `h + 1.0`, `g / 2.0`. Today a binary op with one tensor and one scalar operand lowers to a `TENSOR_*` op (lowering already dispatches when *either* operand is tensor-typed), but the interpreter then throws `"non-tensor operand"` because the scalar side isn't a tensor. This spec makes the interpreter broadcast a scalar operand against the tensor.

**Note (scope clarification):** the ROADMAP listed 017 as needed for softmax's `e / s`. That turned out already-working: `sum(e)` returns a `[1]` *tensor*, so `e / sum(e)` is tensor÷tensor and the runtime's existing `numel==1` broadcast handles it (verified: `softmax(tensor([1,2,3]))` = `{0.090, 0.245, 0.665}`, sums to 1.0). So this spec is **not** on the capstone-critical path — it closes a real ergonomic gap (`x * 2.0` currently throwing), keeping the language coherent.

## 2. Invariants

- For a `TENSOR_ADD` / `TENSOR_SUB` / `TENSOR_MUL` / `TENSOR_DIV` instruction where exactly one operand is a tensor and the other is a scalar `RuntimeValue` (int or float), the interpreter materializes the scalar as a `[1]` F32 tensor and uses the runtime's binary broadcast path. Output shape = the **tensor** operand's shape.
- **Tensor on the left** (`t OP s`): computes `t[i] OP s` for all `i`, for all four ops. (Runtime: `a = t`, `b = [s]`.)
- **Scalar on the left** (`s OP t`), **commutative ops only** (`+`, `*`): computes `s OP t[i]` ≡ `t[i] OP s` for all `i` (operands swapped so the tensor is `a`).
- **Scalar on the left, non-commutative** (`s - t`, `s / t`): **not supported** — throws a clear `std::runtime_error` naming the limitation. The runtime broadcast only supports a scalar `b` (`a[i] OP b_val`), and `s - t[i]` cannot be expressed that way. Documented in §5; a later spec can add it via full-shape scalar materialization.
- **tensor ÷ tensor[1]** broadcast (the softmax path) continues to work via the existing both-tensor path — unchanged.
- Both-scalar operands never reach a `TENSOR_*` op (lowering only emits one when an operand is tensor-typed), so that case is not handled here.
- No IR, builder, sema, or lowering change — this is an interpreter-only fix. Lowering already dispatches `tensor OP scalar` to the tensor op.
- All 24 pre-existing test binaries pass unchanged.

## 3. API surface

Files modified:

1. `src/backend/interpreter.cpp` — in the binary tensor-op block (`TENSOR_ADD/SUB/MUL/DIV`), handle the three mixed cases (tensor-left-scalar-right; scalar-left-tensor-right commutative; scalar-left non-commutative → throw). Add a small file-local helper `scalar_to_tensor1(const RuntimeValue&) -> TensorPtr` building a `[1]` F32 tensor from `rv.to_float()`.

Files added:

2. `tests/test_tensor_scalar.cpp` — spec-017 unit tests.

Files modified (tests):

3. `tests/test_phase0_e2e.cpp` — add a case using a scalar literal in tensor arithmetic (e.g. scaling a vector), now that it works.

No new opcodes, no new builtins. Lowering and sema untouched.

### Interpreter sketch

```cpp
// helper (anonymous namespace)
static TensorPtr scalar_to_tensor1(const RuntimeValue& v) {
    int64_t one[1] = {1};
    TensorPtr t(new zero::Tensor(zero::Tensor::alloc(one, 1, zero::DType::F32)),
                [](zero::Tensor* p){ if (p && p->owns_data) p->free(); delete p; });
    static_cast<float*>(t->data)[0] = static_cast<float>(v.to_float());
    return t;
}

// in the ADD/SUB/MUL/DIV block:
RuntimeValue lv = get_value(instr.operands[0]);
RuntimeValue rv = get_value(instr.operands[1]);
TensorPtr a, b;            // a is always the shape-defining tensor operand
if (lv.is_tensor() && rv.is_tensor()) {            // existing path
    a = lv.as_tensor(); b = rv.as_tensor();
} else if (lv.is_tensor()) {                        // t OP s
    a = lv.as_tensor(); b = scalar_to_tensor1(rv);
} else if (rv.is_tensor()) {                        // s OP t
    if (instr.op == TENSOR_ADD || instr.op == TENSOR_MUL) {  // commutative: swap
        a = rv.as_tensor(); b = scalar_to_tensor1(lv);
    } else {
        throw std::runtime_error(
            "scalar on the left of a non-commutative tensor op "
            "(s - t / s / t) is not supported");
    }
} else {
    throw std::runtime_error("tensor binary op: non-tensor operand");
}
// output shape = a->shape; runtime op(a, b, out) as before.
```

## 4. Acceptance tests

### `tests/test_tensor_scalar.cpp` (unit)

1. `tensor([1,2,3]) * 2.0` → `{2,4,6}`.
2. `tensor([1,2,3]) + 10.0` → `{11,12,13}`.
3. `tensor([10,20,30]) / 2.0` → `{5,10,15}`.
4. `tensor([5,6,7]) - 1.0` → `{4,5,6}`.
5. **Commutative scalar-left:** `2.0 * tensor([1,2,3])` → `{2,4,6}`; `10.0 + tensor([1,2,3])` → `{11,12,13}`.
6. **Non-commutative scalar-left throws:** `2.0 - tensor([1,2,3])` and `6.0 / tensor([1,2,3])` each throw `std::runtime_error` mentioning the limitation.
7. **Integer scalar literal:** `tensor([1,2,3]) * 2` → `{2,4,6}` (int widened to float).
8. **Regression — tensor ÷ tensor[1] still works:** `tensor([2,4,6]) / sum(tensor([1,1,1]))` → `{0.66.., 1.33.., 2.0}`.
9. **2-D scaling:** `tensor([[1,2],[3,4]]) * 3.0` → shape `{2,2}`, data `{3,6,9,12}`.

### `tests/test_phase0_e2e.cpp` (integration, extended)

10. A scaled vector through a user fn: `fn scale(t: tensor) -> tensor { return t * 0.5; }` over `[2,4,6]` → `{1,2,3}`.

All 24 pre-existing test binaries pass unchanged. Floats use ε bounds.

## 5. Out of scope

- **Scalar-on-left, non-commutative** (`s - t`, `s / t`). Throws. Needs full-shape scalar materialization (a `[N]` tensor filled with `s`), which requires the tensor's runtime shape inside lowering or a new runtime path — deferred.
- **Scalar variables vs literals** — both already work identically here, because the broadcast happens in the interpreter on the `RuntimeValue` (a literal and a `let`-bound scalar are both scalar `RuntimeValue`s by then). No distinction needed.
- **Tensor ÷ tensor[1]** broadcast — already works (runtime), untouched. Not part of this spec's new code.
- **Broadcast between two non-trivial shapes** (e.g. `[3]` + `[1,3]`, NumPy-style rank broadcasting). Out of scope; only scalar↔tensor here.
- **Sema-level scalar/tensor type checking.** Spec 018.
- **A `SCALAR_TO_TENSOR` IR op.** Not introduced — the materialization is an interpreter detail. If a backend (LLVM) later needs it explicit in IR, that's a backend-era concern.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) fix is interpreter-only — lowering already dispatches `tensor OP scalar` to the tensor op, so no IR/sema/lowering change; (b) support tensor-left (all ops) and scalar-left commutative (`+`,`*`) by swapping; scalar-left non-commutative throws a clear error and is deferred (the runtime broadcast can't express `s - t[i]`); (c) materialize the scalar in the interpreter from the `RuntimeValue`, so scalar literals and scalar variables both work with no lowering distinction; (d) recorded that softmax's `e/s` already works (tensor÷tensor[1]) so this spec is off the capstone-critical path — an ergonomic fix, not a blocker.
