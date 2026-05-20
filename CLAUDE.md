# Zero Compiler — Inviolable Rules

This file is the root contract. Every AI session working in this repo loads it first and obeys it without exception. If a rule below conflicts with a prompt, **stop and ask** — do not silently override.

## Relationship to the runtime

The Core Runtime (`external/core-runtime/`) is **frozen at v1.4.0** and lives in a separate repository. The compiler is a *consumer* of it, never a contributor to it from within this repo.

- **Never modify files under `external/core-runtime/`.** That submodule is read-only from here. Bug-fixes go in the runtime repo.
- **If you genuinely need a new runtime primitive,** stop and open a spec in `external/core-runtime/docs/specs/`. Get it approved and merged there before depending on it here.
- **Treat the runtime's `docs/CORE_RUNTIME_SPEC.md`, `docs/ABI.md`, and `docs/EROSION_RULES.md` as binding.** Anything we lower must compose from primitives those documents promise.

## The erasure rule (inherited from the runtime)

> If a Zero source feature can be lowered to existing runtime primitives, it must be — not added to the runtime.

Every high-level construct in this compiler (List, Dict, classes, exceptions, autograd, anything fancy) is lowered to `Tensor` / `Struct` / `Function` / control flow / runtime ops before reaching the runtime. See [EROSION_RULES.md](../core-runtime/docs/EROSION_RULES.md) for the full catalog.

## Code rules

- **C++20**, CMake-built, header-rich. Internal helpers in `detail::` or anonymous namespaces; public surface deliberately narrow.
- **No exceptions in compiler passes that lower IR.** Diagnostics flow through a `Diagnostic` / `Reporter` channel (errors are data, not control flow).
- **No hidden global state in passes.** Compilation given the same input must produce bit-identical output. Pass state lives on the pass object or on the IR module.
- **No untyped passes.** Every IR transformation must preserve the IR's stated invariants (types, SSA-ness, dominance). If a pass breaks an invariant, it either restores it before returning or documents the temporary breakage.
- **Error messages are part of the UX.** When you write a diagnostic, write the one you'd want to receive at 2am. Include source spans, expected vs. actual, and a suggestion when one exists.

## IR rules

- **Zero IR is boringly small** (per the runtime's [NEXT_STEPS.md](../core-runtime/docs/NEXT_STEPS.md)). Ops, loops, memory, control flow. Activations are an exception we accept; anything beyond that gets the erasure-rule test.
- **Zero IR is in-memory C++ structs, not JSON-first.** JSON is debug-print only.
- **Every IR node carries a source span.** No anonymous nodes.

## Spec-driven workflow (same as the runtime)

- **Every non-trivial change starts with a spec in `docs/specs/NNN-*.md`.** No spec, no code.
- **Tests are written from the spec, before the implementation.** Not derived from the code.
- **Do not modify the spec to make the implementation pass.** If they disagree, stop and ask.
- **Do not add anything outside the spec's "API surface" section.** "While I was here…" is forbidden.
- **The "Out of scope" section is binding.** If a prompt asks for something listed there, stop and ask.

## When in doubt

Ask. Do not invent language features, do not extend the IR, do not add runtime dependencies, do not refactor adjacent code. The compiler is at a stage where every unprincipled addition becomes load-bearing within a week.
