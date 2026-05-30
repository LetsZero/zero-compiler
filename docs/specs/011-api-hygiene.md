# Spec 011: API hygiene — remove dead opcode, silence duplicate-link warning

**Status:** Implemented
**Depends on:** none
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler
**ABI note:** internal compiler only; no language or runtime surface change.

---

## 1. Goal

Two small, zero-behaviour-change hygiene items from `docs/DEFERRED.md`, grouped because they share one risk profile (no observable behaviour change, pure cleanup):

1. **Delete `OpCode::TENSOR_ALLOC`.** It is unused — `TENSOR_CONST_F32` (spec 003) covers tensor construction, nothing emits `TENSOR_ALLOC`, and the interpreter only has a stub case returning `nullptr`. Dead code in a load-bearing enum invites mis-wiring later.

2. **Silence the duplicate-library link warning.** Six test targets link `zerobackend zeroparse zerosema`, but `zerosema` already `PUBLIC`-links `zeroparse`, so the explicit `zeroparse` is redundant and produces `ld: warning: ignoring duplicate libraries: '../lib/libzeroparse.a'` on every build. Drop the redundant token.

The `zero::ir::` → `zero::compiler::ir` rename, the third API-hygiene item in `DEFERRED.md`, is **deliberately not in this spec** — see §5.

## 2. Invariants

- `OpCode::TENSOR_ALLOC` no longer exists in the `OpCode` enum, `opcode_name`, or the interpreter's `exec_instruction` switch.
- No source file references `TENSOR_ALLOC` after this change (verified by grep).
- The remaining `OpCode` enumerators keep working; the enum has no gaps that matter (it is not serialized by ordinal anywhere — values are referenced by name only, and the interpreter switches on names).
- No test or behaviour changes as a result of removing `TENSOR_ALLOC` (it was never emitted by lowering, so no IR ever contained it).
- The six test targets that currently say `zerobackend zeroparse zerosema` now say `zerobackend zerosema` (or equivalent), and `zerosema`'s `PUBLIC` link to `zeroparse` continues to supply the parser symbols/headers transitively.
- A clean build emits **no** `ignoring duplicate libraries: '../lib/libzeroparse.a'` warning.
- All 20 pre-existing test binaries continue to build and pass without source changes.

## 3. API surface

Files modified:

1. `include/ir/ir.hpp` — remove the `TENSOR_ALLOC,` enumerator and its `case OpCode::TENSOR_ALLOC: return "tensor.alloc";` line in `opcode_name`.
2. `src/backend/interpreter.cpp` — remove the `case OpCode::TENSOR_ALLOC:` arm (it currently shares the stubbed `nullptr` return; deleting the label is sufficient since no other code produces the opcode).
3. `tests/CMakeLists.txt` — on the six lines linking `zerobackend zeroparse zerosema`, remove the redundant `zeroparse` (the targets: `test_source_tensor`, `test_remaining_tensor_ops`, `test_function_tensor_io`, `test_interpreter_recursion`, `test_control_flow`, `test_runtime_diagnostics`).

No header API changes beyond the enum. No new symbols. No lexer/parser/sema/lowering/runtime changes.

## 4. Acceptance tests

This spec is verified by the build and the existing suite, not a new test binary (there is no new behaviour to assert — both changes are removals of dead/redundant material):

1. **No `TENSOR_ALLOC` references remain.** `grep -rn TENSOR_ALLOC include src tests` returns nothing.
2. **Clean build, no duplicate-link warning.** `cmake --build build` output contains no `ignoring duplicate libraries` line.
3. **Full suite green.** `ctest` — all 20 pre-existing test binaries pass, unmodified.

A dedicated test file is intentionally **not** added; adding a test that asserts "an enum value does not exist" or "a linker warning is absent" would be noise. The grep + build + existing suite are the right verification surface.

## 5. Out of scope

- **The `zero::ir::` → `zero::compiler::ir` rename.** Deliberately excluded. It is a large mechanical change across most of the compiler tree, and its sole benefit is allowing the runtime umbrella header `<zero/zero.hpp>` to be included without colliding with the compiler's own `zero::ir`. The narrow-include workaround (spec 003) already handles this cleanly and is documented. Doing the rename now is churn without payoff. This spec **downgrades** that item in `DEFERRED.md` from "to do" to "won't do unless a concrete need for the umbrella include arises."
- **gtest migration.** Separate tooling item.
- **CI for the compiler repo.** Separate, higher-value tooling item — recommended as the next thing after this.
- **Renumbering or compacting the `OpCode` enum.** We remove `TENSOR_ALLOC` in place; we do not renumber the others or add explicit values. The enum is name-referenced only, so ordinal stability is irrelevant.
- **Auditing other potentially-dead opcodes.** Only `TENSOR_ALLOC` is confirmed dead and addressed here.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) grouped exactly two items, both zero-behaviour-change, into one spec (the themed-cleanup pattern); (b) excluded the namespace rename and downgraded it in `DEFERRED.md` rather than bundling a large risky change with two trivial ones; (c) no new test binary — grep + clean-build + existing suite is the correct verification for pure removals.
- *Implementation, verification* — Clean-from-scratch build (`rm -rf build`) emits **0** `ignoring duplicate libraries` warnings (was one per affected target). `grep -rn TENSOR_ALLOC include src tests` returns nothing. `ctest` **20/20 passing**, all binaries unmodified. `OpCode` left un-renumbered (name-referenced only, ordinal irrelevant).
