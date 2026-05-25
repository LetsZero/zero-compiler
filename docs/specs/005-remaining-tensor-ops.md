# Spec 005: Wire the remaining tensor opcodes

**Status:** Implemented
**Depends on:** spec 003 (TENSOR_ADD wiring), spec 004 (source-level tensor + `+` dispatch)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

Finish what spec 003 started. Today only `TENSOR_ADD` is wired; `TENSOR_SUB`, `TENSOR_MUL`, `TENSOR_MATMUL`, `TENSOR_RELU` still fall through to the `nullptr` stub. Source-level dispatch in spec 004 only handles `+`. This spec closes the remaining gaps:

- New IR opcodes for the missing elementwise ops: `TENSOR_NEG`, `TENSOR_DIV`.
- Interpreter handlers for every binary/unary tensor op the runtime exposes today: `sub`, `mul`, `div`, `neg`, `relu`, `matmul`.
- Source-level dispatch via type-based emit selection: `-`, `*`, `/`, unary `-`, and `relu(...)` calls.

After this spec, the following compiles and runs end-to-end against the frozen runtime:

```zero
fn main() {
    let a = tensor([1.0, 2.0, 3.0, 4.0]);
    let b = tensor([5.0, 5.0, 5.0, 5.0]);
    let c = relu(a - b);
    let d = c * c;
    capture(d);
}
```

`matmul` is wired at the IR/interpreter level but is **not** reachable from source in this spec — it requires 2-D tensor literals, which are explicitly out of scope. Hand-constructed IR tests cover it.

## 2. Invariants

- Two new IR opcodes exist, **appended** to the `OpCode` enum after `TENSOR_CONST_F32`:
  - `TENSOR_NEG` — unary, calls `zero::ops::neg`.
  - `TENSOR_DIV` — binary, calls `zero::ops::div`.
- The interpreter handles all of `TENSOR_SUB`, `TENSOR_MUL`, `TENSOR_DIV`, `TENSOR_NEG`, `TENSOR_RELU`, `TENSOR_MATMUL` using the same pattern as `TENSOR_ADD`: pull operands, allocate caller-allocated output with the LHS shape (for matmul, the output shape is `[A.shape[0], B.shape[1]]`), sentinel-fill 0xAB, call into the runtime, on `Status::error` throw `std::runtime_error` with the op name, status code, msg, and `@<id>:<start>-<end>` span fragment.
- `IRBuilder` exposes `tensor_sub`, `tensor_mul`, `tensor_div`, `tensor_neg`, `tensor_relu`, `tensor_matmul` — symmetric with the existing `tensor_add`. Each returns a Value with `Type::make_tensor()`.
- Lowering dispatches the following AST forms to the new tensor opcodes when operands are tensor-typed:
  - `BinaryExpr::SUB`, `MUL`, `DIV` → `TENSOR_SUB`, `TENSOR_MUL`, `TENSOR_DIV`.
  - `UnaryExpr::NEG` (the only `UnaryOp` today) → `TENSOR_NEG`.
  - `CallExpr` with callee `"relu"` and a single tensor argument → `TENSOR_RELU`.
- Scalar paths for `-`, `*`, `/`, `neg`, and `relu()` are completely unchanged when no operand is tensor-typed.
- `relu` is registered as a variadic builtin in sema, returning `Type::make_tensor()`. `matmul` is **not** registered as a sema builtin in this spec, because it has no source-level path in v1.
- `TENSOR_ALLOC` remains stubbed. It has no defined semantics in the current IR (`TENSOR_CONST_F32` covers literal construction). A future spec may repurpose or remove it.
- The error story for tensor-tensor ops with mismatched shapes is unchanged from spec 003: the runtime returns `Status::error`, the interpreter throws with the span attached.
- All 14 pre-existing test binaries continue to pass without modification.

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — append `TENSOR_NEG` and `TENSOR_DIV` to `OpCode`; update `opcode_name` for both.
2. `include/ir/builder.hpp` — add six new methods: `tensor_sub`, `tensor_mul`, `tensor_div`, `tensor_neg`, `tensor_relu`, `tensor_matmul`.
3. `src/backend/interpreter.cpp` — add handlers for the six tensor opcodes. The binary handlers (`SUB`, `MUL`, `DIV`) share validation logic with `TENSOR_ADD`; factor a small static helper `tensor_binary_apply(...)` that takes the runtime op function pointer. Unary `NEG`/`RELU` and `MATMUL` get their own arms.
4. `src/sema/sema.cpp` — register `relu` builtin in `register_builtins()`.
5. `src/ir/lowering.cpp` — extend `BinaryExpr` and `UnaryExpr` dispatch to handle the new tensor ops; extend `CallExpr` to recognize `relu(...)` with a tensor argument.

