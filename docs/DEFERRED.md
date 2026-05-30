# Deferred items

> Things we deliberately did not do, with enough context to pick them up later. Not a wishlist — these are real items surfaced by past specs that we chose not to fix in the moment.

This file is append-only. When an item is fixed, the entry stays (with a strikethrough and a pointer to the spec that resolved it). Don't delete entries.

---

## Robustness

- **Newlines inside `(...)` and `[...]` are not insignificant.** A multi-line expression such as `matmul(a,\n  b)` or a 2-D literal split across lines fails to parse: the argument/element loops don't `skip_newlines()`, so a NEWLINE token where an argument/element is expected causes an error. **Combined with the no-progress gap below, this currently HANGS** (surfaced by spec 018's e2e test, which had to be rewritten single-line). Fix: skip newlines inside bracketed/parenthesised expression contexts (arg lists, literal rows). Idiomatic multi-line ML code (weight matrices) needs this — worth a dedicated spec, ideally before the spec-019 capstone for readability.

- **General no-progress guard in statement/program loops.** The function-body `while (!check(RBRACE) && !eof)` loop (and the if/while-body and program loops) call `parse_stmt` repeatedly; if `parse_stmt` returns without advancing the cursor (which happens on certain malformed input, e.g. a NEWLINE mid-expression per the item above), the loop spins forever. Spec 007 deferred this "general no-progress guard"; it has now bitten three times (spec 005, spec 015's 3-D literal, spec 018's multi-line expr). Robust fix: in each such loop, record the token index before `parse_stmt`; if unchanged after, force `advance()` (or `synchronize()`), guaranteeing progress. This is defense-in-depth that makes the parser hang-proof regardless of input. Pair it with the newline fix above in one parser-robustness spec.

- ~~**Parser error-recovery loop on bad tensor literal contents.**~~ **Resolved by spec 007.** The tensor-literal bracket loop now synchronises to `RBRACKET`/`RPAREN`/`SEMICOLON`/`EOF` after any error, with a 1 s wall-clock test budget to catch regressions. The broader "audit every parser.error() site" sweep remains a future hardening pass — note it if a new hang surfaces.

- ~~**Interpreter call-stack reference invalidation.**~~ **Resolved by spec 008.** The interpreter now addresses its frame by stable index (`call_stack_[my_frame_idx]`) instead of by reference. The `reserve(1024)` hack is gone.

- ~~**`IRBuilder` reference invalidation under `create_block`.**~~ **Resolved by spec 009.** `IRBuilder` now addresses the current block by `uint32_t` id (== index into `fn.blocks`), and `lower_if`/`lower_while` hold ids instead of `BasicBlock&`. `emit()` re-derives the destination block from the id every call. The id==index invariant is documented on `Function` and asserted in `new_block`. The recursive-Fibonacci case that exposed it now returns the correct value, and an IR-level test guards that branch blocks are non-empty.

- ~~**`Reporter`-based diagnostics for runtime errors.**~~ **Resolved by spec 010.** The interpreter now takes an optional `SourceManager*`; on a tensor-op failure with a valid span it emits a Frame & Focus `RuntimeError` diagnostic (source line, focus reason, help) before throwing. The throw is unchanged (additive integration). The driver wires the SourceManager so `zeroc` gets rich runtime errors. (Note: the interpreter is still throw-based for control flow — migrating `execute()` to return a `Status`/`Result` remains a separate, larger item if ever wanted.)

## API hygiene

- **`zero::ir::` namespace collision between the compiler and the runtime.** Both repos define `Function` and `BasicBlock` in `namespace zero::ir`. Spec 003 worked around it by switching the interpreter from `<zero/zero.hpp>` to narrow `<zero/core/*.hpp>` and `<zero/ops/*.hpp>` includes. The umbrella header is unsafe to include anywhere in the compiler tree.
  - **Status: WON'T DO unless needed (downgraded by spec 011).** The rename to `zero::compiler::ir` touches most of the compiler tree, and its only payoff is enabling the umbrella include — which the narrow-include workaround already handles cleanly. Doing it now is churn without payoff. Revisit only if a concrete need for `<zero/zero.hpp>` in the compiler arises.

