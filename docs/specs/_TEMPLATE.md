# Spec NNN: <feature name>

**Status:** Draft | Approved | Implemented | Amended
**Depends on:** (other spec numbers, or "none")
**PR:** (link once opened)
**Author:** (human owner)

---

## 1. Goal

One paragraph. What this feature is and why it must exist in the core runtime (i.e. why it fails the freeze rule of "can it be expressed in Zero?"). If you cannot justify it in three sentences, the feature does not belong here.

## 2. Invariants

Bulleted list of properties that must hold after this change is merged. These are the unambiguous truths the tests will check. Examples:

- Adding a new `DType` value never changes the numeric value of an existing one.
- `dtype_size(F8_E4M3) == 1`.
- `Tensor::alloc` with an unsupported dtype returns `Tensor::empty()`.

Invariants are written as statements of fact, not goals. If you cannot phrase it as "X is true after this change," it is not an invariant — it is a wish.

## 3. API surface

Exact code. Every public symbol added, modified, or removed by this spec. Include signatures, doc comments, and the file path each symbol lives in. This section is what the AI implements verbatim. Nothing outside this section may be added by the implementation.

```cpp
// include/zero/core/example.hpp

namespace zero {

// One-line purpose. Failure mode if any.
Status example_op(const Tensor& input, Tensor& output, Stream* stream = nullptr) noexcept;

} // namespace zero
```

If a symbol is removed or renamed, list it under a separate **Removed** subheading with the rationale.

## 4. Acceptance tests

List of test cases in English, one per invariant or behavior. The AI translates these to concrete tests during the test-writing phase. Each test must map back to an invariant in section 2.

1. (invariant 2.1) Calling `example_op` on a contiguous F32 tensor produces the expected bit-exact output against a scalar reference loop.
2. (invariant 2.2) Calling `example_op` with mismatched dtypes returns `Status::DTypeMismatch` and does not write to `output`.
3. ...

Tests are **derived from this section, not from the implementation.** If the implementation passes a test that this section did not require, the test is wrong.

## 5. Out of scope

Explicit list of things this spec does **not** do. This section is binding — if a prompt later asks for something here, stop and ask before doing it.

- Does not add kernels for `F8_E5M2` (separate spec).
- Does not change the C ABI surface.
- Does not add Python bindings or any FFI surface.

If you find yourself wanting to add "while I'm here," it goes in this list instead.

## 6. Open questions

Questions the human owner has not yet decided. The AI must **not** pick on its own — it must surface these and wait for an answer.

- Should `example_op` accept rank-0 tensors (scalars), or reject them?
- ...

When a question is resolved, move the answer into the relevant section above and delete it from this list.

---

## Amendment log

Once this spec is `Implemented`, edits require an entry here. Format:

- `YYYY-MM-DD` — what changed, why, and which PR.
