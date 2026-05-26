# Spec 006: Function-level tensor I/O

**Status:** Implemented
**Depends on:** spec 003, spec 004, spec 005
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

Make user-defined functions with tensor parameters and tensor return types actually work. Today the AST and parser already accept `fn f(x: tensor) -> tensor`, but the interpreter has a `TODO: Bind arguments to parameter values` comment ([src/backend/interpreter.cpp:104](../../src/backend/interpreter.cpp)) and the IR `Function` struct has no representation of parameter SSA values — so even though the syntax compiles, calls to user-defined functions with arguments don't actually pass anything in.

This spec closes the gap. After it lands, the following compiles and runs end-to-end:

```zero
fn double_it(x: tensor) -> tensor {
    return x + x;
}

fn main() {
    let a = tensor([1.0, 2.0, 3.0, 4.0]);
    let b = double_it(a);
    capture(b);
}
```

…producing `{2, 4, 6, 8}` through the frozen runtime.

This is the architectural ceiling that everything downstream — stdlib, named operations, autograd — runs into. Removing it unblocks every future spec.

## 2. Invariants

- `ir::Function` gains a `std::vector<Value> params` field. The size always equals `param_types.size()`. The vector is populated by the lowering pass when the function is constructed, before any body instructions are emitted.
- `Lowering::lower_function` creates the parameter Values via `fn.new_value(...)`, appends each to `fn.params`, **and** registers them in the lowering's `symbols_` map keyed by parameter name — preserving the existing symbol-table behaviour.
- The interpreter's value storage migrates from the global `values_` map to the **per-frame `locals` map** on the active `CallFrame`. `get_value` and `set_value` both read/write `call_stack_.back().locals`.
- `Interpreter::call_function` binds incoming `args[i]` to `fn.params[i].id` in the **callee's** frame, after pushing the frame and before executing the body.
- When a callee returns (`OpCode::RET`), the result `RuntimeValue` is returned from `call_function`. The caller's `CALL` instruction handler then stores that result into the caller's frame via `set_value(instr.result, ...)`. Because the callee's frame has already been popped by then, `set_value` correctly writes to the caller's locals.
- External functions (registered via `register_external`) keep their existing behaviour: they're invoked directly with the args vector and their return value flows back into the caller's frame.
- The `RuntimeValue` global storage (`values_` field on `Interpreter`) is **deleted** along with its `get_value`/`set_value` siblings. It served no callers once locals are per-frame, and keeping it around invites bugs.
- All 15 pre-existing test binaries continue to pass without modification. The migration is purely internal — observable behaviour for single-function programs (everything `main`-only) is unchanged because `main` is itself a frame and uses its own locals.

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — add `std::vector<Value> params;` to `struct Function`. No constructors change.
2. `include/backend/interpreter.hpp` — remove `values_` field, remove `get_value`/`set_value` from the public/private surface (or migrate them — see below). The `CallFrame::locals` field already exists; nothing about it changes structurally.
3. `src/backend/interpreter.cpp` — `get_value`/`set_value` (now defined in the .cpp since they need access to `call_stack_`) read from `call_stack_.back().locals`. `call_function` binds incoming `args` to `fn.params[i].id` in the freshly-pushed frame's locals.
4. `src/ir/lowering.cpp` — `lower_function` populates `fn.params` as it creates parameter Values.

Files added:

5. `tests/test_function_tensor_io.cpp` — end-to-end tests that exercise user-defined functions with tensor parameters and tensor return types.

No changes to the lexer, parser, sema, AST, runtime, or runtime adapter. The full parsing and type-annotation machinery already exists; this spec only wires the IR and interpreter to use it.

### Sketch — get_value / set_value migration

```cpp
// In interpreter.cpp, file-scope (or as private member methods):

RuntimeValue Interpreter::get_value(const ir::Value& v) {
    if (call_stack_.empty()) return RuntimeValue{};
    auto& locals = call_stack_.back().locals;
    auto it = locals.find(v.id);
    return it != locals.end() ? it->second : RuntimeValue{};
}

void Interpreter::set_value(const ir::Value& v, RuntimeValue rv) {
    if (call_stack_.empty()) return;
    call_stack_.back().locals[v.id] = std::move(rv);
}
```

### Sketch — argument binding in call_function

```cpp
CallFrame frame;
frame.fn = &fn;
frame.block_idx = 0;
frame.instr_idx = 0;
// Bind args to parameter SSA values BEFORE pushing the frame so the
// initial locals map is populated when we look it up.
for (size_t i = 0; i < args.size() && i < fn.params.size(); ++i) {
    frame.locals[fn.params[i].id] = std::move(args[i]);
}
call_stack_.push_back(std::move(frame));
```

### Sketch — lower_function param tracking

```cpp
symbols_.clear();
fn.params.clear();
for (size_t i = 0; i < fn_ast.params.size(); ++i) {
    Value param_val = fn.new_value(param_types[i]);
    fn.params.push_back(param_val);
    symbols_[fn_ast.params[i].name] = param_val;
}
```

