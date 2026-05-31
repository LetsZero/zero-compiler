# Phase 0 — Robustness Findings (edge-case audit)

> **Status: ALL FIXED.** Produced by adversarial passes over the pipeline
> (parser → sema → lowering → interpreter → runtime boundary), hunting for
> crashes, wrong results, and missing diagnostics. Findings #1–#8 came from the
> first pass (post-capstone) and are locked in by `tests/test_robustness.cpp`;
> #9–#10 came from a second pass before starting autograd, over the new
> assignment/mutation/transpose surface, and are locked in by
> `tests/test_assignment.cpp`. Suite green at **29/29**.

## TL;DR

The **data-plane is solid** — every numeric result checked was correct: argmax
(ties / negatives / 2-D flat index), `sum`/`mean`, all tensor–scalar broadcast
forms, elementwise math (incl. IEEE `nan`/`inf` edge values), and valid-rank
matmul. **Every failure found is in the error-plane**: crash-safety, error
handling, and semantic coverage — the parts a happy-path capstone never exercises.

Two findings (#1, #4) are foundational and should be fixed before any Phase-1 work
is built on top of them.

| # | Severity | One-line | Site | Fix |
|---|----------|----------|------|-----|
| 1 | 🔴 Crash | Float literal overflow aborts the compiler (`std::stod`) | `parser.cpp` | ✅ guarded `strtod`, emits diagnostic |
| 2 | 🔴 Crash | Deep recursion segfaults (native stack overflow) | `interpreter.cpp` `call_function` | ✅ `kMaxCallDepth=512` guard → throws |
| 3 | 🟠 Correctness | Missing `return` in non-void fn leaks last evaluated value | `interpreter.cpp`, `sema.cpp` | ✅ sema `MISSING_RETURN` + interp returns void |
| 4 | 🟠 Sema hole | Arguments to variadic fns are never checked | `sema.cpp` | ✅ recurse into every argument |
| 5 | 🟡 Expressiveness | `scalar - tensor` / `scalar / tensor` are runtime errors | `interpreter.cpp` | ✅ scalar lifted to tensor shape, computes |
| 6 | 🟡 Diagnostic | matmul with rank-<2 operand gives a misleading message | `interpreter.cpp` | ✅ rank pre-validation, clear message |
| 7 | ⚪ Minor | Integer literal overflow silently becomes 0 | `parser.cpp` | ✅ checks `from_chars` ec, emits diagnostic |
| 8 | ⚪ Minor | Tensor used as `if`/`while` condition silently coerces to 0 | `interpreter.cpp`, `sema.cpp` | ✅ sema rejects non-scalar condition |

### Second pass — pre-autograd audit (assignment / mutation / transpose surface)

After adding assignment, memory cells, and `transpose`, a second adversarial
sweep over the new surface found two more (both fixed; locked in by
`tests/test_assignment.cpp`):

| # | Severity | One-line | Site | Fix |
|---|----------|----------|------|-----|
| 9 | 🟠 Correctness | Shadowing leaked: an inner-scope `let` corrupted the outer variable after the block | `lowering.cpp` | ✅ lowering now snapshots/restores `symbols_` per block (matches sema scoping) |
| 10 | ⚪ Minor | Comparing tensors (`if t > 0`) silently coerced the tensor to 0 (took else) | `sema.cpp` | ✅ sema rejects comparisons with a tensor operand |

Verified *correct* on this pass (no fix needed): parameter mutation is isolated
(pass-by-value; a reassigned param does not affect the caller), reassigning a
variable to an incompatible type is caught, out-of-scope variable use is caught,
transpose (1-D identity, round-trip, `[1,1]`, feeding matmul), scalar-on-left
ops on 2-D, operator precedence, nested-loop mutation, tensor/matmul
accumulation across loop iterations (the gradient-accumulation pattern), and
nested-function calls returning tensors through cells.

> Regression coverage: `tests/test_robustness.cpp` (`ZeroRobustnessTest`).

---

## 🔴 1. Float literal overflow aborts the compiler

**Repro**
```zero
fn main() { let x = 999...(>308 digits)...9.0
print(x) }
```
```
libc++abi: terminating due to uncaught exception of type std::out_of_range: stod: out of range
[exit 134 — SIGABRT]
```
Also triggers via `tensor([ <huge>.0 ])`.

**Root cause**
`std::stod` is called unguarded in two places:
- [`parser.cpp:519`](../src/parser/parser.cpp) — `parse_primary`, `FLOAT_LIT`
- [`parser.cpp:656`](../src/parser/parser.cpp) — `parse_number_row` (tensor elements)

`std::stod` throws `std::out_of_range` (overflow) / `std::invalid_argument`, and
`parser.parse()` runs **outside** the driver's try/catch
([`main.cpp:67`](../src/driver/main.cpp)). The exception propagates to
`std::terminate`.

**Why it matters** — directly violates CLAUDE.md: *"No exceptions in compiler
passes… errors are data, not control flow."* A malformed literal should be a
`Diagnostic`, not an abort.

**Suggested fix** — parse with `std::from_chars`/`strtod` and check the error
condition, emitting a parser diagnostic (e.g. "floating-point literal out of
range") with the literal's span. (Bonus: lexer doesn't accept exponent syntax —
`1e999` lexes as `1` + identifier `e999` — separate gap, note for later.)

## 🔴 2. Deep recursion segfaults

**Repro**
```zero
fn rec(n: int) -> int { if n <= 0 { return 0 } return rec(n - 1) }
fn main() { print(rec(100000)) }
```
```
[exit 139 — SIGSEGV]
```

**Root cause** — the interpreter recurses on the native C++ stack for every
Zero-level call (`exec_instruction` `CALL` → `call_function`,
[`interpreter.cpp:340`](../src/backend/interpreter.cpp)). No depth limit, so a
deep call chain overflows the real stack.

**Suggested fix** — a configurable call-depth guard in `call_function` that throws
a `RuntimeError` ("maximum call depth exceeded") instead of letting the stack
overflow. (A true fix is an explicit work-stack, but that's larger; a guard is
enough to turn a crash into a diagnostic.)

## 🟠 3. Missing `return` in a non-void function leaks the last evaluated value

**Repro**
```zero
fn f() -> int { let x = 42 }              // no return → f() yields 42
fn g() -> int { let a = 7  let b = 99 }   // → 99
fn h() -> tensor { let a = tensor([7.0,8.0]) }   // → leaks that tensor
```

**Root cause**
- The interpreter's `result` accumulator in `call_function` holds the value of
  the **last executed instruction**; an implicit (or empty-operand) `RET` returns
  that leftover ([`interpreter.cpp:161-187`](../src/backend/interpreter.cpp)).
- Sema never requires a `return` in a non-void function — `check_fn` only checks
  `return` statements that exist ([`sema.cpp:166`](../src/sema/sema.cpp)).

Return-*type* checking works correctly when a `return` is present.

**Suggested fix** — sema error: "non-void function may fall off the end without
returning a value" (all paths must return). Independently, harden the interpreter
to return a defined default (void/0) rather than leaking internal state.

## 🟠 4. Arguments to variadic functions are never semantically checked

**Repro** (all pass sema with no error today)
```zero
fn main() { print(undef) }          // undefined variable — not reported
fn main() { let y = relu(undef) }   // undefined variable — not reported
fn main() { print(add(1)) }         // nested bad arity — not reported
```

**Root cause** — the argument-checking loop in `check_expr`'s `CallExpr` branch is
bounded by `sig.param_types.size()`:
```cpp
for (size_t i = 0; i < e.args.size() && i < sig.param_types.size(); ++i) {
    types::Type arg_type = check_expr(*e.args[i]);   // never reached for variadics
    ...
}
```
([`sema.cpp:337`](../src/sema/sema.cpp)). For a **variadic** signature
`param_types` is empty (size 0), so the loop body never runs and `check_expr` is
never called on any argument.

**Why it matters** — **every** tensor builtin (`relu`, `sum`, `mean`, `argmax`,
`matmul`, `exp`, `log`, `sqrt`, `tanh`, `sigmoid`) plus `print`/`log`/`capture` is
registered variadic ([`sema.cpp:116-164`](../src/sema/sema.cpp)). So undefined
variables, nested arity errors, and type mismatches inside the arguments of any of
them slip straight past sema to runtime (or to silently-wrong results). This
quietly reduces sema's effective coverage to near-zero for the exact code — tensor
ops — the language exists to compile.

**Suggested fix** — always recurse `check_expr` into **every** argument (so
sub-expression errors surface); perform the param-type compatibility comparison
only for the positions where a declared param type exists.

## 🟡 5. `scalar - tensor` and `scalar / tensor` are runtime errors

**Repro**
```zero
let x = tensor([1.0, 2.0])
let y = 1.0 - x      // error: scalar on the left of a non-commutative tensor op … not supported
let z = 2.0 / x      // same
```

**Root cause** — only commutative `+`/`*` swap the operands so the tensor becomes
the shape-defining side; `-`/`/` with a scalar LHS throw
([`interpreter.cpp:414`](../src/backend/interpreter.cpp)).

**Why it matters** — `1.0 - sigmoid(x)` and friends are everyday ML idioms. The
limitation is intentional today, but it surfaces at runtime, not compile time, and
blocks natural expressions.

**Suggested fix** — lower `s - t` to `(-t) + s` and `s / t` via a reciprocal/
elementwise path (compose from existing primitives, per the erosion rule). At
minimum, catch it in sema/lowering with a clear compile-time diagnostic instead of
a runtime throw.

## 🟡 6. matmul with a rank-<2 operand gives a misleading message

**Repro**
```zero
matmul(tensor([1.0,2.0,3.0]), tensor([4.0,5.0,6.0]))
// error: tensor op: output allocation failed
```

**Root cause** — the interpreter constructs the output shape blindly as
`[A.shape[0], B.ndim>=2 ? B.shape[1] : 0]` → `[3, 0]`, then tries to allocate a
0-element output, which fails *before* the runtime's clean "matmul requires
rank-2" check ever runs ([`interpreter.cpp:488`](../src/backend/interpreter.cpp),
alloc at [`:39`](../src/backend/interpreter.cpp)). Valid-rank inner-dimension
mismatches, by contrast, produce a perfect runtime error.