- ~~**`OpCode::TENSOR_ALLOC` is unused.**~~ **Resolved by spec 011.** Removed from the enum, `opcode_name`, and the interpreter switch. It was never emitted by lowering, so no IR ever contained it.

- ~~**Duplicate-library link warning.**~~ **Resolved by spec 011.** The six test targets that linked `zerobackend zeroparse zerosema` now link `zerobackend zerosema` (zerosema `PUBLIC`-links zeroparse). A clean build emits zero `ignoring duplicate libraries` warnings.

## Compiler features deferred

- **2-D (and N-D) tensor literals.** Spec 004 limited to 1-D. Required to make `matmul` reachable from source (currently wired but only IR-constructable). Suggested syntax: `tensor([[1, 2], [3, 4]])` with nested brackets — parser recurses.

- **Other elementwise tensor ops.** The runtime supports `exp`, `log`, `sqrt`, `tanh`, `sigmoid`, `abs`, `sin`, `cos`. No IR opcodes for these yet. Mechanical to add (same pattern as spec 005's `TENSOR_NEG`/`TENSOR_DIV`).

- **Tensor reductions.** `sum`, `mean`, `max`, `argmax` in the runtime. New op family with different shape semantics (output rank = input rank - 1). Needs its own spec.

- **Tensor indexing and gather/scatter from source.** The runtime ops exist (spec 004 of the runtime). No IR opcode, no source syntax. Significant new spec.

- **Tensor-scalar broadcast at the language level.** The runtime supports `b.numel() == 1` broadcast, but the compiler doesn't construct a rank-0 tensor from a scalar literal. So `a + 2.0` (tensor + scalar) fails. Either construct a `tensor([2.0])` automatically in lowering, or add a dedicated `TENSOR_SCALAR_*` family.

- **Sema type-checking of tensor ops.** Shape mismatches surface only at runtime via `Status`. A future spec should make sema reject `tensor([1,2]) + tensor([1,2,3])` at compile time when shapes are statically known.

- **`Generator` (RNG) integration.** The runtime exposes `Generator` (spec 005 of the runtime). No compiler-side wiring. Needed for dropout, init, sampling.

## Robustness (continued)

- ~~**Linux-only abort in `ZeroSourceTensorTest` and `ZeroFunctionTensorIOTest`.**~~ **Resolved by spec 013.** Root cause confirmed: static-destruction of the `static RuntimeValue captured`'s `TensorPtr` ran its `free()`-ing deleter at process exit, which libstdc++ (Linux) and libc++ (macOS) handle differently. Fix: reset `captured` before `main` returns so no deleter runs during static destruction. CI now green on both `ubuntu-latest` and `macos-latest`.

## Build / tooling

- **Optional `gtest` upgrade.** Test files use a custom `TEST()` macro and `assert()`. Workable but unfriendly when a test fails (no diff display, no per-assertion location). When the test count crosses ~50, consider migrating to gtest.

- ~~**CI for the compiler repo.**~~ **Resolved by spec 012.** `.github/workflows/ci.yml` builds + runs the full ctest suite on every push to main and every PR, across ubuntu-latest and macos-latest, with recursive submodule checkout.

- **Sanitizer axis in compiler CI.** Spec 012 ships an OS-only matrix. The compiler's top-level CMake doesn't wire ASAN/UBSAN/TSAN compile options for its own targets (the runtime submodule has them, but the flags don't propagate to compiler-owned libs). Adding `ZERO_*_SANITIZER`-style options to the compiler CMake and then a sanitizer axis to the CI matrix would catch memory bugs like the ones specs 008/009 fixed *automatically*. Worth doing given how many reference-invalidation bugs this codebase has had.

- **Windows / MSVC CI.** The compiler has only been built with AppleClang (and, via CI, ubuntu gcc). MSVC support is untested. Add when there's a concrete need.
