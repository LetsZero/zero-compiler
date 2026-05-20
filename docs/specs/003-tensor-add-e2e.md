# Spec 003: End-to-end tensor `add` through the interpreter

**Status:** Approved
**Depends on:** spec 001 (build foundation), spec 002 (IR source spans)
**PR:** (pending)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

For the first time, make a tensor `add` produce real bytes by going through the full compiler pipeline (well, the half of it that has working IR plus interpreter) into the frozen runtime. Today every `TENSOR_*` opcode is a stub returning `nullptr` ([src/backend/interpreter.cpp:275–284](../../src/backend/interpreter.cpp)); this spec wires `TENSOR_ADD` (and a helper `TENSOR_CONST_F32` used only to construct test inputs) so they actually call `zero::ops::add` from the runtime, propagate the returned `Status`, and surface failures with the IR instruction's source span attached.

The deliverable is an interpreter test that builds an IR module by hand (no parser involvement), runs it, and reads back the bit-exact result of `[1,2,3,4] + [10,10,10,10] = [11,12,13,14]`. This is the first commit in the repo where the language actually does its job.

## 2. Invariants

- A new IR opcode `TENSOR_CONST_F32` exists. Semantics: produce a fresh contiguous F32 `Tensor` of shape `imm_shape` filled with `imm_floats`. Pure (no operands). The result `Value` has runtime type `Tensor`. This op exists primarily to let tests construct tensors without a parser; production lowering will not emit it.
- `TENSOR_ADD` becomes a real op: given two `Tensor`-typed operands, the interpreter allocates a fresh contiguous F32 output tensor with the same shape as operand 0, calls `zero::ops::add(a, b, out)` from the runtime (Stream `nullptr`), and stores `out` as the SSA result. The shape of operand 0 is taken to be authoritative; operand 1 must match it (or be `numel == 1` per the runtime's binary contract).
- `ir::Instruction` gains a `std::vector<int64_t> imm_shape` field and a `std::vector<float> imm_floats` field. Both default-empty. Used only by `TENSOR_CONST_F32` in v1; other opcodes ignore them.
- `IRBuilder` gains two methods:
  - `Value tensor_const_f32(std::vector<int64_t> shape, std::vector<float> data)` — emits `TENSOR_CONST_F32` and returns its result.
  - `Value tensor_add(Value lhs, Value rhs)` — emits `TENSOR_ADD` and returns its result.
- `RuntimeValue` gains a new variant alternative: `std::shared_ptr<zero::Tensor>`. The `shared_ptr`'s deleter is responsible for calling `tensor->free()` when the last reference dies, **but only if `tensor->owns_data` is true**. This keeps the runtime's caller-allocated/owns-data contract intact.
- `RuntimeValue` exposes `is_tensor()` and `as_tensor()` (returning `const std::shared_ptr<zero::Tensor>&`).
- When `zero::ops::add` returns a non-OK `Status`, the interpreter throws a `std::runtime_error` whose message embeds:
  - the failing op name (`tensor_add`)
  - the `StatusCode` name
  - the `Status::msg` if non-null
  - the instruction's source span as `@<source_id>:<start>-<end>` (per spec 002 dumper format)
- The pre-existing `TENSOR_SUB`, `TENSOR_MUL`, `TENSOR_MATMUL`, `TENSOR_RELU` opcodes remain stubbed. They continue to fall through to the existing `nullptr` path. **They are not wired in this spec.**
- All existing tests continue to pass without modification. Source code under `src/parser/`, `src/sema/`, `src/lexer/`, `src/source/`, `src/driver/`, `src/diagnostics/`, `runtime/` is **not** touched.

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — add `imm_shape: std::vector<int64_t>` and `imm_floats: std::vector<float>` to `Instruction`. Update `opcode_name` for `TENSOR_CONST_F32` to return `"tensor.const.f32"`. (`TENSOR_CONST_F32` is appended to the `OpCode` enum after `TENSOR_RELU`.)
2. `include/ir/builder.hpp` — add the two new `tensor_const_f32` and `tensor_add` builder methods.
3. `include/backend/interpreter.hpp` — extend `RuntimeValue::data` variant with `std::shared_ptr<zero::Tensor>`; add `is_tensor()` and `as_tensor()` accessors; add a constructor taking the shared_ptr.
4. `src/backend/interpreter.cpp` — handle `TENSOR_CONST_F32` and `TENSOR_ADD` in `exec_instruction`. Wire the `Status` check + diagnostic-bearing throw on error.
5. `src/ir/ir.cpp` — extend the dumper for `TENSOR_CONST_F32` (renders shape and data inline so dumped IR is reproducible).

Files added:

6. `tests/test_tensor_add_e2e.cpp` — end-to-end interpreter test.

CMakeLists changes:

7. `tests/CMakeLists.txt` — register the new test binary. **Critically, the new test target links `zero-core`** (it needs the runtime's headers via interpreter and indirectly via `RuntimeValue`).
8. `src/backend/CMakeLists.txt` — the existing `zerobackend` library must link `zero-core`, because `RuntimeValue`'s definition now references `zero::Tensor`. Downstream targets that link `zerobackend` inherit this transitively.

No new top-level symbols outside `zero::ir`, `zero::backend`, and the test executable. No changes to the language grammar.

### Sketch — interpreter handling

```cpp
case OpCode::TENSOR_CONST_F32: {
    int64_t shape_arr[8] = {0};
    int8_t ndim = static_cast<int8_t>(instr.imm_shape.size());
    for (int8_t i = 0; i < ndim; ++i) shape_arr[i] = instr.imm_shape[i];

    auto t = std::shared_ptr<zero::Tensor>(
        new zero::Tensor(zero::Tensor::alloc(shape_arr, ndim, zero::DType::F32)),
        [](zero::Tensor* p) { if (p && p->owns_data) p->free(); delete p; });

    // Fill bytes.
    float* dst = static_cast<float*>(t->data);
    for (size_t i = 0; i < instr.imm_floats.size(); ++i) dst[i] = instr.imm_floats[i];

    result = RuntimeValue(t);
    break;
}

case OpCode::TENSOR_ADD: {
    auto a = get_value(instr.operands[0]).as_tensor();
    auto b = get_value(instr.operands[1]).as_tensor();

    int64_t shape_arr[8] = {0};
    for (int8_t i = 0; i < a->ndim; ++i) shape_arr[i] = a->shape[i];
    auto out = std::shared_ptr<zero::Tensor>(
        new zero::Tensor(zero::Tensor::alloc(shape_arr, a->ndim, zero::DType::F32)),
        [](zero::Tensor* p) { if (p && p->owns_data) p->free(); delete p; });

    zero::Status s = zero::ops::add(*a, *b, *out);
    if (s.is_error()) {
        std::ostringstream msg;
        msg << "tensor_add failed: " << static_cast<int>(s.code);
        if (s.msg) msg << " (" << s.msg << ")";
        if (instr.span.valid()) {
            msg << " @" << static_cast<uint32_t>(instr.span.source_id)
                << ":" << instr.span.start_offset << "-" << instr.span.end_offset;
        }
        throw std::runtime_error(msg.str());
    }
    result = RuntimeValue(out);
    break;
}
```

## 4. Acceptance tests

New test file: `tests/test_tensor_add_e2e.cpp`.

1. **Happy path.** Build an IR module with one function `main` that:
   - Emits `tensor_const_f32([4], {1, 2, 3, 4})` → `%a`
   - Emits `tensor_const_f32([4], {10, 10, 10, 10})` → `%b`
   - Emits `tensor_add(%a, %b)` → `%c`
   - Returns void.
   After `interpreter.execute(...)`, retrieve the last computed tensor via a test helper (or expose it deliberately via the interpreter's `values_` map). Assert it is a 1-D F32 tensor of shape `[4]`, contiguous, with bytes `{11.0, 12.0, 13.0, 14.0}`.
2. **Shape mismatch → throw with span.** Build a module that calls `tensor_add` on tensors of shape `[4]` and `[3]`. Wrap `execute` in a `try/catch` and assert:
   - A `std::runtime_error` was thrown.
   - Its `what()` includes `"tensor_add failed"`, the status code, and a `@<id>:<a>-<b>` span fragment matching the instruction's span.
3. **Dtype mismatch is not directly testable from `TENSOR_CONST_F32` alone** (it produces only F32), so this category is deferred. The spec acknowledges this in §5.
4. **Sentinel "writes zero bytes on error".** Inputs of mismatched shape: the output `Tensor` allocated by the interpreter exists but its bytes were sentinel-filled (e.g., `0xAB` memset) before the runtime call; assert those bytes are unchanged after the failed call. This is a property of the runtime per spec 002 of the runtime, and we re-test it here from the compiler's side to prove the bridge preserves it.
5. **No regressions.** All pre-existing test binaries (`test_lexer`, `test_parser`, `test_sema`, `test_ir`, `test_backend`, `test_ir_spans`, `test_runtime_bridge`, …) build and pass with **zero source changes**.

The test uses the same custom `TEST(...)` macro pattern as `test_ir.cpp`.

## 5. Out of scope

- **Source-level tensor syntax.** Tensors cannot yet be written in `.zero` source. The parser, lexer, sema, and AST are untouched. A future spec will add tensor literals and tensor types to the source language; until then, IR is constructed by hand.
- **The compiler's `runtime/` adapter directory.** It still wraps only `print`/`log`. The tensor bridge sits directly in the interpreter; restructuring the adapter is a separate concern.
- **Other tensor ops.** `TENSOR_SUB`, `TENSOR_MUL`, `TENSOR_MATMUL`, `TENSOR_RELU` remain stubbed. Once spec 003 lands, wiring each additional op is mechanical — a future spec will batch them, but only after this one establishes the pattern.
- **Multi-dtype.** F32 only. Other dtypes throw `NOT_IMPLEMENTED`-equivalent errors from the runtime, which we surface as the same kind of throw, but no new IR machinery for multi-dtype lands here.
- **Stream / Generator / gather / scatter / contiguous opcodes.** None of these gain IR opcodes in this spec. The `add` runtime call passes `Stream* = nullptr` (default).
- **`Reporter`-based diagnostics.** On Status error we throw `std::runtime_error` with a span-embedded message; we do not (yet) route errors through `diagnostics::Reporter`. That integration is its own focused spec — the architectural sketch is `interp.set_reporter(&r);` then format a `Diagnostic` instead of throwing. Deliberately deferred.
- **Tensors as function parameters or return values.** The test function returns void and the tensor lives in SSA values during interpretation. Function-level tensor I/O is its own concern.
- **Reusing existing `print` infrastructure to show tensors.** Out of scope; could be a debugging amenity later.
- **Any optimization pass.** No IR transforms.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) the helper opcode is `TENSOR_CONST_F32` rather than introducing a parser-level tensor literal — keeps the spec strictly to the IR/interpreter half; (b) `RuntimeValue` wraps the runtime `Tensor` in a `std::shared_ptr` with a custom deleter that respects `owns_data`, preserving the runtime's caller-allocated/ownership contract; (c) on `Status::error` the interpreter throws `std::runtime_error` with the span embedded as a `@<id>:<start>-<end>` fragment (same format as the IR dumper from spec 002) — reporter integration deferred; (d) only `TENSOR_ADD` is wired in this spec; the other four `TENSOR_*` opcodes stay stubbed pending a follow-up spec; (e) shape is taken authoritatively from operand 0, matching how the runtime's binary op contract treats `a` as the shape source.
