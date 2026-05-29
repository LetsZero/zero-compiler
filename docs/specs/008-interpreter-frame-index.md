# Spec 008: Interpreter call-stack frame-index refactor

**Status:** Implemented
**Depends on:** spec 006
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

Remove the `call_stack_.reserve(1024)` band-aid added in spec 006 and fix the underlying defect: `Interpreter::call_function` holds `auto& current = call_stack_.back()` and then recursively pushes new frames via `exec_instruction → CALL → call_function`. If the nested push reallocates the underlying `std::vector`, `current` dangles and the next iteration reads garbage state (`std::bad_variant_access` was the observable failure in spec 006).

The fix is to address the frame by **index** instead of by reference. Indices into the call stack are stable across `push_back` reallocations as long as we never pop below the current frame — which is exactly the invariant the interpreter already maintains.

## 2. Invariants

- `Interpreter::call_function` captures a `size_t my_frame_idx` immediately after pushing its frame, and never holds a long-lived reference into `call_stack_`.
- All accesses to the frame's `block_idx`, `instr_idx`, and `locals` inside the body of `call_function` go through `call_stack_[my_frame_idx]` (or a freshly-re-bound `auto& current = call_stack_[my_frame_idx]` per-iteration, which is safe because the binding is re-derived from a stable index after any operation that may reallocate).
- The loop condition becomes `call_stack_.size() > my_frame_idx && call_stack_[my_frame_idx].fn == &fn` so we exit cleanly if the callee popped us.
- The `call_stack_.reserve(1024)` line in `Interpreter::execute()` is **deleted**. The interpreter must correctly handle reallocation during nested calls.
- All 17 pre-existing test binaries continue to pass without modification.
- A new test deliberately triggers many nested calls (a chain that would force `call_stack_` to grow well past its initial capacity) and asserts the program still produces the correct result.

## 3. API surface

Files modified:

1. `src/backend/interpreter.cpp`:
   - `execute()` no longer calls `call_stack_.reserve(1024)`.
   - `call_function()` switches from `auto& current = call_stack_.back()` to a stable `size_t my_frame_idx = call_stack_.size() - 1` after the push, with all subsequent accesses going through `call_stack_[my_frame_idx]`.

Files added:

2. `tests/test_interpreter_recursion.cpp` — exercises deeply-nested calls.

No header changes. No new public symbols. No changes elsewhere.

### Sketch

```cpp
// In call_function, after push:
size_t my_frame_idx = call_stack_.size() - 1;

RuntimeValue result;
while (call_stack_.size() > my_frame_idx
       && call_stack_[my_frame_idx].fn == &fn) {
    auto& current = call_stack_[my_frame_idx];  // re-derived each iter

    if (current.block_idx >= fn.blocks.size()) break;
    const BasicBlock& bb = fn.blocks[current.block_idx];

    while (current.instr_idx < bb.instrs.size()) {
        const Instruction& instr = bb.instrs[current.instr_idx];

        if (instr.op == OpCode::RET) {
            if (!instr.operands.empty()) result = get_value(instr.operands[0]);
            call_stack_.pop_back();
            return result;
        }
        if (instr.op == OpCode::BR) {
            current.block_idx = instr.target_block;
            current.instr_idx = 0;
            break;
        }
        if (instr.op == OpCode::COND_BR) {
            RuntimeValue cond = get_value(instr.operands[0]);
            current.block_idx = (cond.to_int() != 0) ? instr.target_block : instr.else_block;
            current.instr_idx = 0;
            break;
        }

        result = exec_instruction(instr);
        // After exec_instruction, call_stack_ may have reallocated.
        // Re-derive the reference before mutating.
        call_stack_[my_frame_idx].instr_idx++;
    }

    // Fall-through to next block.
    auto& cur = call_stack_[my_frame_idx];
    if (cur.instr_idx >= bb.instrs.size() && cur.block_idx < fn.blocks.size() - 1) {
        cur.block_idx++;
        cur.instr_idx = 0;
    } else if (cur.instr_idx >= bb.instrs.size()) {
        break;
    }
}
```

