# Spec 010: Reporter integration for runtime errors

**Status:** Approved
**Depends on:** spec 002 (IR spans), spec 003 (tensor-op throw path)
**PR:** (pending)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

When a tensor op fails at runtime, render a proper "Frame & Focus" diagnostic with source-line context instead of just throwing a one-line `std::runtime_error`. The `diagnostics::Reporter` already exists and produces nicely-formatted, source-aware errors for the lexer/parser/sema; the interpreter is the only error producer not using it. Since spec 002 every IR instruction carries a `source::Span`, and `SourceManager` can resolve a span to `(filename, line, col)` — so the data is all present, just not wired together.

After this spec, running a `.zero` program that hits a shape-mismatched tensor op through `zeroc` prints a `[ ERROR ] RUNTIME in '<file>'` block pointing at the offending source line, rather than a bare string.

## 2. Invariants

- `Interpreter` gains an optional, nullable `const source::SourceManager* sm_` (default `nullptr`) and a setter `void set_source_manager(const source::SourceManager* sm)`.
- When a tensor op returns an error `Status` AND `sm_` is non-null AND the instruction's span is valid, the interpreter resolves the span to a `diagnostics::SourceLocation{ path, line, col }` (via `sm_->get_path(span.source_id)` and `sm_->get_line_col(span)`) and calls `diagnostics::Reporter::reportError(ErrorType::RUNTIME, loc, message, help)` **before** throwing.
- The interpreter still throws `std::runtime_error` with the same message it does today (op name + status code + msg + `@id:start-end` span fragment). The Reporter call is **additive** — it does not replace the throw. The throw remains the control-flow mechanism that unwinds execution; the Reporter call is purely for rich rendering.
- When `sm_` is `nullptr` (the default), behaviour is exactly as today: throw only, no Reporter output. This keeps every existing test green (none set a SourceManager).
- The driver (`zeroc`) calls `interp.set_source_manager(&sm)` before `execute`, so real compilation gets rich runtime diagnostics. The driver's existing `catch` still prints the thrown message and returns 1 — the Reporter block appears first (stderr), then the catch's summary line.
- All 19 pre-existing test binaries continue to pass without modification.

## 3. API surface

Files modified:

1. `include/backend/interpreter.hpp` — add `#include "source/source.hpp"` and `#include "diagnostics/reporter.hpp"` (compiler-side headers, no `zero::ir` collision); add the `sm_` field and `set_source_manager` setter.
2. `src/backend/interpreter.cpp` — `throw_with_span(...)` gains a `const source::SourceManager* sm` parameter; when non-null and the span is valid, it emits a `Reporter::reportError(RUNTIME, ...)` before throwing. All call sites pass `sm_`.
3. `src/backend/CMakeLists.txt` — `zerobackend` links `zerodiag` and `zerosrc` (it already links `zeroir zero-core`; `zeroir` pulls `zerosrc` transitively, but make both explicit).
4. `src/driver/main.cpp` — `interp.set_source_manager(&sm);` before `interp.execute(...)`.

Files added:

5. `tests/test_runtime_diagnostics.cpp` — captures `std::cerr` and asserts the Reporter block is emitted when a SourceManager is set.

No changes to the lexer, parser, sema, AST, IR, or runtime.

### Sketch — throw_with_span

```cpp
static void throw_with_span(const char* op_name,
                            const zero::Status& s,
                            const source::Span& span,
                            const source::SourceManager* sm) {
    // Rich diagnostic when we have a source manager + a real span.
    if (sm != nullptr && span.valid()) {
        auto [line, col] = sm->get_line_col(span);
        diagnostics::SourceLocation loc(
            sm->get_path(span.source_id),
            static_cast<int>(line),
            static_cast<int>(col));
        std::string msg = std::string(op_name) + ": "
            + (s.msg ? s.msg : "runtime error");
        diagnostics::Reporter::reportError(
            diagnostics::ErrorType::RUNTIME, loc, msg,
            /*help=*/"check tensor shapes and dtypes at this call site");
    }

    // Throw as before — unchanged message, unchanged control flow.
    std::ostringstream m;
    m << op_name << " failed: code=" << static_cast<int>(s.code);
    if (s.msg) m << " (" << s.msg << ")";
    if (span.valid()) {
        m << " @" << static_cast<uint32_t>(span.source_id)
          << ":" << span.start_offset << "-" << span.end_offset;
    }
    throw std::runtime_error(m.str());
}
```

Call sites become `throw_with_span(op_name, s, instr.span, sm_)`.

## 4. Acceptance tests

New test file: `tests/test_runtime_diagnostics.cpp`.

1. **Reporter fires when a SourceManager is set.** Compile a program with a shape-mismatched tensor op (`tensor([1,2,3,4]) - tensor([1,2,3])`), set the interpreter's SourceManager, redirect `std::cerr` to a `std::stringstream`, run, and assert:
   - A `std::runtime_error` was still thrown (control flow preserved).
   - The captured stderr contains `[ ERROR ]`, `RUNTIME`, and the virtual filename used in the test.

2. **No Reporter output when no SourceManager.** Same program, do **not** set a SourceManager. Assert: the throw still happens, and the captured stderr contains **no** `[ ERROR ]` block (only the throw, which the test catches and does not print).

3. **Happy path emits nothing.** A well-formed tensor add with a SourceManager set produces no Reporter output and no throw.

4. **No regressions.** All 19 pre-existing test binaries pass with zero source changes.

## 5. Out of scope

- **Replacing the throw with a Status-return interpreter.** The interpreter remains throw-based for control flow. Migrating it to return a `Status`/`Result` from `execute` is a much larger change; deferred.
- **Parser/sema diagnostics.** They already use the Reporter; not touched.
- **Multi-line / multi-span runtime diagnostics.** Single span, single location.
- **A `help` message tailored per `StatusCode`.** v1 uses one generic help string ("check tensor shapes and dtypes…"). Per-code help text is a later refinement.
- **Caret/underline length matching the offending token.** The Reporter underlines from the column; refining the span length rendering is its own concern.
- **Routing the *thrown* message through the Reporter in the driver's `catch`.** The driver still prints its summary line on catch. We don't dedupe the two outputs in this spec.
- **Colorized-output toggling / NO_COLOR support.** Out of scope.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) Reporter call is *additive* to the throw, not a replacement — preserves control flow and every existing test that asserts on the thrown message; (b) the SourceManager is an opt-in nullable pointer, so the rich path only activates for real compilation (the driver wires it; unit tests opt in deliberately); (c) one generic `help` string for v1, per-`StatusCode` help deferred; (d) the test captures `std::cerr` via `rdbuf` swap to assert the Reporter block is emitted — a real behavioural assertion, not just "doesn't crash".
