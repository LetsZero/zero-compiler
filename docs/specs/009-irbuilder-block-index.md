# Spec 009: IRBuilder block-index refactor (control-flow miscompile fix)

**Status:** Implemented
**Depends on:** spec 002 (spans), spec 006 (function I/O), spec 008 (sibling fix in the interpreter)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

Fix a silent miscompile of **every** `if` and `while`. `IRBuilder` holds `BasicBlock* current_block_`, and `lower_if`/`lower_while` hold `BasicBlock&` references into `fn.blocks` (a `std::vector<BasicBlock>`). The moment a *second* `create_block()` does `push_back` and the vector reallocates, every previously-held block pointer/reference dangles. Subsequent `emit()` and `set_insert_point()` calls then write into freed memory, and the affected blocks come out **empty** in the final IR.

This was surfaced by spec 008's recursive-Fibonacci probe: `fn check(n: int) -> int { if (n < 2) { return 99; } return 7; }` returns `7` for `n = 0`, because the `then` branch's `return 99` instruction was emitted into a dangling block and never reached the IR.

The fix mirrors spec 008's interpreter fix: address blocks by **id/index** rather than by pointer/reference. This is sound because the codebase already maintains **block id == index into `fn.blocks`** — `Function::new_block` sets `bb.id = next_block_id++` in lockstep with `push_back`, and the interpreter already indexes `fn.blocks[instr.target_block]` by block id.

## 2. Invariants

