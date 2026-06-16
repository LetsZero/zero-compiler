# Autograd — Design Note (the tape)

> **Status: BUILT (Option B).** Decision made and implemented — reverse-mode
> autograd now runs, written entirely in Zero, in `examples/autograd_mlp.zero`,
> covered by `tests/test_autograd.cpp` (per-rule gradient checks + convergence)
> and the primitive by `tests/test_tensor_array.cpp`. This note is kept as the
> rationale. See **"Decision & outcome"** at the bottom for what shipped.
>
> *(Originally: the "think before the hard part" note. `NEXT_STEPS.md` warns
> that "many projects die here"; this decided the tape's shape before any code.)*

## Goal (falsifiable)

> Reproduce `examples/train_mlp.zero` — a 2-layer relu MLP trained to a target —
> **without a hand-written backward pass.** The forward pass is written once;
> gradients are produced automatically. When the autograd version converges to
> the same answer as the hand-written one, autograd works.

The hand-written `train_mlp.zero` is the reference oracle.

---

## What autograd needs, decomposed

Reverse-mode AD (PyTorch-style, runtime tape) has three parts:

1. **A differentiable value** — a value paired with its gradient. We already
   have its shape: the `Param { value: tensor, grad: tensor }` struct. Call the
   general version a **Var**.
2. **A tape** — as the forward pass runs, each op records *what it did* and *on
   which Vars*, so the backward pass can replay it in reverse.
3. **A backward rule per op** — given the output gradient, compute and
   accumulate the input gradients (e.g. for `matmul(a, b)`: `grad_a += grad_out @ bᵀ`,
   `grad_b += aᵀ @ grad_out`).

Two challenges. One is solved on paper; the other is a real fork.

---

## Challenge 1 — encoding "backward" without closures · SOLVED

PyTorch attaches a `grad_fn` **closure** to each output. Zero has no closures
and no function pointers (by design). So how does a tape entry say "to go
backward, do *this*"?

**Answer: a tagged tape + a dispatch.** Don't store behavior; store *data*.
Each tape entry records an integer **op tag** (`ADD=0`, `MUL=1`, `MATMUL=2`,
`RELU=3`, …) plus handles to its input/output Vars. The backward pass is one
function with a dispatch on the tag:

```
fn backward_step(tag, ...) {
    if tag == MATMUL { /* grad_a += grad_out @ bᵀ; grad_b += aᵀ @ grad_out */ }
    else if tag == RELU { /* grad_in += grad_out * step(input) */ }
    else if tag == ADD  { /* grad_a += grad_out; grad_b += grad_out */ }
    ...
}
```

This is exactly the erosion-rule pattern already in the catalog: *"closures → a
Struct of captured values + a free Function"*, *"async/await → a state machine
encoded as a Struct + Function."* A closure becomes **(tag + operand handles)**
data plus **one dispatch function**. No new language feature needed for this
part.

The tape's *control data* — op tags, operand indices, a length counter — are all
just `int`/`float` arrays. We can store and index those **today** (`t[i]`).

---

## Challenge 2 — storing the tape's tensors · THE REAL FORK

The tape references Vars, and each Var holds **tensors of different shapes**
(`x` is `[1,2]`, `W1` is `[2,2]`, `h` is `[1,2]`, a bias might be `[3]`, …). To
walk the tape in reverse, the backward pass must fetch "Var #k" at a **runtime**
index. So autograd needs:

> **a runtime-indexable collection of tensors** (heterogeneous shapes).

We do **not** have this, and — importantly — it is **not composable** from what
we have:

- A `tensor` holds floats, not tensors. `List[float]` *is* a tensor (that's why
  List/Dict belong in Zero, per the earlier discussion) — but **`List[tensor]`
  is a different animal**: you cannot pack variable-shaped tensors into one flat
  tensor without manual offset arithmetic.
- A `struct` can hold several tensors, but its fields are accessed by a
  **compile-time** index (`struct_get` takes a constant). You cannot do
  `pool[runtime_k]` over struct fields.

So this is a genuine expressiveness gap, not a missing library. There are two
honest ways to close it.

### Option A — Flat arena (composable, no new primitive)

Store **all** Var values and grads in one big flat `tensor` (an arena). A Var is
`(offset, numel)` into the arena; shapes live in a parallel `int` array. Backward
reads/writes arena slices by index.

- ✅ Uses only today's primitives (one big tensor + `t[i]` + structs for
  metadata). Nothing added to the language.
- ❌ Every op must do manual offset + shape bookkeeping. `matmul` over arena
  slices means reconstructing 2-D shapes from the shape table by hand. This is
  arena-allocator code written in a tiny language — error-prone, and it would
  **bury the autograd logic in pointer math**. High risk of dying in bookkeeping
  bugs (the exact `NEXT_STEPS` death).

### Option B — A `tensor array` primitive (one small addition)

Add a runtime-indexable collection of tensors as an **interpreter-level**
value (like we did for struct values and `t[i]` — *not* a change to the frozen
runtime). Roughly:

