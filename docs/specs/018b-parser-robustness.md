# Spec 018b: Parser robustness — newlines in brackets + no-progress guard

**Status:** Implemented
**Depends on:** spec 007 (recovery), spec 015 (literal recovery)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler
**Roadmap:** Phase 0, item 018b (inserted; surfaced by spec 018).

---

## 1. Goal

Two related parser defects, fixed together because they compound into a hang:

1. **Newlines inside `(...)` and `[...]` are significant.** A multi-line expression — `matmul(a,\n b)`, or a 2-D literal split across rows — fails to parse, because the argument-list, group, and tensor-literal parsers don't skip `NEWLINE` tokens. Idiomatic ML code (weight matrices written row-per-line) is exactly this shape. Fix: treat newlines as insignificant inside bracketed/parenthesised expression contexts.

2. **No-progress infinite loop.** The five statement-body loops (`fn` body, `if`-then, `if`-else, `while` body, block) call `parse_stmt` in a loop; `consume()` does **not** advance the cursor on a mismatch, so on certain malformed input `parse_stmt` can return without consuming anything and the loop spins forever. This is the "general no-progress guard" deferred since spec 007; it has now surfaced four times (specs 005, 015, 018, and the multi-line case above). Fix: guarantee forward progress in every statement-body loop.

Defect (1) is the trigger; defect (2) turns the trigger into a hang. Both are closed here so the parser is hang-proof on **any** input and multi-line expressions parse correctly — clearing the way for the spec-019 capstone to be written readably.

## 2. Invariants

- **Newline insignificance in expression brackets:** the following all parse without error (and produce the same AST as their single-line equivalents):
  - `matmul(a,\n b)` and any call with arguments spread across lines.
  - `tensor([[1, 2],\n [3, 4]])` — a 2-D literal with rows on separate lines.
  - `tensor([1,\n 2,\n 3])` — a 1-D literal across lines.
  - `(\n a + b\n )` — a parenthesised group across lines.
- Newline insignificance is **scoped to inside brackets/parens**. At statement level, newlines remain meaningful as today (statement separators); this spec does not change statement-level newline handling.
- **No-progress guard:** every statement-body loop makes forward progress each iteration. If one iteration of the loop fails to advance the cursor (cursor offset unchanged after `parse_stmt` + `skip_newlines`) and we are not at EOF, the parser force-advances one token. Consequently **no input — however malformed — causes the parser to hang.**
- The five statement-body loops are unified into a single helper `parse_stmt_block(out)` carrying the guard, so they cannot diverge.
- `had_error()` still reflects genuine syntax errors; the guard only affects *termination*, not whether errors are reported.
- All 26 pre-existing test binaries pass unchanged. The spec-007 recovery tests and spec-015 literal tests still hold (the guard and newline-skipping are additive).
- The spec-018 e2e test (currently forced single-line) **can be rewritten multi-line and pass** — used as a regression test here.

## 3. API surface

Files modified:

1. `include/parser/parser.hpp` — declare `void parse_stmt_block(std::vector<std::unique_ptr<ast::Stmt>>& out);`.
2. `src/parser/parser.cpp`:
   - **New** `parse_stmt_block`: the shared statement-body loop with the no-progress guard (`uint32_t before = current_.span.start_offset; … if (!eof && current_.span.start_offset == before) advance();`).
   - Replace the five inline body loops (`parse_fn_decl`, `parse_if_stmt` ×2, `parse_while_stmt`, `parse_block`) with `parse_stmt_block(container)`.
   - **Newline skipping** added in `parse_call` (after `(`, after each `,`, before `)`), in `parse_primary`'s group branch (after `(`, before `)`), in the tensor-literal branch (after `(`/`[`, after each row, before `]`/`)`), and in `parse_number_row` (at entry and after each `,`).
   - Optionally add the guard to the top-level program loop for uniformity (it already progresses via `parse_fn_decl`/`synchronize`, but the guard is cheap insurance).

Files added:

3. `tests/test_multiline.cpp` — spec-018b unit tests (multi-line parses; malformed input terminates within a wall-clock budget).

