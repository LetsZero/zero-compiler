# Zero Examples

Example programs demonstrating Zero's features.

## Running Examples

```bash
# Run any example
.\build\bin\Debug\zeroc.exe examples\calculator.zero

# Dump IR for debugging
.\build\bin\Debug\zeroc.exe --dump-ir examples\hello.zero
```

## Examples

### 1. hello.zero

Simple arithmetic that returns 30 (10 + 20):

```zero
fn main() {
    let x = 10
    let y = 20
    return x + y
}
```

### 2. calculator.zero **(NEW)**

Full calculator demonstrating all arithmetic operations:

- Addition, subtraction, multiplication, division
- Compound expressions with parentheses
- Nested calculations
- Colored output with `log()`

### 3. hello_world.zero

Display module demonstration:

- `print()` for output
- `log()` with colors: `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`

### 4. error_demo.zero

Error handling showcase with "Frame & Focus" diagnostics.

### 5. logging_demo.zero

Advanced logging with semantic colors and ANSI formatting.

### 6. train_linear.zero

**Gradient descent, in Zero.** A one-parameter model `w * x` learns to fit a
target by minimising squared error — hand-written gradient, `while` loop,
variable assignment. `w` converges 0 → 3.0. No autograd, no runtime magic.

### 7. train_features.zero

Multi-feature linear regression (`pred = sum(x * w)`); the parameter is now a
vector and `w` converges toward `[1, 2, 3]`. Trains with only existing ops
(elementwise mul, `sum`, scalar broadcast, assignment) — no `matmul`/transpose.

### 8. train_layer.zero

A matmul-based linear layer with multiple outputs (`pred = matmul(x, W)`),
trained with a hand-written weight gradient `grad_W = transpose(x) @ err`.
`pred` converges to the target `[1, 3]`. Uses `matmul` + `transpose`; still no
autograd.

### 9. train_mlp.zero

A **2-layer MLP with a fully hand-written backward pass** — backprop through
`relu` via `step` (relu's derivative). `pred` converges to the target. This is
the manual reference that autograd will need to reproduce. Uses `matmul`,
`transpose`, `relu`, `step`.

### 10. struct_param.zero

First use of `struct`: a `Param { value, grad }` bundles a weight with its
gradient — the shape autograd will use. Trains a 3-feature regression; `value`
converges toward `[1, 2, 3]`.

### 11. list_in_zero.zero

**The erosion rule, applied to data structures.** A growable `List` written
entirely in Zero — backed by a tensor (storage) and a length tensor (count),
with `push` and `get` as ordinary Zero functions. The compiler has no concept
of a "list"; it sees only `Struct` + `Tensor` + element indexing (`t[i]`,
`t[i] = x`). This is the same pattern `EROSION_RULES.md` describes for List,
proven working.

### 12. autograd_mlp.zero

**Reverse-mode autograd, written entirely in Zero.** The same 2-layer relu MLP
as `train_mlp.zero`, trained to the same target — but with *no hand-written
backward pass*. A `Tape` struct holds two `tensorarray` pools (Var values +
grads); a Var is an integer handle; forward ops record an integer tag and
`backward` walks the tape in reverse with a tag dispatch (Zero has no closures —
"what to do backward" is data, not behavior). The compiler has no concept of
autograd: it sees `Struct` + `Tensor` + `tensorarray` + control flow. This is
the erosion rule applied to the Phase-1 headline feature. See
[../docs/AUTOGRAD_DESIGN.md](../docs/AUTOGRAD_DESIGN.md).

## Language Features

```zero
// Variables
let x = 42
let pi = 3.14
let msg = "Hello"

// Arithmetic
let result = (a + b) * 2

// Output
print("Value:", result)
log("Success!", color="green")

// Functions
fn add(a: int, b: int) -> int {
    return a + b
}
```