```
let pool = tensorarray(capacity)     // holds tensors, indexed at runtime
ta_set(pool, k, some_tensor)         // store
let t = ta_get(pool, k)              // fetch (any shape)
```

- ✅ Makes the tape natural: the Var pool is a `tensor array`; the tape stores
  integer handles into it; backward fetches `ta_get(pool, k)` and accumulates.
  The autograd code stays readable.
- ✅ **Passes the erosion test.** Unlike List/Dict (which *are* composable from
  tensor+struct, so they belong in Zero), a heterogeneous tensor collection
  *cannot* be composed — so it is a legitimate primitive, the same way `t[i]`
  indexing was. It does not touch the frozen runtime; it's interpreter state.
- ❌ It grows the language's core surface by one concept. That is the cost, and
  it is permanent-ish, so it deserves the scrutiny every primitive gets.

### The distinction that matters

This is the same "primitive vs. compose" question as List/Dict — but the answer
flips, and *why* it flips is the whole point:

| Type | Composable from tensor+struct? | Where it belongs |
|---|---|---|
| `List[float]`, `Dict` | **Yes** (List = tensor+len; Dict = tensors+struct) | **In Zero** (library) |
| Collection of **tensors** | **No** (can't pack varied shapes) | **A primitive** (if we want it) |

So adding a tensor-array primitive is *not* a contradiction of "don't put
List/Dict in the compiler." It's the one piece that genuinely can't be erased.

---

## Recommendation

**Lean Option B** — add a minimal `tensor array` primitive — *unless* we want to
prove the point that it's avoidable. Reasoning: the flat-arena path (A) is
composable but so heavy that it risks turning autograd into an arena-debugging
project, which is precisely the failure `NEXT_STEPS` flags. One small, honest
primitive keeps the autograd code legible and is justified by genuine
non-composability.

A sketch of the build order if we take B:

1. `tensor array` primitive (`tensorarray` / `ta_get` / `ta_set`) — small,
   mirrors the `t[i]` work.
2. A `Var` + tape, **written in Zero** (struct + tensor-array + the tag/dispatch
   pattern). The tape itself is a Zero program, not compiler code.
3. Backward rules for the handful of ops `train_mlp` uses (`matmul`, `relu`,
   `add`/`sub`, elementwise `mul`), as the dispatch.
4. An SGD loop driving it; converge against the hand-written reference.

## Decision & outcome

**Chose Option B** — the single `tensorarray` primitive — and built it. What shipped:

1. **The primitive.** `tensorarray(n)` / `ta_get(arr, k)` / `ta_set(arr, k, t)`:
   a runtime-indexable, mutable collection of whole tensors, threaded through
   the pipeline exactly like `t[i]` (a `TypeKind::TENSOR_ARRAY`, three IR ops
   `TENSOR_ARRAY_NEW/GET/SET`, interpreter-level state — **not** a runtime
   change). It has reference semantics, so a pool passed into a function and
   mutated there is visible to the caller — what the tape needs. Locked by
   `tests/test_tensor_array.cpp` (10 cases).

2. **Autograd, written in Zero.** `examples/autograd_mlp.zero`. A `Tape` struct
   bundles two pools (Var values + grads) and four fixed-capacity tensor
   "columns" (op tag, input a, input b, output handle) plus two `[1]` counters.
   A **Var is just an integer handle.** Forward ops (`ad_matmul`, `ad_relu`,
   `ad_sub`, `ad_mul`, `ad_sum`) compute the value, allocate a Var, and append a
   tape entry. `backward` walks the tape in reverse and **dispatches on the
   integer tag** (Challenge 1, exactly as designed — no closures). The same
   2-layer relu MLP as `train_mlp.zero` converges to the same target **with no
   hand-written backward pass.** Locked by `tests/test_autograd.cpp`: each rule
   checked against a hand-derived gradient, plus end-to-end convergence.

A small, principled side-fix fell out: tensor⊗scalar now types as `tensor` in
sema (`binary_result_type`) instead of `unknown` — it always executed as a
tensor; the type was just imprecise. (Needed so `zeros * 0.0 + scalar` — the
`sum` backward — stays tensor-typed.)

**The compiler still has no concept of "autograd," "Var," or "tape."** It sees
`Struct` + `Tensor` + `tensorarray` + control flow. The erosion thesis holds:
the one thing that genuinely couldn't be composed (a heterogeneous tensor
collection) became one honest primitive; everything else is a Zero program.

### What this does *not* yet cover (honest scope)

- Backward rules exist only for the five ops the MLP uses (`matmul`, `relu`,
  `sub`, `mul`, `sum`). Adding an op = one forward wrapper + one `else if`.
- Fixed tape capacity (16) per step; the graph is rebuilt each step (dynamic,
  PyTorch-style). No `requires_grad`/`detach`, no higher-order grads, no
  graph-level optimization — none of which Phase 1 needs yet.
