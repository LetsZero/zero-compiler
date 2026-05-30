# Spec 018: Static (literal-derived) shape checking in sema

**Status:** Implemented
**Depends on:** specs 014–017 (the ops whose shapes are inferred)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler
**Roadmap:** Phase 0, item 018.

---

## 1. Goal

Catch statically-knowable tensor shape mismatches at **compile time** with a diagnostic, instead of only at runtime. E.g. `tensor([1,2]) + tensor([1,2,3])` or a `matmul` with non-conforming inner dimensions should be a sema error before the interpreter ever runs.

Scope is deliberately bounded: **literal-derived shapes only**. A tensor literal has a known shape; that shape propagates through `let` bindings and shape-deterministic ops (elementwise, reductions, matmul). Anything whose shape isn't statically known (a function parameter, a value from a user function) has **unknown** shape and is simply not checked — it falls through to the existing runtime `Status` check. This is not a full shape-inference engine; it is bounded propagation of known literal shapes.

## 2. Invariants

- A new `ErrorKind::SHAPE_MISMATCH` exists; shape errors report through the existing `error(...)` path (and thus, via the driver, the Reporter) with the offending expression's span.
- Sema computes an inferred **static shape** for tensor-typed expressions, where determinable. Representation: `std::optional<std::vector<int64_t>>` — `nullopt` means "unknown, do not check"; a present vector is the known shape. (A scalar / non-tensor expression also yields `nullopt`; conflating "scalar" and "unknown tensor" as `nullopt` is intentional — both mean "skip the shape check here".)
- Shape propagation rules (all literal-derived):
  - **Tensor literal** → its `shape`.
  - **Identifier** → the shape recorded for that variable at its `let` (if known), else `nullopt`.
  - **Group** → inner expression's shape.
  - **Unary `-`** on a tensor → operand shape (shape-preserving).
  - **`exp`/`log`/`sqrt`/`tanh`/`sigmoid`/`relu`** (1 arg) → operand shape (shape-preserving).
  - **`sum`/`mean`/`argmax`** (1 arg) → `{1}` (full reduction → 1-element tensor, per spec 014).
  - **`matmul`(a, b)** → if both shapes known and rank-2: result `{a[0], b[1]}`; if inner dims disagree (`a[1] != b[0]`) → `SHAPE_MISMATCH` error, result `nullopt`. If either unknown or not rank-2 → `nullopt`, no check.
  - **Elementwise binary `+ - * /`** → if both operand shapes known: equal shapes → that shape; one operand has `numel == 1` → the other shape (scalar/`[1]` broadcast, matching the runtime); otherwise → `SHAPE_MISMATCH` error, `nullopt`. If only one side known (e.g. `tensor * 2.0`, or one side a parameter) → the known side's shape, no error. Comparisons (`== < >` …) → `nullopt` (scalar bool result).
  - **User-function call** → `nullopt` (return shape not statically tracked).
