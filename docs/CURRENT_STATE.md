# Zero Compiler — Current State (Audit)

> A snapshot of what actually exists in this repo today vs. what [COMPILER_OVERVIEW.md](COMPILER_OVERVIEW.md) aspires to. Read this before writing any spec — it tells you which gap to close first.

**Audit date:** end of v1.4 runtime cycle.
**Audit method:** read-only pass over `src/`, `include/`, `tests/`, `runtime/`, `CMakeLists.txt`, and the (stale) `mpp_status.md`.
**Verdict:** the compiler is a **working scalar pipeline** stapled to a **completely stubbed tensor surface**. The frontend is real; the tensor side is hollow.

---

## What works today

| Stage | Status | Notes |
|---|---|---|
| Lexer ([src/lexer/](../src/lexer)) | ✅ substantive | identifiers, literals, keywords, operators, source spans on tokens. |
| Parser ([src/parser/](../src/parser)) | ✅ substantive | recursive descent → AST, precedence, basic error recovery. |
| Source manager ([src/source/](../src/source)) | ✅ substantive | file loading, line/col mapping, `Span` with merge. |
| Diagnostics ([src/diagnostics/](../src/diagnostics)) | ✅ substantive | "Frame & Focus" reporter with source-span rendering. |
| Sema ([src/sema/](../src/sema)) | 🟡 partial | undefined-name and basic type-compatibility checks; no shape/dtype semantics yet. |
| IR ([include/ir/ir.hpp](../include/ir/ir.hpp)) | ✅ substantive | SSA Module/Function/BasicBlock/Value/Instruction, 23 opcodes. |
| Lowering ([src/ir/lowering.cpp](../src/ir/lowering.cpp)) | ✅ substantive | every AST statement/expression lowers to ZIR. |
| Interpreter ([src/backend/interpreter.cpp](../src/backend/interpreter.cpp)) | 🟡 substantive but partial | every **scalar** opcode executes; every **tensor** opcode is a stub. |
| Driver ([src/driver/main.cpp](../src/driver/main.cpp)) | ✅ substantive | `zeroc` CLI, `--dump-ir`, end-to-end compile-and-run for scalar programs. |
| Tests ([tests/](../tests)) | ✅ substantive | ~50 test cases across lexer/parser/sema/ir/backend. |

## What's hollow or missing

| Concern | Where | Severity |
|---|---|---|
| **All tensor ops are no-ops in the interpreter.** | [src/backend/interpreter.cpp:276–284](../src/backend/interpreter.cpp) — `TENSOR_*` opcodes return `nullptr`. | 🔴 blocker |
| **Compiler consumes none of the v1.4 runtime surface.** | runtime adapter at [runtime/](../runtime) is only `print`/`log` wrappers — no tensor-op bridge. | 🔴 blocker |
| **C++ standard mismatch.** | [CMakeLists.txt](../CMakeLists.txt) sets `CXX_STANDARD 17`; the submodule runtime requires C++20 (constexpr, designated initializers, `std::array` initialization). | 🔴 build risk |
| **IR nodes have no source span.** | [include/ir/ir.hpp](../include/ir/ir.hpp) `Instruction` struct lacks a `Span` field. Violates [CLAUDE.md](../CLAUDE.md) ("every IR node carries a source span"). | 🟠 contract drift |
| **No IR passes folder.** | No `src/ir/passes/`. No DCE, no constant folding, no verifier. | 🟠 expected gap |
| **No `Status` plumbing through the IR.** | Lowering emits tensor ops as if they return `void`. The runtime returns `Status` since v1.2 spec 002. | 🔴 will silently break once linked |
| **No `Stream*` in the calling convention.** | Lowering doesn't thread a stream parameter through tensor calls. Required by runtime spec 003. | 🟠 ABI mismatch with frozen runtime |
| **No support for `gather`/`scatter` / `Generator` / `contiguous`** (specs 004/005/006). | No IR opcodes, no lowering paths. | 🟠 missing surface |
| **No end-to-end tensor test.** | [tests/](../tests) has scalar coverage; nothing exercises source → IR → real tensor op. | 🟠 nothing proves the pipeline works for the language's main job |
| **`mpp_status.md` is stale.** | Dated 2026-01-12, predates the runtime freeze cycle. Most "DONE" items still hold; the runtime-integration column is now wrong. | 🟢 cosmetic — this audit supersedes it. |

## The fundamental problem

The compiler can compile and run programs that do scalar arithmetic, control flow, and call into runtime print/log helpers. **It cannot, today, produce a program that adds two tensors.** Every tensor opcode in the IR is recognized by the interpreter and then dropped on the floor.

This is recoverable. The frontend is genuinely usable. The IR is clean. The interpreter has the right shape — it just needs its tensor handlers connected to `external/core-runtime/include/zero/ops/*.hpp`. But until that connection lands, nothing the compiler emits actually exercises the runtime.

## Pinned versions

- Compiler tree: current `main` (commit `21fa6fa`).
- Core runtime submodule: pinned at `v1.4.0` (commit `a9785af`).

These two are now in agreement; before this audit they were not (submodule was at v1.2.0).

---

## What this implies for the first specs

The audit suggests a clear order for the first three compiler specs. None of them is the "shiny" feature; all three are foundation.

1. **Spec 001 — Build foundation against v1.4 runtime.** Bump `CXX_STANDARD` to 20, confirm the submodule includes resolve, link a trivial test that calls one runtime op (`zero::ops::add`) from a host-side test (not yet from the compiler). Proves the bridge is buildable before we ask the compiler to drive it.

2. **Spec 002 — Source spans on IR nodes.** Tiny but contract-critical. Adds a `Span` field to `Instruction`, threads it through `IRBuilder`, updates the dumper. Closes the gap CLAUDE.md created.

3. **Spec 003 — End-to-end tensor add.** Pick a single op (`add`), lower it from AST to a new `TENSOR_ADD` (already exists in the enum) that actually calls `zero::ops::add` in the interpreter with a real F32 contiguous tensor. Plumb the new `Status` return into a runtime-error diagnostic. This is the first spec that proves the language can do its job.

After these three, every subsequent spec is "add another op" or "add another pass" — at that point we're building, not bootstrapping. But these three have to come first, in this order, because each one unblocks the next.

---

## See also

- [COMPILER_OVERVIEW.md](COMPILER_OVERVIEW.md) — where we're going.
- [CLAUDE.md](../CLAUDE.md) — the rules.
- [`docs/specs/_TEMPLATE.md`](specs/_TEMPLATE.md) — the spec workflow.
- [external/core-runtime/docs/CORE_RUNTIME_SPEC.md](../external/core-runtime/docs/CORE_RUNTIME_SPEC.md) — the surface this compiler targets.
