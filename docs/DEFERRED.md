# Deferred items

> Things we deliberately did not do, with enough context to pick them up later. Not a wishlist — these are real items surfaced by past specs that we chose not to fix in the moment.

This file is append-only. When an item is fixed, the entry stays (with a strikethrough and a pointer to the spec that resolved it). Don't delete entries.

---

## Robustness

- ~~**Parser error-recovery loop on bad tensor literal contents.**~~ **Resolved by spec 007.** The tensor-literal bracket loop now synchronises to `RBRACKET`/`RPAREN`/`SEMICOLON`/`EOF` after any error, with a 1 s wall-clock test budget to catch regressions. The broader "audit every parser.error() site" sweep remains a future hardening pass — note it if a new hang surfaces.

- ~~**Interpreter call-stack reference invalidation.**~~ **Resolved by spec 008.** The interpreter now addresses its frame by stable index (`call_stack_[my_frame_idx]`) instead of by reference. The `reserve(1024)` hack is gone.

- ~~**`IRBuilder` reference invalidation under `create_block`.**~~ **Resolved by spec 009.** `IRBuilder` now addresses the current block by `uint32_t` id (== index into `fn.blocks`), and `lower_if`/`lower_while` hold ids instead of `BasicBlock&`. `emit()` re-derives the destination block from the id every call. The id==index invariant is documented on `Function` and asserted in `new_block`. The recursive-Fibonacci case that exposed it now returns the correct value, and an IR-level test guards that branch blocks are non-empty.

- ~~**`Reporter`-based diagnostics for runtime errors.**~~ **Resolved by spec 010.** The interpreter now takes an optional `SourceManager*`; on a tensor-op failure with a valid span it emits a Frame & Focus `RuntimeError` diagnostic (source line, focus reason, help) before throwing. The throw is unchanged (additive integration). The driver wires the SourceManager so `zeroc` gets rich runtime errors. (Note: the interpreter is still throw-based for control flow — migrating `execute()` to return a `Status`/`Result` remains a separate, larger item if ever wanted.)

## API hygiene

- **`zero::ir::` namespace collision between the compiler and the runtime.** Both repos define `Function` and `BasicBlock` in `namespace zero::ir`. Spec 003 worked around it by switching the interpreter from `<zero/zero.hpp>` to narrow `<zero/core/*.hpp>` and `<zero/ops/*.hpp>` includes. The umbrella header is unsafe to include anywhere in the compiler tree.
  - Suggested fix: rename the compiler's IR to `zero::compiler::ir`. Touches every file that says `namespace ir` or `zero::ir::` — significant but mechanical. Defer until a real use case wants the umbrella include.

- **`OpCode::TENSOR_ALLOC` is unused.** `TENSOR_CONST_F32` covers tensor construction; nothing emits or consumes `TENSOR_ALLOC`. Either delete or repurpose. Repurpose candidate: a no-data allocation (uninit output buffer) that the compiler can emit ahead of a `TENSOR_*` op so the buffer is explicit in IR instead of hidden inside the interpreter.

- **Duplicate-library link warning.** Several test targets show `ld: warning: ignoring duplicate libraries: '../lib/libzeroparse.a'` because they link both `zerosema` and `zeroparse` directly, and `zerosema` already pulls `zeroparse` transitively. Cosmetic; clean by removing the redundant direct link.

## Compiler features deferred

- **2-D (and N-D) tensor literals.** Spec 004 limited to 1-D. Required to make `matmul` reachable from source (currently wired but only IR-constructable). Suggested syntax: `tensor([[1, 2], [3, 4]])` with nested brackets — parser recurses.

- **Other elementwise tensor ops.** The runtime supports `exp`, `log`, `sqrt`, `tanh`, `sigmoid`, `abs`, `sin`, `cos`. No IR opcodes for these yet. Mechanical to add (same pattern as spec 005's `TENSOR_NEG`/`TENSOR_DIV`).

- **Tensor reductions.** `sum`, `mean`, `max`, `argmax` in the runtime. New op family with different shape semantics (output rank = input rank - 1). Needs its own spec.

- **Tensor indexing and gather/scatter from source.** The runtime ops exist (spec 004 of the runtime). No IR opcode, no source syntax. Significant new spec.

- **Tensor-scalar broadcast at the language level.** The runtime supports `b.numel() == 1` broadcast, but the compiler doesn't construct a rank-0 tensor from a scalar literal. So `a + 2.0` (tensor + scalar) fails. Either construct a `tensor([2.0])` automatically in lowering, or add a dedicated `TENSOR_SCALAR_*` family.

- **Sema type-checking of tensor ops.** Shape mismatches surface only at runtime via `Status`. A future spec should make sema reject `tensor([1,2]) + tensor([1,2,3])` at compile time when shapes are statically known.

- **`Generator` (RNG) integration.** The runtime exposes `Generator` (spec 005 of the runtime). No compiler-side wiring. Needed for dropout, init, sampling.

## Build / tooling

- **Optional `gtest` upgrade.** Test files use a custom `TEST()` macro and `assert()`. Workable but unfriendly when a test fails (no diff display, no per-assertion location). When the test count crosses ~50, consider migrating to gtest.

- **CI for the compiler repo.** The runtime has GitHub Actions (runtime spec 007). The compiler doesn't yet. Adding it is half a day of work and prevents the freeze-equivalent from rotting (the compiler isn't frozen, but regressions still happen).