**Suggested fix** — pre-validate operand ranks (rank-2) in lowering or in the
matmul interpreter arm and emit a precise diagnostic; don't let a degenerate
pre-allocation mask the real cause.

## ⚪ 7. Integer literal overflow silently becomes 0

`from_chars`'s error code is ignored in `parse_primary`'s `INT_LIT`
([`parser.cpp:506`](../src/parser/parser.cpp)), so `99999999999999999999999`
parses to `0` with no diagnostic. Fix: check the `ec` and report an out-of-range
integer literal.

## ⚪ 8. Tensor used as a condition silently coerces to 0 (false)

```zero
if tensor([1.0, 2.0]) { ... } else { ... }   // always takes else, no diagnostic
```
`COND_BR` does `get_value(cond).to_int()`, which returns 0 for a tensor
([`interpreter.cpp:176`](../src/backend/interpreter.cpp)); sema doesn't check that
a condition is scalar/boolean. Fix: sema diagnostic for a non-scalar condition.

---

## What is NOT broken (verified)

- **Numerics**: `argmax` (ties → first, negatives, 2-D flat index), `sum`/`mean`
  (1-D and 2-D full reduction), `t*s`/`s*t`/`t+s`/`s+t`/`t/s`, int-scalar widening,
  `sqrt(-1)=nan`, `log(0)=-inf`, chained reductions, `relu` of a reduction result.
- **Valid matmul** inner-dimension mismatch → clean, well-spanned runtime error.
- **Static shape check** (spec 018) catches literal-derived `+` and `matmul`
  mismatches at compile time.
- **Parser robustness**: ragged / 3-D / empty tensor literals are rejected (not
  hangs); deeply nested parens (5000) parse without crashing; the no-progress
  guard holds.
- **Return-type checking** fires correctly when a `return` is present.
- **Arg-count checking** fires correctly for non-variadic (user) functions called
  directly (the hole in #4 is specifically *variadic* arguments).

---

## Suggested grouping for fixes

A "Phase 0.5 robustness" batch, roughly by blast radius:

1. **Safe literal parsing** (#1, #7) — no exception escapes the parser; numeric
   literals out of range become diagnostics.
2. **Sema argument checking** (#4) — recurse into every call argument; the single
   highest-leverage correctness fix.
3. **Defined behavior for missing returns** (#3) — sema requires a return; the
   interpreter never leaks internal state.
4. **Call-depth guard** (#2) — crash → diagnostic.
5. **Cleaner tensor-op diagnostics & expressiveness** (#5, #6, #8) — compile-time
   errors / operand lowering instead of runtime throws and misleading messages.
