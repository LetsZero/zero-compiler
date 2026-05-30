# Spec 013: Fix Linux-only abort in source/function tensor tests

**Status:** Implemented
**Depends on:** spec 012 (CI surfaced it), spec 003 (TensorPtr deleter), specs 004/006 (the affected tests)
**PR:** (local commit)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

`ZeroSourceTensorTest` (spec 004) and `ZeroFunctionTensorIOTest` (spec 006) abort on `ubuntu-latest` (libstdc++) with `terminate called without an active exception`, while passing on macOS (libc++) and in every local run. Spec 012's CI caught it on the first run. Make both pass on Linux.

**Hypothesis (to be confirmed by the CI run this spec triggers):** the abort happens during *static destruction*, not inside a test. Both files hold a namespace-scope `static RuntimeValue captured;` that, at the happy-path end of the run, still owns a `TensorPtr` (`std::shared_ptr<zero::Tensor>` with a custom deleter that calls `tensor->free()` then `delete`). At process exit the deleter runs during static teardown and touches the header-only runtime's allocator. `terminate ... without an active exception` is the libstdc++ signature for a fault outside any in-flight exception — consistent with a destructor-time problem rather than a thrown-and-uncaught error (which would say "after throwing an instance of …"). libc++ and libstdc++ differ in static-destruction behaviour, which explains macOS-pass / Linux-abort.

**Fix:** make teardown deterministic — explicitly release the captured tensor(s) *before* `main` returns, so no `TensorPtr` deleter runs during static destruction. This is good hygiene regardless of the exact root cause: a shared_ptr whose deleter calls into another library should not first run at program-exit static-destruction time.

To *confirm* the hypothesis (and pinpoint the culprit if the fix misses), the test harness also gets per-subtest output flushing so the next CI run shows exactly which subtest is last before any abort.

## 2. Invariants

- Both `tests/test_source_tensor.cpp` and `tests/test_function_tensor_io.cpp`:
  - Reset the `static RuntimeValue captured;` to a default (non-tensor) value at the **end of `main`**, before returning — so no `TensorPtr` survives into static destruction.
  - Emit each `"  Running <name>..."` line with an explicit flush (so a subsequent abort cannot swallow the indication of which subtest was active).
- After this change, both tests pass on `ubuntu-latest` **and** `macos-latest` under CI (the acceptance signal), and continue to pass locally.
- No change to the *assertions* or *behaviour* of any subtest — only teardown determinism and output flushing. The tests verify the same things they did before.
- No production code (`src/`, `include/`) changes. This is a test-side teardown/diagnostic fix. (If the CI run reveals the abort is *not* static-destruction but a real production bug, this spec is amended and the fix moves into production code — see §6 / amendment log.)
- The other test files are not modified (they don't exhibit the abort), though the same teardown pattern may be applied to them in a later sweep if desired.

## 3. API surface

Files modified:

1. `tests/test_source_tensor.cpp` — flush the per-subtest line in `run_all_tests`; add `captured = RuntimeValue{};` at the end of `main` before `return`.
2. `tests/test_function_tensor_io.cpp` — same two changes.

No new files. No production code. No CMake change.

### Sketch

```cpp
// run_all_tests(): make the progress line unbuffered so it survives an abort.
std::cout << "  Running " << t.name << "... " << std::flush;

// main(): deterministic teardown — drop the captured tensor before statics run.
int main() {
    std::cout << "=== ... ===\n";
    int rc = run_all_tests();
    captured = RuntimeValue{};   // release any held TensorPtr now, not at exit
    return rc;
}
```

## 4. Acceptance tests

No new test binary — this fixes existing tests. Verification is multi-platform CI plus local:

1. **Local (macOS):** `ctest` remains 20/20 (no regression on the platform that already passed).
2. **CI Linux leg green:** the `ubuntu-latest` job passes 20/20 — specifically `ZeroSourceTensorTest` and `ZeroFunctionTensorIOTest` no longer abort. This is the acceptance signal and can only be confirmed on CI (the bug does not reproduce on macOS/libc++).
3. **CI macOS leg green:** stays 20/20.

If the Linux leg is still red after this change, the now-flushed per-subtest output identifies the exact aborting subtest, and this spec is amended with the narrowed cause and a corrected fix.

## 5. Out of scope

- **Applying the teardown pattern to all test files.** Only the two that abort are fixed here. A later hygiene sweep can standardise it.
- **A shared test-harness header.** The TEST macro / `run_all_tests` is copy-pasted across test files; de-duplicating it into a common header is a separate tooling item.
- **Sanitizer CI** (would likely have caught this earlier). Tracked separately in `DEFERRED.md`.
- **Changing the `TensorPtr` deleter or `RuntimeValue`'s variant.** If the CI run confirms the static-destruction hypothesis, the test-side reset is sufficient; we do not need to alter production ownership semantics. (Only revisited if CI proves a production bug.)
- **Windows.** Not in CI yet.

## 6. Open questions

- **Is the root cause truly static destruction, or a real production-code bug that only manifests on libstdc++?** The fix in this spec assumes the former (and is correct hygiene either way). The CI run triggered by this commit is the deciding experiment. If it goes green, hypothesis confirmed. If not, the flushed output re-scopes the problem and this spec is amended before any further change.

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) ship the most-likely fix (deterministic teardown) and the diagnostic (per-subtest flush) in one CI iteration, rather than a pure observe-then-fix round-trip — the fix is correct hygiene regardless, and the flush de-risks the case where it misses; (b) keep the change test-side only, with an explicit escape hatch in §6/§5 to move into production code if CI proves a real production bug; (c) the acceptance signal is intrinsically CI-based (the bug is libstdc++-only), which this spec states plainly rather than pretending local verification suffices.
- *Implementation, verification — HYPOTHESIS CONFIRMED.* The static-destruction hypothesis was correct: resetting `captured` before `main` returns eliminated the abort, no production code needed. CI run 26692598115 is **green on both legs** — `ubuntu-latest: success`, `macos-latest: success`, 20/20 each. The escape hatch in §6 (move fix into production code) was not needed. Local macOS ctest remains 20/20. This is the first fully-green multi-platform CI run on the compiler.