Files added:

6. `tests/test_remaining_tensor_ops.cpp` — end-to-end tests covering every newly-wired op.

No new public symbols outside those listed. No changes to the runtime, runtime adapter, or AST.

### Sketch — interpreter binary helper

```cpp
// File-local helper. Both pointers are non-owning; out is the
// caller-allocated destination expected by the runtime.
template <typename Op>
static zero::Status run_binary(
    const TensorPtr& a, const TensorPtr& b, TensorPtr& out_holder,
    Op op_fn)
{
    int64_t shape_arr[zero::MAX_DIMS] = {0};
    for (int8_t i = 0; i < a->ndim; ++i) shape_arr[i] = a->shape[i];
    out_holder = std::shared_ptr<zero::Tensor>(
        new zero::Tensor(zero::Tensor::alloc(shape_arr, a->ndim, zero::DType::F32)),
        [](zero::Tensor* p) { if (p && p->owns_data) p->free(); delete p; });
    std::memset(out_holder->data, 0xAB, out_holder->nbytes());
    return op_fn(*a, *b, *out_holder);
}
```

Then each binary case looks like:

```cpp
case OpCode::TENSOR_SUB: {
    auto a = get_value(instr.operands[0]).as_tensor();
    auto b = get_value(instr.operands[1]).as_tensor();
    TensorPtr out;
    zero::Status s = run_binary(a, b, out,
        [](auto& x, auto& y, auto& z) { return zero::ops::sub(x, y, z); });
    if (s.is_error()) throw_with_span("tensor_sub", s, instr.span);
    result = RuntimeValue(std::move(out));
    break;
}
```

A small helper `throw_with_span(op_name, status, span)` centralises the message format. The same shape that spec 003 produced inline gets reused for every new op.

`TENSOR_MATMUL`'s output shape comes from `[A.shape[0], B.shape[1]]`, not from the LHS alone — it uses a separate code path rather than `run_binary`.

`TENSOR_RELU` and `TENSOR_NEG` are unary; they reuse the LHS shape but pass through `zero::ops::relu` / `zero::ops::neg`.

## 4. Acceptance tests

New test file: `tests/test_remaining_tensor_ops.cpp`.

