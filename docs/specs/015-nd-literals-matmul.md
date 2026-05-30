# Spec 015: 2-D tensor literals + source-callable `matmul`

**Status:** Implemented
**Depends on:** spec 004 (1-D literals), spec 005 (matmul IR/interpreter path), spec 007 (parser recovery)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler
**Roadmap:** Phase 0, item 015.

---

## 1. Goal

Let source express matrices and multiply them — the missing pieces for a linear layer.

```zero
let w = tensor([[0.1, 0.2, 0.3],
                [0.4, 0.5, 0.6]]);   # shape [2, 3]
let y = matmul(x, w);
```

Two additions: **2-D tensor literals** (nested brackets, shape inferred, ragged rejected) and a **source-callable `matmul(a, b)`** that lowers to the already-wired `TENSOR_MATMUL`.

Scope is 1-D and 2-D only. 3-D+ literals are rejected with a clear error (the MLP capstone needs at most 2-D; general N-D is deferred).

## 2. Invariants

- `ast::TensorLiteral` gains `std::vector<int64_t> shape;` alongside the existing flattened `std::vector<double> values;` (row-major). A 1-D literal has `shape == {N}`; a 2-D literal has `shape == {rows, cols}` with `values` flattened row-major.
- The parser accepts:
  - 1-D: `tensor([ a, b, c ])` → `shape {3}` (unchanged behaviour).
  - 2-D: `tensor([ [a,b], [c,d], [e,f] ])` → `shape {3, 2}`, `values {a,b,c,d,e,f}`.
  - Each numeric element may carry an optional leading `-` (spec 004 behaviour preserved).
- The parser **rejects** with a clear, single error (and recovers without hanging — spec 007 discipline):
  - **Ragged** 2-D: rows of differing length → "ragged tensor literal: rows have differing lengths".
  - **3-D or deeper**: a third `[` nesting level → "only 1-D and 2-D tensor literals are supported".
  - **Mixed**: some rows bracketed, some not → falls under the ragged/element errors.
  - **Empty**: `tensor([])` or `tensor([[]])` → "tensor literal must have at least one element".
- Lowering uses `e.shape` (not `{values.size()}`) when emitting `TENSOR_CONST_F32`. A 2-D literal therefore produces a rank-2 contiguous F32 tensor.
- `matmul` is a sema builtin returning `tensor`. Lowering: a `CallExpr` with callee `matmul` and **exactly two tensor-typed arguments** lowers to `builder.tensor_matmul(a, b)`. Other arity/types fall through to the normal call path.
- The interpreter and runtime matmul path are unchanged (spec 005 already wired `TENSOR_MATMUL`: output `[A.rows, B.cols]`, runtime validates rank-2 / inner-dim and returns `Status` → diagnostic on mismatch).
- All 22 pre-existing test binaries pass unchanged. (1-D literals must behave exactly as before.)

## 3. API surface

Files modified:

1. `include/ast/ast.hpp` — add `std::vector<int64_t> shape;` to `TensorLiteral`.
2. `include/parser/parser.hpp` — declare a private helper `bool parse_number_row(std::vector<double>& out, size_t& count);` (parses `num (, num)*`, optional leading `-`; returns false + sets a flag on a non-numeric token).
3. `src/parser/parser.cpp` — restructure the `TENSOR` branch in `parse_primary`: after `tensor (` and the outer `[`, peek for a nested `[` to choose 1-D vs 2-D; build `values` + `shape`; detect ragged / 3-D / empty; preserve spec-007 recovery (advance to `RBRACKET`/`RPAREN`/`SEMICOLON` on error).
4. `src/sema/sema.cpp` — register `matmul` as a tensor-returning variadic builtin.
5. `src/ir/lowering.cpp` — (a) `TensorLiteral` lowering uses `e.shape`; (b) `CallExpr` dispatches `matmul` with two tensor args to `tensor_matmul`.

Files modified (tests):

6. `tests/test_phase0_e2e.cpp` — extend with a `matmul`-through-source case (a 1×2 · 2×3 → 1×3 linear-layer shape).

Files added:

7. `tests/test_nd_literals.cpp` — spec-015 unit tests.

No IR opcode changes (matmul opcode already exists). No interpreter changes.

### Parser shape (sketch)

```cpp
if (match(TokenType::TENSOR)) {
    Span start = previous_.span;
    consume(LPAREN, ...); consume(LBRACKET, "Expected '[' ...");
    TensorLiteral lit;
    bool failed = false;

    if (check(TokenType::LBRACKET)) {
        // 2-D: a list of rows.
        int64_t cols = -1, rows = 0;
        while (!check(RBRACKET) && !current_.is_eof()) {
            consume(LBRACKET, "Expected '[' to start a tensor row");
            if (check(TokenType::LBRACKET)) {           // a third level
                error("only 1-D and 2-D tensor literals are supported");
                failed = true; break;
            }
            size_t n = 0;
            if (!parse_number_row(lit.values, n)) { failed = true; break; }
            consume(RBRACKET, "Expected ']' to end a tensor row");
            if (cols < 0) cols = static_cast<int64_t>(n);
            else if (static_cast<int64_t>(n) != cols) {
                error("ragged tensor literal: rows have differing lengths");
                failed = true; break;
            }
            ++rows;
            if (!match(COMMA)) break;
        }
        lit.shape = { rows, cols < 0 ? 0 : cols };
    } else {
        // 1-D.
        size_t n = 0;
        if (!parse_number_row(lit.values, n)) failed = true;
        lit.shape = { static_cast<int64_t>(n) };
    }

    if (failed) { /* spec-007 recovery: advance to ] ) ; */ }

    consume(RBRACKET, ...); consume(RPAREN, ...);
    if (lit.values.empty()) error("tensor literal must have at least one element");
    lit.span = start.merge(previous_.span);
    return make_expr(std::move(lit));
}
```