Files modified (tests):

4. `tests/test_phase0_e2e.cpp` — restore the spec-018 false-positive guard to **multi-line** form (now that it parses), as a regression test.

No lexer, sema, IR, lowering, or interpreter change.

### Sketch

```cpp
void Parser::parse_stmt_block(std::vector<std::unique_ptr<ast::Stmt>>& out) {
    while (!check(TokenType::RBRACE) && !current_.is_eof()) {
        const uint32_t before = current_.span.start_offset;
        auto stmt = parse_stmt();
        if (stmt) out.push_back(std::move(stmt));
        skip_newlines();
        // No-progress guard: prevents an infinite loop when parse_stmt
        // cannot consume the current (malformed) token.
        if (!current_.is_eof() && current_.span.start_offset == before) {
            advance();
        }
    }
}
```

## 4. Acceptance tests

### `tests/test_multiline.cpp` (unit)

Each parse runs under a 1-second wall-clock budget (the spec-007 hang-guard pattern).

1. **Multi-line call:** `matmul(\n tensor([[1,2,3],[4,5,6]]),\n tensor([[1,2],[3,4],[5,6]])\n)` parses with no error.
2. **Multi-line 2-D literal:** `tensor([[1, 2],\n [3, 4]])` parses, shape `[2,2]`, no error.
3. **Multi-line 1-D literal:** `tensor([1,\n 2,\n 3])` parses, shape `[3]`.
4. **Multi-line group:** `let x = (\n 1 + 2\n );` parses, no error.
5. **Multi-line == single-line AST:** the multi-line and single-line forms of the same matmul produce identical IR (compare dumped module text, or op counts).
6. **Malformed input terminates (no hang):** a pile of stray tokens inside a function body (e.g. `fn main() { @ # ] ) ; }`-style garbage, and the previously-hanging multi-line nested case) — `parse()` returns within the budget with `had_error()` true.
7. **Statement-level newlines unchanged:** a normal multi-statement function still parses as separate statements (regression).

### `tests/test_phase0_e2e.cpp` (integration)

8. The spec-018 `no_false_positive_shape_error` guard, **restored to multi-line source**, parses and analyzes with no error.

All 26 pre-existing test binaries pass unchanged.

## 5. Out of scope

- **Line-continuation / significant-indentation rules.** Statement-level newline semantics are unchanged; only bracket-interior newlines become insignificant.
- **Semicolon-optional statements / ASI.** Not touched.
- **Error-message quality improvements.** The guard force-advances silently after an error is already reported; producing a *better* message for the malformed token is separate.
- **A general expression-level recovery framework.** This spec fixes the concrete defects (bracket newlines + body-loop progress); a comprehensive recovery redesign is not attempted.
- **`synchronize()` changes.** Left as-is.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) unify the five identical statement-body loops into one `parse_stmt_block` helper carrying the guard, so they can't diverge and the fix lands in one place; (b) progress is measured by `current_.span.start_offset` (monotonic per token) — simplest reliable proxy without adding a lexer position counter; (c) newline-skipping scoped strictly to bracket/paren interiors, leaving statement-level newline semantics untouched; (d) restore the spec-018 e2e guard to multi-line as a regression test, closing the loop on what surfaced the bug.
- *Implementation, verification* — `ctest` **27/27**. The five body loops collapsed into `parse_stmt_block`; newline-skips added in `parse_call` (after `(`, each `,`, before `)`), the group branch, the tensor-literal branch (after `(`/`[`, each row, before `]`/`)`), and `parse_number_row`. New `ZeroMultilineTest` (7 cases): multi-line call / 2-D literal / 1-D literal / group all parse clean; multi-line vs single-line produce identical IR (op counts); malformed garbage terminates within a 1 s budget with `had_error()`; statement-level newlines unchanged. The spec-018 e2e softmax-over-linear program is now **multi-line** and analyzes clean. All 26 prior binaries unchanged. The four-time-recurring no-progress hang is now structurally impossible.