- **Block id == index into `fn.blocks`** is documented as a load-bearing invariant of `Function`, and asserted in `new_block` under `NDEBUG`-guarded `assert`.
- `IRBuilder` holds `uint32_t current_block_id_` instead of `BasicBlock* current_block_`. No raw `BasicBlock*`/`BasicBlock&` is retained across any call that can append to `fn.blocks`.
- `IRBuilder::emit` writes to `fn_.blocks[current_block_id_]`, re-derived from the index every call — never a cached reference.
- `IRBuilder::create_block(label)` returns `uint32_t` (the new block's id).
- `IRBuilder::set_insert_point(uint32_t block_id)` sets the insert point by id.
- `IRBuilder::br(uint32_t target_id)` and `cond_br(Value, uint32_t then_id, uint32_t else_id)` take ids.
- `lower_if` and `lower_while` hold `uint32_t` block ids, never `BasicBlock&`, across their `create_block` calls.
- After this fix, control flow lowers correctly:
  - `if (cond) { A } else { B }` produces a `then` block containing A's instructions, an `else` block containing B's, and a `merge` block — none empty when the source branch is non-empty.
  - The recursive-Fibonacci program (pulled from spec 008) now returns the correct value.
- All 18 pre-existing test binaries continue to pass without modification.
- `Function::new_block` and `Function::entry` keep returning `BasicBlock&` — they are only ever used for *immediate* access (e.g. `test_ir.cpp`), never held across a subsequent `create_block`. Not changed.

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — document the id==index invariant on `Function`; add a debug `assert(bb.id == blocks.size())` in `new_block` *before* the `push_back`.
2. `include/ir/builder.hpp`:
   - `current_block_` field: `BasicBlock*` → `uint32_t current_block_id_`.
   - Constructor: `current_block_id_(fn.entry().id)`.
   - `set_insert_point(BasicBlock&)` → `set_insert_point(uint32_t block_id)`.
   - `current_block()` accessor → `current_block_id()` returning `uint32_t`.
   - `create_block(label)` returns `uint32_t`.
   - `br(BasicBlock&)` → `br(uint32_t target_id)`.
   - `cond_br(Value, BasicBlock&, BasicBlock&)` → `cond_br(Value, uint32_t then_id, uint32_t else_id)`.
   - `emit`: `current_block_->add(...)` → `fn_.blocks[current_block_id_].add(...)`.
3. `src/ir/lowering.cpp` — `lower_if` and `lower_while` use `uint32_t` ids returned by `create_block` and pass them to `set_insert_point`/`br`/`cond_br`.

Files added:

4. `tests/test_control_flow.cpp` — control-flow lowering + execution tests, including the recursive-Fibonacci case carried over from spec 008.

No changes to the lexer, parser, sema, AST, runtime, interpreter (its sibling fix already shipped in spec 008), or the diagnostics layer.

### Sketch — IRBuilder

```cpp
// field
uint32_t current_block_id_ = 0;

IRBuilder(Function& fn) : fn_(fn), current_block_id_(fn.entry().id) {}

void set_insert_point(uint32_t block_id) { current_block_id_ = block_id; }
uint32_t current_block_id() const { return current_block_id_; }

uint32_t create_block(const std::string& label = "") {
    return fn_.new_block(label).id;
}

void br(uint32_t target_id) {
    Instruction instr;
    instr.op = OpCode::BR;
    instr.target_block = target_id;
    emit(instr);
}

void cond_br(Value cond, uint32_t then_id, uint32_t else_id) {
    Instruction instr;
    instr.op = OpCode::COND_BR;
    instr.operands = {cond};
    instr.target_block = then_id;
    instr.else_block = else_id;
    emit(instr);
}

private:
void emit(Instruction instr) {
    instr.span = current_span_;
    fn_.blocks[current_block_id_].add(std::move(instr));
}
```

### Sketch — lower_if

```cpp
void Lowering::lower_if(IRBuilder& builder, ast::IfStmt& if_stmt) {
    builder.set_current_span(if_stmt.span);
    Value cond = if_stmt.condition ? lower_expr(builder, *if_stmt.condition) : Value{};

    uint32_t then_id  = builder.create_block("if.then");
    uint32_t merge_id = builder.create_block("if.end");

    builder.set_current_span(if_stmt.span);
    if (if_stmt.else_branch.empty()) {
        builder.cond_br(cond, then_id, merge_id);
    } else {
        uint32_t else_id = builder.create_block("if.else");
        builder.cond_br(cond, then_id, else_id);
        builder.set_insert_point(else_id);
        for (auto& s : if_stmt.else_branch) lower_stmt(builder, *s);
        builder.set_current_span(if_stmt.span);
        builder.br(merge_id);
    }

    builder.set_insert_point(then_id);
    for (auto& s : if_stmt.then_branch) lower_stmt(builder, *s);
    builder.set_current_span(if_stmt.span);
    builder.br(merge_id);

    builder.set_insert_point(merge_id);
}
```

Note the ordering subtlety this fix *also* corrects: `create_block` for both `then` and `merge` happens **before** any `set_insert_point`, so all block ids are allocated up front and no id shifts under us. The old reference-based code couldn't do this safely; the index-based version can.

## 4. Acceptance tests

New test file: `tests/test_control_flow.cpp`. End-to-end source → execute via `capture`.

1. **if-true branch executes.** `fn f(n: int) -> int { if (n < 2) { return 99; } return 7; }`, `f(0)` → `99`. (The exact case that was wrong before this fix.)
2. **if-false branch falls through.** Same `f`, `f(5)` → `7`.
3. **if/else both branches.** `fn g(n: int) -> int { if (n < 0) { return 1; } else { return 2; } }`; `g(-1)` → `1`, `g(3)` → `2`.
4. **while loop runs.** `fn sum_to(n: int) -> int { let s = 0; let i = 0; while (i < n) { ... } return s; }` — a loop that accumulates; verify the result for a small `n`. (If the language lacks assignment-in-place needed for a real accumulator, fall back to a loop whose body is observable via a simpler mechanism; the test must at minimum prove the while body block is non-empty and the loop terminates.)
5. **Recursive Fibonacci** (carried from spec 008). `fn fib(n: int) -> int { if (n < 2) { return n; } return fib(n-1) + fib(n-2); }`, `fib(8)` → `21`.
6. **IR-level: no empty branch blocks.** Lower an `if` with non-empty branches and assert (by walking the module) that the `then`/`else` blocks contain at least one instruction each. This is the direct regression guard for the dangling-block bug.
7. **No regressions.** All 18 pre-existing test binaries pass with zero source changes.

## 5. Out of scope

- **Interpreter changes.** Its sibling fix already shipped in spec 008. This spec only touches IR construction.
- **`Function::new_block` / `entry` signatures.** They keep returning `BasicBlock&` for immediate use. Not changed.
- **A general `BlockHandle` wrapper type.** A raw `uint32_t` id is sufficient and matches the existing `instr.target_block` representation. No new type introduced.
- **Block removal / dead-block elimination.** No pass removes blocks, so the id==index invariant holds trivially. If a future pass needs to remove blocks, it must preserve the invariant (e.g. tombstone, not erase) or this design must be revisited — noted, not addressed.
- **`while`-loop language completeness.** If the accumulator pattern needs mutable assignment the language doesn't yet support, the while test is written to the weaker bar in §4.4. Full loop semantics are a separate concern.
- **Reporter integration.** Still deferred.
- **Span correctness audit for control flow.** Spans are set as before; this spec does not re-audit them.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) address blocks by `uint32_t` id, not a new handle type — matches `instr.target_block` and the id==index invariant the interpreter already relies on; (b) the invariant is asserted in `new_block` under debug builds rather than enforced by a heavier structure; (c) `Function::new_block`/`entry` keep returning references (immediate-use only); (d) the recursive-Fibonacci test that surfaced the bug in spec 008 lands here as the headline regression test, plus a direct "branch blocks are non-empty" IR-level assertion.
- *Implementation, verification* — `ctest` **19/19 passing**. The refactor compiled clean across all consumers on the first build (the only external block-API user, `test_ir.cpp`, uses `Function::new_block` for immediate access, which was left unchanged). New `ZeroControlFlowTest`: if-true executes (the previously-wrong case → 99), if-false falls through (→ 7), if/else both branches, while-body block non-empty + terminates, recursive `fib(8)` → 21, and an IR-level guard that `if.then`/`if.else` blocks are non-empty. The spec-008 recursion test's placeholder note was updated to point here. `docs/DEFERRED.md` marks the IRBuilder item resolved.