## 4. Acceptance tests

New test file: `tests/test_function_tensor_io.cpp`.

1. **Single user function, tensor in / tensor out.** Source:
   ```zero
   fn double_it(x: tensor) -> tensor { return x + x; }
   fn main() { let a = tensor([1, 2, 3, 4]); capture(double_it(a)); }
   ```
   Captured tensor: `{2, 4, 6, 8}` bit-exact, shape `[4]`, F32 contiguous.

2. **Function compositing through tensor calls.** Source:
   ```zero
   fn neg_relu(x: tensor) -> tensor { return relu(-x); }
   fn main() { let a = tensor([1, -2, 3, -4]); capture(neg_relu(a)); }
   ```
   Captured tensor: `{0, 2, 0, 4}` (negate then relu).

3. **Multiple parameters.** Source:
   ```zero
   fn weighted_add(a: tensor, b: tensor) -> tensor { return a + b + b; }
   fn main() { let x = tensor([1, 1, 1, 1]); let y = tensor([10, 10, 10, 10]); capture(weighted_add(x, y)); }
   ```
   Captured tensor: `{21, 21, 21, 21}`.

4. **Scalar parameters still work.** A `fn id(x: int) -> int { return x; }` called with `id(7)` returns `7` (verifies the per-frame value-storage migration didn't break scalar paths).

5. **No regressions.** All 15 pre-existing test binaries pass with zero source changes.

## 5. Out of scope

- **Multiple return values.** Single return only.
- **Closures / capturing references.** No.
- **Higher-order functions / function values.** No.
- **Recursion testing.** The implementation may happen to support shallow recursion via per-frame locals, but no test exercises it; future spec may add deeper testing once a real use case appears.
- **Function values as data** (`let f = some_function;`). No first-class functions.
- **Default parameters / named arguments.** No.
- **Generic parameters / templates.** No.
- **Tensors as struct fields.** No struct support for tensors yet.
- **N-D tensor parameters.** Functions accept whatever the caller passes; in practice that's rank-1 today (until 2-D literals land). Nothing in this spec restricts rank, but nothing tests rank > 1 either.
- **Sema type-checking of tensor argument shapes.** Mismatches still surface at runtime.
- **Forward declarations.** Functions must be defined before use, per the existing sema's two-pass behaviour.
- **The parser error-recovery loop bug** (tracked in `docs/DEFERRED.md`). Independent of function I/O.
- **Reporter integration** (tracked in `docs/DEFERRED.md`). Errors still throw.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) the value-storage migration is unconditional — the global `values_` map goes away, every value lives in the active frame's locals; (b) keep `get_value`/`set_value` as the only accessors so the migration is a 5-line change to two methods; (c) parameter binding happens **before** `call_stack_.push_back` so the very first instruction of the callee can already see the args; (d) `ir::Function::params` is a `std::vector<Value>`, parallel to `param_types`, never separately allocated.
- *Implementation* — Three bugs surfaced (and were fixed) during implementation:
  - **`parse_type()` didn't accept the `tensor` keyword.** Spec 004 made `tensor` a `TokenType::TENSOR` keyword for the literal form, but `parse_type()` still expected an `IDENT` matching the string `"tensor"`. So `(x: tensor)` failed to parse and the parser's error-recovery looped, producing a 339 s hang (same pattern as spec 005's first run). Fixed by accepting `TokenType::TENSOR` in `parse_type()` as `TypeKind::TENSOR`. Strictly a spec 004 carry-over; logged a finer-grained `DEFERRED.md` entry noting the recovery bug should be hardened.
  - **`Lowering::CallExpr` always assumed void return type.** Even when the callee was a user-defined function returning `tensor`, the generated `CALL` instruction had an invalid result `Value`, so `let b = double_it(a)` couldn't be bound to anything usable. Fixed by adding a forward-pass `fn_return_types_` map in `Lowering::lower()` and using it in the CallExpr branch.
  - **Interpreter call-stack reference invalidation.** `call_function` holds `auto& current = call_stack_.back()` and then recursively calls `call_function` via `exec_instruction → CALL`. If the nested push reallocated the underlying `std::vector`, `current` dangled and the second iteration read garbage state, producing a `std::bad_variant_access` deep inside an op's variant read. Patched by `call_stack_.reserve(1024)` in `execute()`; the proper index-based refactor of that loop is logged in `docs/DEFERRED.md`.
- *Implementation, verification* — `ctest` **16/16 passing**. The new `ZeroFunctionTensorIOTest` covers a single-param tensor function (`double_it`), tensor function composition (`neg_relu`), two-param tensor function (`weighted_add` — the test that surfaced the reallocation bug), and the scalar-param regression (`id(7)`). All 15 pre-existing test binaries pass unmodified.
