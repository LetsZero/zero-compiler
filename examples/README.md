# Zero Examples

This directory contains example programs demonstrating Zero's features.

## Examples

### 1. hello_world.zero

Basic demonstration of the display module:

- `print()` function for basic output
- `log()` function with named colors
- Direct ANSI code formatting

**Run**: (Once compiler is complete)

```bash
zero run examples/hello_world.zero
```

### 2. logging_demo.zero

Advanced logging examples:

- Semantic color usage (info, warning, error)
- Build process logging
- Debug information display
- Custom ANSI formatting

### 3. error_demo.zero

Error handling demonstration:

- Shows commented examples of different error types
- Demonstrates Zero's "Frame & Focus" error format
- Helpful for understanding diagnostic messages

## Display Module Features

### Print Function

```rust
print("Hello, World!")
print(42)
```

- Basic stdout output
- Automatic newline
- Works with strings and numbers

### Log Function

```rust
// Named colors
log("Success!", color="green")
log("Warning!", color="yellow")
log("Error!", color="red")

// Direct ANSI codes
log("Bold text", ansi="\x1b[1m")
```

**Supported Colors**:

- `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`

### Error Reporting

Zero automatically provides detailed error messages with:

- Source file context
- Line and column numbers
- Visual pointers (^^^)
- Helpful suggestions
- Color-coded output

## Future Examples

Once the compiler is complete, we'll add:

- `tensor_operations.zero` - Tensor manipulation
- `neural_network.zero` - Simple neural network
- `autograd_demo.zero` - Automatic differentiation
- `training_loop.zero` - Model training example