- Variable shapes are tracked per function in a `var_shapes_` map keyed by name, cleared at the start of each `check_fn`. Function parameters are **not** recorded (unknown shape). (Block-level shadowing of a tensor variable is not separately scoped — a known, low-risk limitation noted in §5.)
- Shape inference runs **once per top-level expression** in `check_stmt` (the `LetStmt` init, `ReturnStmt` value, `ExprStmt` expression), so each mismatch is reported exactly once. It does not run on `if`/`while` conditions (scalar bool).
- Sema shape errors are **non-fatal** (collected, like all sema errors). Lowering and execution still proceed if a caller ignores `had_error()` — so existing tests that deliberately trigger a *runtime* mismatch throw continue to pass (they don't gate on sema). The new value is: a caller that *does* check `had_error()` (e.g. `zeroc`) now rejects the program at compile time.
- All 25 pre-existing test binaries pass unchanged.

## 3. API surface

Files modified:

1. `include/sema/sema.hpp` — add `SHAPE_MISMATCH` to `ErrorKind`; add `using Shape = std::vector<int64_t>;`, a `std::unordered_map<std::string, Shape> var_shapes_;` member, and a private method `std::optional<Shape> infer_shape(ast::Expr&);`. Clear `var_shapes_` in `reset()`.
2. `src/sema/sema.cpp` —
   - `check_fn`: clear `var_shapes_` at entry.
   - `check_stmt`: `LetStmt` records `infer_shape(*s.init)` into `var_shapes_[s.name]` (when known); `ReturnStmt` and `ExprStmt` call `infer_shape` on their expression to run the checks.
   - Implement `infer_shape` per the rules in §2, emitting `SHAPE_MISMATCH` on incompatible elementwise/matmul shapes.
3. `src/diagnostics/reporter.cpp` or the sema→Reporter mapping (if `ErrorKind` is mapped to an `ErrorType` there) — map `SHAPE_MISMATCH` to `ErrorType::TYPE` (it is a kind of type error). *(If sema errors are not yet routed through the Reporter, this sub-item is a no-op; the error still lands in `errors_`.)*

Files added:

4. `tests/test_shape_check.cpp` — spec-018 unit tests (assert `sema.had_error()` for mismatches, and *no* error for valid/unknown-shape programs).

Files modified (tests):

5. `tests/test_phase0_e2e.cpp` — add an assertion that a well-formed multi-op program (matmul + elementwise + reduction) produces **no** sema error (guards against false positives on the capstone path).

No parser, IR, builder, lowering, or interpreter change.

## 4. Acceptance tests

### `tests/test_shape_check.cpp` (unit)

A helper compiles source through parse + sema and returns `sema.had_error()`.

1. **Elementwise mismatch via literals:** `tensor([1,2]) + tensor([1,2,3])` → error.
2. **Elementwise mismatch via variables:** `let a = tensor([1,2]); let b = tensor([1,2,3]); let c = a + b;` → error.
3. **matmul inner-dim mismatch:** `matmul(tensor([[1,2,3]]), tensor([[1,2]]))` (`[1,3]·[1,2]`, inner 3≠1) → error.
4. **Valid elementwise:** `tensor([1,2,3]) + tensor([4,5,6])` → no error.
5. **Valid matmul:** `matmul(tensor([[1,2,3],[4,5,6]]), tensor([[1,2],[3,4],[5,6]]))` (`[2,3]·[3,2]`) → no error.
6. **Broadcast not flagged:** `tensor([1,2,3]) / sum(tensor([1,1,1]))` (`[3] / [1]`) → no error.
7. **Scalar broadcast not flagged:** `tensor([1,2,3]) * 2.0` → no error.
8. **Unknown shape not flagged:** `fn f(x: tensor) -> tensor { return x + x; }` → no error (parameter shape unknown).
9. **Shape-preserving propagation:** `let a = tensor([1,2,3]); let b = exp(a); let c = b + tensor([1,2]);` → error (b is `[3]`, literal is `[2]`).
10. **Reduction result shape:** `let a = tensor([1,2,3]); let s = sum(a); let r = s + tensor([9]);` → no error (`[1] + [1]`); but `let r2 = s + tensor([1,2,3]);` → no error too (`[1]` broadcasts) — confirm broadcast direction both ways is accepted.

### `tests/test_phase0_e2e.cpp` (integration, extended)

11. A well-formed program combining `matmul`, `relu`, `exp`, `sum`, and `/` (a softmax-over-linear shape) analyzes with **no** sema error — guards the capstone path against false positives.

All 25 pre-existing test binaries pass unchanged (they don't gate on `had_error()`, so deliberately-mismatched runtime-throw tests still execute and throw).

## 5. Out of scope

- **Dynamic / non-literal shapes.** Function parameters, user-function results, and anything not literal-derived have unknown shape and are not checked. Runtime `Status` remains the backstop.
- **Full shape inference / a typed-shape system in `types::Type`.** Shapes live in a sema-side map, not in `Type`. A real shape-carrying type system is a much larger, post-Phase-0 effort.
- **Block-scoped shadowing of tensor variables.** `var_shapes_` is per-function-flat; a tensor variable redeclared in an inner block overwrites the outer entry. Low-risk; noted, not handled.
- **Rank/broadcast rules beyond scalar↔tensor and equal-shape.** No NumPy-style rank broadcasting (`[3]` vs `[1,3]`); such a pair is treated as a mismatch if both known. (Matches the runtime, which also doesn't do it.)
- **Propagating shapes across function boundaries** (inferring a callee's result shape from its body). No interprocedural shape analysis.
- **Making sema errors fatal / halting compilation.** They remain collected; whether to stop on `had_error()` is the driver's policy, unchanged here.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) shapes tracked in a sema-side `var_shapes_` map (`optional<vector<int64_t>>`), not in `types::Type`; (b) `nullopt` conflates "scalar" and "unknown tensor" because both mean "skip the check"; (c) inference runs once per top-level statement expression so mismatches report exactly once; (d) a dedicated `SHAPE_MISMATCH` ErrorKind; (e) sema errors stay non-fatal, so existing runtime-throw tests are unaffected.
- *Implementation, verification* — `ctest` **26/26** (`ZeroShapeCheckTest`, 10 cases: literal/variable mismatch, matmul inner-dim mismatch, valid programs, broadcast-not-flagged, unknown-param-not-flagged, shape-preserving propagation, reduction-result broadcast). All 25 prior binaries unchanged (sema errors non-fatal). Shape inference behaved as designed; no rework.
- *Implementation — orthogonal parser bug surfaced.* The e2e false-positive guard was first written **multi-line** and **hung in the parser** (not sema): the parser doesn't skip newlines inside `(...)`/`[...]`, so a multi-line `matmul(a,\n b)` errors mid-expression and the function-body loop spins (the long-deferred "general no-progress guard"). Shape-checking itself is fine on the single-line equivalent (sema clean). The e2e test was rewritten single-line; both parser issues are logged in `docs/DEFERRED.md` for a dedicated parser-robustness spec — recommended before the spec-019 capstone.