1. **`tensor_sub` end-to-end via source**: `tensor([5,4,3,2]) - tensor([1,1,1,1])` → `{4,3,2,1}`.
2. **`tensor_mul` end-to-end via source**: `tensor([1,2,3,4]) * tensor([2,2,2,2])` → `{2,4,6,8}`.
3. **`tensor_div` end-to-end via source**: `tensor([10,20,30,40]) / tensor([2,4,6,8])` → `{5,5,5,5}`.
4. **`tensor_neg` end-to-end via source**: `-tensor([1,2,3,4])` → `{-1,-2,-3,-4}`.
5. **`tensor_relu` end-to-end via source**: `relu(tensor([-1, 0, 2, -3]))` → `{0, 0, 2, 0}`.
6. **Mixed expression**: `relu(tensor([1,2,3,4]) - tensor([3,3,3,3])) * tensor([2,2,2,2])` → `{0, 0, 0, 2}`.
7. **`tensor_matmul` via hand-constructed IR**: 2×3 × 3×2 matmul produces the expected 2×2 result (`{22, 28, 49, 64}` with the same test inputs as the runtime's own basic test).
8. **Scalar paths unchanged**: `let x = 5 - 2; let y = x * 3; let z = y / 2; let w = -z;` lowers entirely to scalar `OpCode::{SUB, MUL, DIV, NEG}` with no `TENSOR_*` opcodes anywhere in the resulting IR.
9. **Shape mismatch on `tensor_sub` throws with span**: same error-path contract spec 003 established for `TENSOR_ADD`.
10. **No regressions**: all 14 pre-existing test binaries pass unmodified.

## 5. Out of scope

- **2-D tensor literals.** No `tensor([[1,2],[3,4]])`. Required for source-level `matmul`; deferred to a separate spec.
- **Source-level `matmul`.** No `matmul(a, b)` syntax in v1. The IR-level path works (tested by hand-constructed IR), but until 2-D literals land, no real source program can invoke it.
- **Other elementwise ops** (`exp`, `log`, `sqrt`, `tanh`, `sigmoid`, `abs`, `sin`, `cos`). The runtime supports them via `ElementwiseOp` but no IR opcodes exist for them. A follow-up spec adds them mechanically.
- **Tensor-scalar broadcast at the language level.** `a + 2.0` where `a` is tensor is not yet dispatched (the runtime supports `b.numel() == 1` broadcast, but the compiler doesn't construct a scalar tensor from a scalar literal). Deferred.
- **Reductions** (`sum`, `mean`, `max`, etc.). No IR opcodes, no source path. Each gets its own future spec.
- **Indexed access** (`gather`, `scatter`). Separate concern.
- **`Generator` support in source.** Deferred.
- **Sema type-checking of tensor ops.** Shape compatibility is still only enforced at runtime via `Status`.
- **Reporter integration.** Errors still throw `std::runtime_error`.
- **TENSOR_ALLOC.** Remains stubbed; not used by anything.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) two new opcodes (`TENSOR_NEG`, `TENSOR_DIV`) are appended; (b) `matmul` is wired at the IR/interpreter level but explicitly *not* at the source level — no source program can construct the 2-D tensors it requires; (c) factored a static `run_binary` helper in the interpreter so each new binary op is ~5 lines, mirroring the runtime's own validator-helper pattern; (d) `relu` registered as a sema builtin; `matmul` is *not* (would require source path to ever fire); (e) `TENSOR_ALLOC` left stubbed and unused — calling it out as deletion-candidate for a future spec.
- *Implementation* — Two refinements:
  - **Parser fix for negative literals inside tensor brackets.** The first run hung on `tensor([-1.0, 0.0, 2.0, -3.0])` — spec 004's parser only accepted INT_LIT / FLOAT_LIT directly, not a `-` prefix. Without the prefix the parser failed and its error-recovery path looped. Extended the tensor-literal parser to accept an optional `MINUS` token before each element and negate it. Strictly speaking this fix belongs to spec 004's "tensor literal syntax," but since it surfaced here, fixing it here is more honest than ignoring it. The spec 004 amendment log should reference this fix; for now, the fix lives in spec 005's commit.
  - **Final helper shape.** The spec sketch mentioned a template `run_binary(...)` helper; the actual implementation is a smaller, non-template `alloc_output_like(shape, ndim)` plus a switch inside the case statement that selects which runtime op function to call. Cleaner because the four binary ops differ only in which `zero::ops::*` they call, and a switch is more legible than a function pointer here.
- *Implementation, verification* — `ctest` **15/15 passing**. The new `ZeroRemainingTensorOpsTest` covers all six new opcodes (sub/mul/div/neg/relu through source; matmul via hand-constructed IR), the mixed-expression case `relu(a - b) * c`, scalar-paths-untouched, and the shape-mismatch error path with span. All 14 pre-existing test binaries pass unmodified.
