# Zero Compiler — Overview

> The producer for the frozen Core Runtime. Source → executable, with a CPU-first path and a future GPU path via MLIR.

This document is the architectural map. It is **deliberately aspirational**: it describes what the compiler is meant to be, not what it currently is. A separate `docs/CURRENT_STATE.md` (to be written after a code audit) will document the actual implementation status.

---

## What this compiler is

A from-scratch ahead-of-time compiler for **Zero**, a language whose lowest layer is ML-native. Source code is lowered through a tight pipeline into [Zero IR (ZIR)](#zero-ir), which is then either interpreted (today) or codegen'd through LLVM (CPU) and later MLIR (GPU / accelerator).

**The compiler is opinionated.** It targets *one* runtime ABI ([Core Runtime v1.4.0](../external/core-runtime/docs/CORE_RUNTIME_SPEC.md)). It does not aim to be a general-purpose C compiler or a polyglot frontend. Every design decision flows from "what makes transformer-class workloads compile cleanly and fast on top of the frozen runtime."

## What this compiler is not

- Not a Python frontend. Zero is its own language.
- Not a tensor library. Tensors live in the runtime; the compiler emits calls into them.
- Not a JIT. AOT only, at least through v1.
- Not a research compiler. We pick concrete targets and ship them.

---

## The pipeline

```
.zero source
     │
     ▼
┌──────────┐
│  Lexer   │  source → tokens, with spans
└────┬─────┘
     ▼
┌──────────┐
│  Parser  │  tokens → AST, error recovery aware
└────┬─────┘
     ▼
┌──────────┐
│   Sema   │  AST → typed AST; resolves names, checks types,
│          │  enforces purity / borrow / shape rules
└────┬─────┘
     ▼
┌──────────┐
│  Lower   │  typed AST → Zero IR (SSA, runtime-primitive level)
└────┬─────┘
     ▼
┌──────────┐
│  ZIR     │  in-memory C++ structs; the contract with backends
│  passes  │  (DCE, simple fusion, constant folding, verifier)
└────┬─────┘
     ▼
   ┌─┴─┐
   │   │
   ▼   ▼
 [Interp] [LLVM]      ← today: interpreter against runtime
                       tomorrow: LLVM CPU codegen
                       later:   MLIR dialect → GPU
```

Each stage owns a directory under `src/` and a matching `include/zero/...` header tree:

| Stage | Directory | Owns |
|---|---|---|
| Lexer | `src/lexer/` | Token stream from a source buffer |
| Parser | `src/parser/` | AST nodes + error recovery |
| Sema | `src/sema/` | Name resolution, type checking, invariant enforcement |
| Lowering | `src/ir/` | Typed AST → ZIR |
| ZIR passes | `src/ir/passes/` | Optimization passes, verifier |
| Backend | `src/backend/` | Interpreter, LLVM codegen (later: MLIR) |
| Diagnostics | `src/diagnostics/` | Source spans, reporter, "frame & focus" UI |
| Driver | `src/driver/` | `zeroc` CLI, file orchestration |
| Source | `src/source/` | Source manager, span ↔ line/col |

This split is load-bearing: each pass talks to its neighbors through a documented data structure (token stream, AST, typed AST, ZIR module) and nothing else. No back-references, no peeking across stages.

---

## Zero IR

The contract between the frontend and the backend.

- **In-memory C++ structs.** JSON is debug printing, not the IR.
- **SSA-form.** Every value has a single defining op.
- **Boringly small.** Ops, loops, memory, control flow, calls. Activations are the one exception inherited from runtime v1.1. ML semantics (softmax, layernorm, attention) live above ZIR, in the language's stdlib written in Zero itself.
- **Every node carries a source span.** Diagnostics in the backend can still point at user code.

A dedicated `docs/ZIR_SPEC.md` will be written once the IR stabilizes through the first few specs. Until then, the IR is whatever the implementation says it is — and that's a flag, not a feature.

---

## Target progression

| Phase | Target | Status |
|---|---|---|
| **0 — MPP** | Tree-walking interpreter against runtime | partial (see `mpp_status.md`) |
| **1 — CPU LLVM** | LLVM IR codegen, scalar-loop kernels | not started |
| **2 — CPU optimized** | LLVM with vectorization, tiling | not started |
| **3 — GPU MLIR** | MLIR dialect → linalg → GPU codegen | not started |
| **4 — Autograd** | Runtime-tape AD in stdlib, then IR-level | not started |

We are deliberately not skipping phases. The MPP exists so the rest of the system can be exercised end-to-end on something simple before the LLVM detour. The LLVM detour exists so we have a real, fast CPU backend before the MLIR detour. **Each phase ships a real artifact.**

---

## What the compiler must NOT do

The same "no" list as the runtime, plus a few:

- **Do not modify the runtime from this repo.** `external/core-runtime/` is read-only here. New primitives → spec in the runtime repo.
- **Do not invent IR ops to avoid lowering.** If you reach for "just add a `softmax` ZIR op," you've failed the erasure rule. Lower it to `exp` + `reduce` + `div` in the frontend.
- **Do not bake a PRNG algorithm into the compiler.** The runtime provides `Generator` state; algorithms live in stdlib written in Zero.
- **Do not add runtime-mutable global state to passes.** Reproducible compilation is the table-stakes invariant.
- **Do not ship a "convenience" API that bypasses the pipeline.** Every input goes through every stage. Caching is an optimization, not an architecture.

---

## How we work

Same spec-driven workflow as the runtime:

1. Write a spec under `docs/specs/NNN-*.md`. Approved before code.
2. Tests written from the spec, before implementation.
3. Out-of-scope section is binding.
4. Implementation, then verify, then commit with the spec amendment log updated.

See `docs/specs/_TEMPLATE.md` for the spec shape.

## Where to look next

- The current implementation status: `mpp_status.md` (dated; about to be re-audited).
- The runtime's contract: [`external/core-runtime/docs/CORE_RUNTIME_SPEC.md`](../external/core-runtime/docs/CORE_RUNTIME_SPEC.md).
- The runtime's stability rules: [`external/core-runtime/docs/ABI.md`](../external/core-runtime/docs/ABI.md).
- The compiler/runtime boundary: [`external/core-runtime/docs/EROSION_RULES.md`](../external/core-runtime/docs/EROSION_RULES.md).