`parse_number_row` factors the existing optional-minus + INT/FLOAT element loop so both the 1-D and per-row paths share it.

## 4. Acceptance tests

### `tests/test_nd_literals.cpp` (unit)

1. **2-D shape + row-major bytes:** `tensor([[1,2,3],[4,5,6]])` → captured tensor `ndim==2`, `shape=={2,3}`, contiguous, data `{1,2,3,4,5,6}`.
2. **2-D with negatives/floats:** `tensor([[-1.0, 2.0],[3.0, -4.0]])` → shape `{2,2}`, data `{-1,2,3,-4}`.
3. **1-D still works:** `tensor([1,2,3])` → `ndim==1`, `shape=={3}` (regression).
4. **Ragged rejected:** `tensor([[1,2],[3]])` → `parser.had_error()`, message mentions "ragged", and parsing terminates (no hang — implicit via the test completing).
5. **3-D rejected:** `tensor([[[1]]])` → `had_error()`, message mentions "1-D and 2-D".
6. **Empty rejected:** `tensor([])` and `tensor([[]])` → `had_error()`.
7. **`matmul` from source:** `matmul(tensor([[1,2,3],[4,5,6]]), tensor([[1,2],[3,4],[5,6]]))` → captured `[2,2]` tensor `{22,28,49,64}` (same values as the runtime's own matmul test).
8. **`matmul` shape mismatch** (inner dims disagree) surfaces the runtime's `RuntimeError` diagnostic + throw (as spec 010).

### `tests/test_phase0_e2e.cpp` (integration, extended)

9. A linear-layer shape end-to-end: `matmul(tensor([[1.0, 2.0]]), tensor([[1.0,2.0,3.0],[4.0,5.0,6.0]]))` → `[1,3]` with values `{9, 12, 15}`, through a user function.

All 22 pre-existing test binaries pass unchanged.

## 5. Out of scope

- **3-D and higher literals.** Rejected with a clear error. General N-D is a later spec if ever needed.
- **Non-rectangular / jagged tensors.** Rejected (ragged).
- **`matmul` for non-2-D operands / batched matmul.** The runtime is rank-2 only; mismatches surface as runtime diagnostics. No batching.
- **Broadcasting in `matmul`.** None.
- **Tensor-scalar broadcast** (e.g. `a * 2.0`) — that's spec 017.
- **Sema shape-checking of matmul conformance at compile time** — spec 018. For now inner-dim mismatch is a runtime error.
- **N-D reductions / per-axis ops.** Unrelated.
- **Reshaping source literals into other ranks.** No reshape syntax.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) scope to 1-D + 2-D only, rejecting 3-D+ explicitly — the capstone needs at most 2-D and a bounded parser is lower-risk than general N-D recursion; (b) `TensorLiteral` carries a flat `values` + a `shape` vector (mirrors `TENSOR_CONST_F32`'s `imm_floats`/`imm_shape`) rather than a nested structure; (c) factor `parse_number_row` so 1-D and per-row parsing share the optional-minus element loop and the spec-007 recovery discipline is preserved; (d) `matmul` reuses the existing `TENSOR_MATMUL` path — no new IR or interpreter code, just sema builtin + lowering dispatch.
- *Implementation* — One bug the tests caught immediately: the `threed_rejected` case (`tensor([[[1]]])`) **hung** under ctest. Root cause: the previous recovery (advance to `]`/`)`/`;`, then run the closing `consume()`s) left the cursor mid-brackets on deeply-malformed input, so the enclosing function-body loop re-entered `parse_stmt` with no forward progress → infinite loop. This is the "general no-progress" gap spec 007 had explicitly deferred; the new 3-D path is a fresh way to reach it. **Fix:** on any malformed literal, skip straight to the statement boundary (`;` / `}` / EOF) and return immediately, never running the closing consumes. This guarantees a clean resync point regardless of how the brackets are malformed, so the body loop always advances. Verified it does not regress spec 007's `test_parser_recovery` (1-D bad-token + multi-function recovery still green).
- *Implementation, verification* — `ctest` **23/23 passing** (new `ZeroNDLiteralsTest`; `test_phase0_e2e` extended with a `[1,2]·[2,3]→[1,3]` linear-layer case). 1-D literals unchanged; all 22 prior binaries green.