The key subtlety: after `result = exec_instruction(instr);` the call stack may have grown, popped, or both. The very next statement increments `instr_idx`, but we must **re-derive the reference from `my_frame_idx`** before doing so. The sketch above does it via `call_stack_[my_frame_idx].instr_idx++` rather than `current.instr_idx++`.

Inside a single iteration of the inner loop, `current` is bound fresh from the index — it's only invalidated if `exec_instruction` reallocates, and we re-derive before any mutation after that point.

## 4. Acceptance tests

New test file: `tests/test_interpreter_recursion.cpp`.

1. **Deep call chain works without reserve.** A test builds a chain `f0 → f1 → … → fN` where each `fk` calls `fk+1` and returns its result + 1. With `N` large enough to force `call_stack_` to grow several times past its initial capacity (use `N = 200`), the result is `N` (each frame contributes +1). Tests that the index-based access survives many reallocations.

2. **Single call still works.** Regression-guard for the simple case spec 006 already covers.

3. ~~**Recursive Fibonacci-style call.**~~ **Pulled during implementation** — see amendment log. The recursive-Fibonacci probe surfaced a *separate* pre-existing bug in `IRBuilder` (dangling `BasicBlock&`/`current_block_` across `create_block` reallocation) that has nothing to do with the call-stack refactor this spec is about. The probe is removed from this spec's test; the IRBuilder bug is logged in `docs/DEFERRED.md` and will get its own spec. A note in `test_interpreter_recursion.cpp` records why.

4. **No regressions.** All 17 pre-existing test binaries pass with zero source changes.

## 5. Out of scope

- **Performance work.** Keeping a small `reserve` as a perf hint (avoids the first few reallocations) is reasonable, but this spec strictly removes the hack. A future spec can add a small constant (`reserve(16)`) as a perf tuning if profiling shows it matters.
- **Other interpreter restructuring.** The block-iteration logic, the variant value model, externals registration — all unchanged.
- **Reporter integration.** Errors still throw.
- **Tail-call optimisation.** No.
- **Stack-overflow detection.** The OS stack will overflow at some recursion depth before `call_stack_` does. Not addressed here.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) frame index, not iterator/pointer/reference; (b) every per-iteration access re-derives from the stable index; (c) the `reserve(1024)` line is deleted, not reduced — make the fix unconditionally correct rather than carrying a hidden invariant; (d) the recursion test uses two flavours (deep linear chain and recursive Fibonacci) to cover both growth patterns.
- *Implementation* — The recursive-Fibonacci test (planned item 3 in §4) **could not be landed in this spec** and was pulled. When wired up, it returned the wrong value (`check(n){ if (n<2) return 99; return 7; }` returned 7 for `n=0`). Tracing the interpreter showed the `then`-branch block was *empty* in the IR — the COND_BR's target had no instructions. Root cause is a **separate, pre-existing bug in `IRBuilder`**: it holds a `BasicBlock* current_block_` and `lower_if`/`lower_while` hold `BasicBlock&` references, all pointing into `fn.blocks` (a `std::vector`). The second `create_block()` call `push_back`s and reallocates, dangling those references; subsequent `emit()` writes land in freed memory and never reach the real block. This is unrelated to the call-stack refactor — it's a silent miscompile of *any* control-flow construct, latent because no prior test ran `if`/`while` inside a user function through the interpreter end-to-end. Logged in `docs/DEFERRED.md` as "IRBuilder reference invalidation under create_block"; it gets its own spec (the fix mirrors this spec's: address blocks by index, not reference). The Fibonacci test moves to that spec.
- *Implementation, verification* — `ctest` **18/18 passing**. The shipped `ZeroInterpreterRecursionTest` covers the two cases that exercise *this* spec's fix without touching the IRBuilder bug: the 200-deep linear call chain (forces many `call_stack_` reallocations — the exact scenario the old `auto& current` code corrupted) and a single user-function call regression guard. The `reserve(1024)` line is removed from `execute()`. All 17 pre-existing test binaries pass unmodified.
