# Spec 002: Source spans on IR instructions

**Status:** Implemented
**Depends on:** spec 001
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

[CLAUDE.md](../../CLAUDE.md) states: *"Every IR node carries a source span. No anonymous nodes."* Today every `ir::Instruction` is anonymous — the struct has no span field, and the `IRBuilder` does not thread one. This is a load-bearing gap: once we land spec 003 (real tensor ops) and the runtime returns a `Status::error`, we will have no way to point the diagnostic at the line of source that produced the failing op.

This spec closes the gap with the smallest possible change. We add one field to `Instruction`, two methods to `IRBuilder` (set / push-pop current span), make `emit` stamp the current span, and update the lowering pass to push the AST node's span around each lowering step. The dumper learns to print the span when present.

## 2. Invariants

- `ir::Instruction` has a public field `source::Span span;` initialised to `Span::invalid()` by default.
- `ir::IRBuilder` exposes:
  - `void set_current_span(source::Span s) noexcept` — sets the span stamped onto every subsequently-emitted instruction.
  - `source::Span current_span() const noexcept` — read accessor.
  - The current span is stamped onto every `Instruction` by `IRBuilder::emit`. No builder method, public or private, emits an instruction without going through `emit`.
- The `Span` default initial value is `Span::invalid()`. Instructions produced before any `set_current_span` call have an invalid span (this is fine — those are instructions synthesized without a clear source location, e.g., implicit `RET` at function end).
- After this spec, every `Instruction` emitted by the lowering pass for an AST node that has a `span` field carries that span. The lowering pass is responsible for calling `set_current_span` before invoking any `IRBuilder` method.
- The IR dumper (`ir::print_instruction`) renders the span as a trailing `; @file:line:col` comment when the span is valid, and omits the comment when the span is `invalid()`.
- No `Instruction` or `Span` field is renamed, reordered, or removed. The addition is purely additive — pre-existing test code that constructs `Instruction` literals continues to compile (designated-initialiser-style fields stay at the same names).

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — add `source::Span span = source::Span::invalid();` field to `Instruction`. Include `source/source.hpp` (transitively pulled in already by `types/types.hpp` but make it explicit). No other change.
2. `include/ir/builder.hpp` — add a private `source::Span current_span_ = source::Span::invalid();`, the two public `set_current_span` / `current_span` methods, and update `emit(Instruction)` to stamp the current span onto the instruction before pushing to the block.
3. `src/ir/ir.cpp` — update `print_instruction` to append `  ; @file:line:col` when `instr.span.valid()`. The trailing comment uses the source manager's `offset_to_line_col` if the file is loadable; otherwise prints `; @<source_id>:<offset>` as a fallback.
4. `src/ir/lowering.cpp` — at the top of each statement / expression visitor, call `builder.set_current_span(node.span)` before any IR is emitted. No changes to lowering logic; only span propagation.

No new headers. No new test framework. No new public symbols in any of `ir`, `types`, `source`, or `lowering` beyond those listed.

### Sketch — IRBuilder.emit

```cpp
private:
    source::Span current_span_ = source::Span::invalid();
    void emit(Instruction instr) {
        instr.span = current_span_;
        current_block_->add(std::move(instr));
    }
```

### Sketch — lowering, per visitor

```cpp
void lower_binary_expr(const ast::BinaryExpr& e) {
    builder.set_current_span(e.span);
    auto lhs = lower_expr(*e.lhs);
    auto rhs = lower_expr(*e.rhs);
    // builder.add() etc. all inherit the span until something else sets it.
}
```

The pattern is "set on entry to a visitor, child visitors overwrite when they enter." There is no stack discipline — the span is a simple stateful value. This is identical to how LLVM's `IRBuilder::SetCurrentDebugLocation` works and is the simplest design that works.

## 4. Acceptance tests

New test file: `tests/test_ir_spans.cpp`. Twelve assertions, organized as:

1. **Default-constructed `Instruction`** has `span.valid() == false`.
2. **`IRBuilder::current_span()` is invalid by default.**
3. **After `set_current_span(s)`, `current_span()` returns `s` bit-for-bit.**
4. **An `Instruction` emitted after `set_current_span(s)` has `span == s`** (verified by inspecting the block's tail instruction).
5. **A second `set_current_span(s2)` overwrites cleanly**; subsequent instructions get `s2`.
6. **An instruction emitted before any `set_current_span` call** has an invalid span (regression guard against accidental default mutation).
7. **`print_instruction` on an instruction with an invalid span** does NOT include the `; @` comment.
8. **`print_instruction` on an instruction with a valid span** does include a `; @` comment containing the span's source_id and offsets.

End-to-end via the lowering pass:

9. **A `let x = 1 + 2` lowered through `lower_module`** produces an `ADD` instruction whose `span` is the span of the `BinaryExpr` in the AST (verified by source-id + offset match).
10. **A subsequent `let y = x + 3` produces an `ADD` whose span differs** from the first one (proves the lowering does not leak spans across statements).

The pre-existing `tests/test_ir.cpp` must continue to pass without modification. (Defaulting to invalid spans means existing code that constructs `Instruction` literals is undisturbed.)

## 5. Out of scope

- **Spans on `Function`, `BasicBlock`, `Value`, or `Module`.** Function and block-level spans are useful but not the contract gap CLAUDE.md called out. A later spec may add them.
- **Span propagation through optimization passes** (DCE, fold, …). No optimization passes exist yet; when they land, each pass's spec will state how it preserves or merges spans.
- **Multi-span / "this op was synthesized from these N AST nodes" attribution.** Single-span only in v1.
- **Source-aware error rendering** (e.g., printing the actual source line for a failed IR op). That belongs in spec 003's diagnostic path — this spec only ensures the *data* is on every instruction.
- **Changes to AST `Span` fields.** AST nodes already carry spans. We consume them, we do not extend them.
- **Test framework changes.** Custom `ASSERT` macro pattern, no gtest.
- **Modifying any other compiler stage** — lexer, parser, sema, interpreter, driver, runtime adapter. Strictly IR + lowering.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) stateful "current span" on the builder rather than a span parameter on every method, matching LLVM's `IRBuilder::SetCurrentDebugLocation` model — far fewer signature changes and the lowering pass is naturally tree-recursive so the state is well-scoped; (b) span field defaults to `Span::invalid()` so existing instruction-literal construction in tests is undisturbed; (c) dumper format chosen as `; @file:line:col` for valid spans, falling back to `; @<source_id>:<offset>` if the source can't be resolved — keeps the IR text readable while still being machine-parseable; (d) no span propagation across optimization passes since no passes exist yet (intentionally deferred to whichever spec introduces the first pass).
- *Implementation* — Two refinements:
  - **Dumper format simplified.** The runtime SourceManager isn't available to `print_instruction`, so resolving to `file:line:col` would require threading it through. Defer that to spec 003+ (the diagnostic path) and emit `; @<source_id>:<start_offset>-<end_offset>` here — uniformly machine-parseable, no SourceManager dependency. The spec's §4 acceptance test for the dumper was updated to look for this format.
  - **Implicit RET span explicitly reset to invalid.** `lower_function` resets the builder's current span to `Span::invalid()` immediately before emitting the synthesized return, so the last user statement's span doesn't leak onto a compiler-synthesized op.
- *Implementation, verification* — `cmake --build` clean. `ctest` 12/12 passing: the new `ZeroIRSpansTest` with 10 assertions, plus all 11 pre-existing test binaries unchanged (proves the change is strictly additive at the source level).
