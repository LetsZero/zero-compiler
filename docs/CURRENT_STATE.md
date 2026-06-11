# Zero Compiler — Current State

> **Read this first if you are resuming the project cold.** It is the accurate
> "where are we right now" snapshot. For the plan and the per-feature history,
> see [ROADMAP.md](ROADMAP.md) and [docs/specs/](specs/).

**As of:** Phase 0 complete (spec 019) + robustness hardening + first Phase-1
substrate (assignment, CLI tensor output). CI green on Linux + macOS,
**31/31** tests.

---

## TL;DR

**Phase 0 is complete.** A 2-layer MLP with a softmax output, written in `.zero`
source, runs end-to-end through the interpreter and produces correct numerics
(`{0.410960, 0.589040}`, matches an independent oracle, sums to 1.0). `softmax`
is composed in Zero from `exp`/`sum`/`÷` — there is no runtime/IR softmax
primitive. The language can express and execute a real forward pass.

What it is: a **tree-walking interpreter** for a small ML-native language,
targeting the frozen Core Runtime **v1.4.0**.
It can also **train** models with *hand-written* backward passes (see the
`examples/train_*.zero` — scalar/feature regression, a matmul layer, and a
2-layer MLP with manual backprop). What it is **not** yet: no **autograd**
(gradients are hand-written today), no LLVM/native codegen (interpreter only),
no GPU.

---

## What works (verified, tested)

- **Full frontend:** lexer, parser (recursive-descent, multi-line aware,
  hang-proof), source manager, "Frame & Focus" diagnostics.
- **Sema:** name/type resolution + **static literal-derived shape checking**
  (catches `tensor([1,2]) + tensor([1,2,3])` and bad `matmul` dims at compile time).
- **IR (ZIR):** SSA Module/Function/BasicBlock/Value/Instruction; every instruction
  carries a source span; blocks addressed by id (no dangling refs).
- **Lowering:** AST → ZIR for scalars, control flow, functions, and all tensor ops below.
- **Interpreter:** executes against the frozen runtime; per-frame value storage
  (recursion-safe); `Status` errors surfaced as source-spanned `RuntimeError`
  diagnostics.
- **Element-level tensor indexing:** `t[i]` (read; yields a scalar float) and
  `t[i] = x` (in-place write into the tensor's flat / row-major buffer). The
  index target can be an identifier *or* a chain (`l.data[n] = x`). This is the
  primitive that lets `List`/`Dict` be written *in Zero* on top of `Struct` +
  `Tensor` (see `examples/list_in_zero.zero`) — they are not compiler types.
- **Structs:** `struct Name { f: tensor, ... }`; positional construction
  `Name(a, b)`; field read `p.f`; struct-typed variables, params, and returns;
  structs in mutable cells (reassign in a loop). Sema checks field count, field
  names, and field access on non-structs. Field *mutation* (`p.f = x`) is not in
  yet — rebuild the struct. Interpreter represents a struct as an ordered list
  of field values; the runtime's `StructLayout`/`StructData` is reserved for the
  future codegen backend.
- **Variable assignment / mutation (`x = expr`):** reassign declared variables;
  makes `while` loops useful (the condition sees updated state). Mutated
  variables lower to memory cells (alloca/load/store) so updates survive across
  basic blocks; never-reassigned variables keep direct-SSA lowering (identical
  IR, no regressions). Sema rejects assign-to-undeclared and type mismatches.
- **CLI tensor output:** `print` renders tensors (1-D / 2-D), so a `.zero`
  program's tensor results are observable from `zeroc` — not just from C++ tests.
- **Tensors from source:**
  - literals: 1-D and 2-D (`tensor([[1,2],[3,4]])`), multi-line OK
  - elementwise: `+ - * /`, unary `-`, `relu`, `exp`, `log`, `sqrt`, `tanh`, `sigmoid`, `step` (relu derivative)
  - `matmul`, `transpose` (2-D; permute + contiguous)
  - reductions: `sum`, `mean`, `argmax` (full reduction → `[1]`)
  - tensor–scalar broadcast both directions, incl. `s - t` / `s / t` (`x * 2.0`, `1.0 - x`)
  - user functions with tensor params/returns; nested calls
- **Robustness hardening (post-capstone edge-case audit):** an adversarial pass
  over the whole pipeline found and fixed 8 issues — two process crashes (float
  literal overflow → `SIGABRT`; deep recursion → `SIGSEGV`), a missing-return
  value leak, a sema hole where variadic-function arguments went unchecked, plus
  cleaner diagnostics for scalar–tensor ops, matmul rank errors, integer
  overflow, and non-scalar conditions. All locked in by `tests/test_robustness.cpp`
  (`ZeroRobustnessTest`, 13 cases). Full writeup: [PHASE0_ROBUSTNESS_FINDINGS.md](PHASE0_ROBUSTNESS_FINDINGS.md).
- **CI:** GitHub Actions, ubuntu + macos, every push/PR. 31 test binaries
  (ctest auto-discovers registered tests — no per-test wiring needed).

## What does NOT exist yet (the honest gaps)

- **Autograd.** Training works only with *hand-written* gradients today (the
  `train_*.zero` examples). Automatic differentiation (runtime-tape) is the
  Phase-1 headline and is not built yet.
- **LLVM / native codegen.** Tree-walking interpreter only — not fast, not deployable.
- **GPU / MLIR.**
- **Multi-dtype compute.** F32 only (the runtime's fp8/bf16 enums don't compute).
- **Module/import system.** `stdlib/nn.zero` exists as an artifact but isn't importable;
  programs define helpers (e.g. `softmax`) inline.
- **Batched / per-axis reductions.** `sum` etc. are full-reduction only (per-row
  softmax for a batched MLP needs last-axis reductions).
- **Other deferred items:** see [DEFERRED.md](DEFERRED.md) (all *bug/robustness*
  items are resolved; what remains is features + the deliberately-skipped
  `zero::ir::` rename and gtest migration).

## Pinned versions

- Compiler: `main` (Phase 0 complete; specs 001–019 implemented).
- Core runtime submodule: **v1.4.0** (`external/core-runtime`, frozen). Read-only
  from this repo — new primitives require a spec in the runtime repo.

---

## How to resume (for a fresh session)

1. **Read, in order:** [CLAUDE.md](../CLAUDE.md) (inviolable rules), this file,
   [ROADMAP.md](ROADMAP.md), [DEFERRED.md](DEFERRED.md).
2. **Build & test:** `cmake -B build && cmake --build build --parallel && ctest --test-dir build`
   (expect 31/31). The submodule must be initialised:
   `git submodule update --init --recursive`.
3. **Workflow:** spec-driven. Every change starts with an approved
   `docs/specs/NNN-*.md` (template in `docs/specs/_TEMPLATE.md`); tests written
   from the spec; "Out of scope" is binding; commit with the amendment log updated;
   push and confirm CI green on both platforms.
4. **Next work = Phase 1.** Not yet planned in detail. Per the ROADMAP and the
   runtime's `NEXT_STEPS.md`: **autograd first** (runtime-tape, forward then
   backward), **then LLVM CPU codegen**. Phase 1 deserves its own roadmap doc
   (4–6 specs) before coding starts — mirror this Phase-0 ROADMAP's structure.

---

## Note on `mpp_status.md`

`mpp_status.md` (dated 2026-01-12) predates this whole effort and is superseded by
this file + ROADMAP.md. Treat it as historical.
