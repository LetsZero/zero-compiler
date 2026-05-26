# Spec 007: Parser error-recovery hardening

**Status:** Implemented
**Depends on:** none (touches parser only)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

Stop the parser from hanging when a tensor literal contains an unexpected token. The bug surfaced twice already — spec 005's missing `MINUS` handler caused a 255 s `ctest` hang; spec 006's missing `parse_type()` case for the `tensor` keyword caused a 339 s hang. Each time the *trigger* was patched, but the underlying defect — the bracket-element loop in `parse_primary`'s tensor literal branch doesn't advance the cursor after `error()` — stays in place. The next new token class we introduce will hit the same trap.

This spec fixes it once. The bracket-element loop synchronises to a recovery point (`RBRACKET`, `RPAREN`, `SEMICOLON`, or `EOF`) on any parse error, so the cursor always advances and higher-level loops can make progress.

## 2. Invariants

- After encountering an unexpected token inside `tensor([ ... ])`, the parser:
  1. Reports the error exactly once (via `error()`).
  2. Advances the cursor past the bad token(s) until it reaches one of `RBRACKET`, `RPAREN`, `SEMICOLON`, or `EOF`.
  3. Continues parsing the surrounding program (no hang, no infinite loop, no crash).
- `parser.had_error()` returns `true` after such an input.
- The parser does **not** invent valid AST for the bad literal: the resulting `TensorLiteral` may be empty or have only the values parsed before the bad token, and that's fine — the test verifies the error is reported and the parser terminates, not that recovery is semantically perfect.
- The fix is local to the tensor-literal branch in `parse_primary`. No changes to `synchronize()`, no changes to other parser functions, no changes to `error()` semantics.
- All 16 pre-existing test binaries continue to pass without modification.

## 3. API surface

Files modified:

1. `src/parser/parser.cpp` — the tensor-literal branch in `parse_primary` gets a small recovery routine that, after `error("Expected numeric literal …")` and `break`, consumes tokens until the next `RBRACKET`, `RPAREN`, `SEMICOLON`, or `EOF`. The existing `consume(RBRACKET, …)` / `consume(RPAREN, …)` calls after the loop then succeed (or report cleanly if the cursor is at EOF).

Files added:

2. `tests/test_parser_recovery.cpp` — three small acceptance tests; see §4.

No header changes. No new public symbols.

### Sketch

```cpp
// Inside parse_primary's TENSOR branch, after the existing element loop:

if (parser.had_error()) {
    // Recovery: advance past tokens until we hit a known stopping point.
    while (!current_.is_eof()
           && !check(TokenType::RBRACKET)
           && !check(TokenType::RPAREN)
           && !check(TokenType::SEMICOLON)) {
        advance();
    }
}
```

Placed *between* the inner for-loop and the `consume(RBRACKET, …)` so the subsequent `consume` calls find their expected tokens (or fail cleanly at EOF). The existing `error()` calls inside `consume` are no-ops when `panic_mode_` is true, so a single error is reported even if multiple `consume` calls fail along the way.

The existing `error("Tensor literal must have at least one element")` check at the end of the branch is left in place — it correctly fires when the element list is empty after recovery.

## 4. Acceptance tests

New test file: `tests/test_parser_recovery.cpp`.

1. **Unrecognised token inside brackets does not hang.** Source: `fn main() { let a = tensor([1, "x", 2]); }`. The test runs the parser and asserts:
   - `parser.had_error() == true`.
   - The parser returned (no timeout). The test wraps `parser.parse()` in a hard-coded budget check: if it takes more than 1 second, the test fails. (Practically, the fix makes it return in microseconds; the budget exists to catch a future regression.)

2. **Garbled bracket contents still let the rest of the file parse.** Source:
   ```
   fn main() { let a = tensor([1, ?, 3]); }
   fn other() { let b = 7; }
   ```
   Assert: `had_error() == true`, but `program.functions.size() == 2` (both `main` and `other` were parsed; the parser recovered enough to see the second function).

3. **Pre-existing inputs still work.** A well-formed `tensor([1.0, 2.0, 3.0])` parses cleanly with `had_error() == false`.

4. **No regressions.** All 16 pre-existing test binaries pass with zero source changes.

## 5. Out of scope

- **General no-progress guard in `parse_fn_decl`'s body loop.** Would be a stronger systemic defence (catch the same class of bug at *any* parse site, not just tensor literals), but it changes the parser's iteration shape and could mask other bugs. Defer to a separate spec if a different parse site exhibits the same hang pattern in the future.
- **Reporter integration for parse errors.** Errors still flow through the existing `error()` path. Tracked in `docs/DEFERRED.md`.
- **Improved error messages for the recovery case.** The error reported is the original "Expected numeric literal …" (or whatever the first error was). Synthesising a more user-friendly "tensor literal contains invalid tokens" message is its own concern.
- **`synchronize()` augmentation.** The existing `synchronize()` syncs at the statement boundary on `FN`/`LET`/`IF`/`WHILE`/`RETURN`. We don't extend it — the local fix in the bracket loop is sufficient.
- **Other potentially-loopy parse sites.** This spec fixes the tensor-literal bracket loop only. If another site hangs in the future, fix it then with the same pattern.
- **Changing the empty-list error.** `tensor([])` still produces "Tensor literal must have at least one element" — the recovery path may also reach the same state with no elements, and that's fine.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) recovery scope is local to the tensor-literal branch, not a global change to `error()` or `synchronize()`; (b) synchronisation points are `RBRACKET` / `RPAREN` / `SEMICOLON` / `EOF` (not `RBRACE`) — the brace is too coarse and would skip past the enclosing statement; (c) the test asserts a wall-clock budget of 1 second to catch future regressions of the same hang pattern; (d) deliberately *not* adding a general no-progress guard in the function body loop — saves it for a future spec if a different site triggers the same defect.
- *Implementation, verification* — `ctest` **17/17 passing**. The new `ZeroParserRecoveryTest` has four cases: bad-token-in-brackets (the previously-hanging pattern), garbled-brackets-don't-block-other-functions, well-formed input still parses cleanly, and a regression guard for spec 005's negative-literal repro. Each one enforces a 1 s wall-clock budget. All 16 pre-existing test binaries pass unmodified.
