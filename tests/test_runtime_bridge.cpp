/**
 * @file test_runtime_bridge.cpp
 * @brief Acceptance test for spec 001 — runtime bridge buildability.
 *
 * Proves that this repo can include the frozen Core Runtime (v1.4.0)
 * via the external/core-runtime submodule, allocate a contiguous F32
 * tensor, and call a runtime op directly. If this binary builds and
 * passes, the build foundation is real.
 *
 * Style matches the runtime's own tests: a custom ASSERT macro that
 * tallies failures and returns nonzero on any miss. No external test
 * framework.
 */

#include <zero/zero.hpp>

#include <cstdio>
#include <cstdint>

using namespace zero;

static int failures = 0;

#define ASSERT(cond, msg)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL: %s\n", msg);                                     \
            ++failures;                                                         \
        } else {                                                                \
            std::printf("PASS: %s\n", msg);                                     \
        }                                                                       \
    } while (0)

int main() {
    std::printf("=== Spec 001 — runtime bridge smoke test ===\n\n");

    int64_t shape[] = {4};
    Tensor a   = Tensor::alloc(shape, 1, DType::F32);
    Tensor b   = Tensor::alloc(shape, 1, DType::F32);
    Tensor out = Tensor::alloc(shape, 1, DType::F32);

    // Inputs: a = {1, 2, 3, 4}; b = {10, 10, 10, 10}
    for (int i = 0; i < 4; ++i) {
        static_cast<float*>(a.data)[i] = static_cast<float>(i + 1);
        static_cast<float*>(b.data)[i] = 10.0f;
    }

    // Call into the frozen runtime directly. This is the first time anything
    // in the compiler tree exercises the v1.4 surface end-to-end.
    Status s = ops::add(a, b, out);
    ASSERT(s.is_ok(), "ops::add returned ok()");

    const float* o = static_cast<const float*>(out.data);
    ASSERT(o[0] == 11.0f, "add[0] = 1 + 10 = 11");
    ASSERT(o[1] == 12.0f, "add[1] = 2 + 10 = 12");
    ASSERT(o[2] == 13.0f, "add[2] = 3 + 10 = 13");
    ASSERT(o[3] == 14.0f, "add[3] = 4 + 10 = 14");

    a.free(); b.free(); out.free();

    std::printf("\n=== Result: %d failure(s) ===\n", failures);
    return failures == 0 ? 0 : 1;
}
