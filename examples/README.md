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
