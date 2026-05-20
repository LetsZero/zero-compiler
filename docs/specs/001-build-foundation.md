# Spec 001: Build foundation against runtime v1.4

**Status:** Approved
**Depends on:** none
**PR:** (pending)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

Prove that the compiler tree can build, link, and execute against the frozen Core Runtime v1.4.0 surface — *before* any compiler code is asked to drive it. Today: `CMAKE_CXX_STANDARD = 17` while the runtime requires C++20, the `external/core-runtime` subdirectory is added at the bottom of `CMakeLists.txt` (after `tests/`, so tests cannot link it), and no test anywhere actually `#include`s a runtime header. This spec closes all three gaps with the smallest possible change.

The deliverable is a single new test binary, `test_runtime_bridge`, that allocates F32 tensors and calls `zero::ops::add` directly. If it builds and passes under `ctest`, the bridge between the two repos is real. No compiler IR, no lowering, no interpreter changes touch this spec.

## 2. Invariants

- `CMAKE_CXX_STANDARD` is `20` (was `17`). `CMAKE_CXX_STANDARD_REQUIRED` stays `ON`, `CMAKE_CXX_EXTENSIONS` stays `OFF`.
- The submodule `external/core-runtime` is added via `add_subdirectory(...)` **before** any `add_subdirectory(src/...)` or `add_subdirectory(tests)`, so its `zero-core` interface library is available to every downstream target.
- The runtime's own tests do **not** build as part of the compiler build: `set(ZERO_BUILD_TESTS OFF CACHE BOOL "" FORCE)` is set before the submodule's `add_subdirectory`.
- A new test file `tests/test_runtime_bridge.cpp` exists. Its binary is wired into CMake and registered with `add_test(...)` so `ctest` picks it up.
- The new test:
  - `#include <zero/zero.hpp>` (from the submodule)
  - Allocates two contiguous F32 tensors of shape `[4]` with known values
  - Calls `zero::ops::add(a, b, c)` and asserts the returned `Status::is_ok()`
  - Asserts the four output bytes match the expected element-wise sum bit-for-bit
- Every pre-existing test target still builds and runs (no source changes to existing tests are made by this spec).
- `ctest` registration is added for every existing test binary as well, since they are currently built but not run by `ctest`. *(Side-cleanup, but cheap to do correctly once.)*

## 3. API surface

Files modified:

1. `CMakeLists.txt` — C++20 bump, reorder `add_subdirectory(external/core-runtime)` to run before `src/` and `tests/`, set `ZERO_BUILD_TESTS=OFF` before adding the submodule.
2. `tests/CMakeLists.txt` — add the new test target, add `add_test(NAME … COMMAND …)` calls for the new test and for every existing one.

Files added:

3. `tests/test_runtime_bridge.cpp` — the smoke test described above.

No source code under `src/`, `include/`, or `runtime/` is changed by this spec. No new public symbols are introduced.

### Sketch of the test file

```cpp
// tests/test_runtime_bridge.cpp
#include <zero/zero.hpp>
#include <cstdio>
#include <cstdint>

using namespace zero;

#define ASSERT(cond, msg) /* same custom assertion as runtime tests */

int main() {
    int64_t shape[] = {4};
    Tensor a   = Tensor::alloc(shape, 1, DType::F32);
    Tensor b   = Tensor::alloc(shape, 1, DType::F32);
    Tensor out = Tensor::alloc(shape, 1, DType::F32);
    for (int i = 0; i < 4; ++i) {
        static_cast<float*>(a.data)[i] = static_cast<float>(i + 1);
        static_cast<float*>(b.data)[i] = 10.0f;
    }
    ASSERT(ops::add(a, b, out).is_ok(), "ops::add returns ok");
    ASSERT(static_cast<float*>(out.data)[0] == 11.0f, "add[0] = 11");
    ASSERT(static_cast<float*>(out.data)[3] == 14.0f, "add[3] = 14");
    a.free(); b.free(); out.free();
    return 0;  // or 1 on failure
}
```

## 4. Acceptance tests

The acceptance is a clean `cmake -B build && cmake --build build && ctest --test-dir build` cycle in which:

1. CMake configure succeeds with `-- C++ Standard: 20` logged.
2. CMake configure reports `-- Found core-runtime submodule` (the existing check).
3. The build produces every pre-existing test binary plus the new `test_runtime_bridge`.
4. `ctest` discovers and runs every test binary, and all of them pass.
5. The runtime's own `ctest` does not run as part of this build (confirmed by absence of the `Zero*Test` binaries in `build/external/core-runtime/`).

## 5. Out of scope

- **No changes to the compiler frontend or backend.** Lexer, parser, sema, lowering, IR, interpreter, diagnostics — all untouched.
- **No `Status` plumbing through the IR or interpreter.** That is spec 003.
- **No source spans on IR nodes.** That is spec 002.
- **No new IR opcodes.** `TENSOR_ADD` already exists in the enum; we do not yet wire its interpreter handler to a real runtime call.
- **No changes to the compiler's `runtime/` adapter directory.** Still print/log wrappers.
- **No CI workflow file.** That is its own spec (mirror of the runtime's spec 007), to come after the first real compiler spec lands.
- **No version bump on the compiler.** The compiler is at v0.1.0 and stays there until a real feature spec ships.
- **No coverage of `gather`, `scatter`, `Generator`, `contiguous`.** This spec exercises one op (`add`) — the minimum to prove the bridge.
- **No fix for `mpp_status.md`.** It is dated and superseded by `CURRENT_STATE.md`; we leave it in place rather than touch it here.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) `ZERO_BUILD_TESTS=OFF` is forced rather than left to default, so the compiler build never accidentally runs the runtime's test suite; (b) ctest registration is added for *every* test binary in `tests/CMakeLists.txt` as a one-shot side-cleanup, since the existing tests are built but never run by `ctest` today — fixing this is a 5-line addition and saves a separate spec; (c) the new test uses the same custom `ASSERT` macro style as the runtime's tests rather than introducing gtest, to keep the compiler's test dependency surface at zero.
