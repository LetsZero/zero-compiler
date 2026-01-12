# Zero Compiler

A high-performance compiler for the Zero programming language, designed for AI/ML workloads.

## Quick Start

```bash
# Build the compiler
cmake -B build
cmake --build build --config Debug

# Run a Zero program
.\build\bin\Debug\zeroc.exe examples\calculator.zero

# Dump IR for debugging
.\build\bin\Debug\zeroc.exe --dump-ir examples\hello.zero
```

## Language Features

### Variables and Types

```zero
let x = 42          // Integer
let y = 3.14        // Float
let msg = "Hello"   // String
```

### Arithmetic

```zero
let sum = a + b     // Addition
let diff = a - b    // Subtraction
let prod = a * b    // Multiplication
let quot = a / b    // Division
let expr = (a + b) * 2  // Grouped expressions
```

### Control Flow

```zero
if x > 10 {
    print("Large")
} else {
    print("Small")
}

while count > 0 {
    count = count - 1
}
```

### Functions

```zero
fn add(a: int, b: int) -> int {
    return a + b
}

fn main() {
    print(add(10, 20))
    return 0
}
```

### Built-in Functions

| Function                | Description    | Example                    |
| ----------------------- | -------------- | -------------------------- |
| `print(...)`            | Print values   | `print("Hello", 42)`       |
| `log(msg, color="...")` | Colored output | `log("OK", color="green")` |

**Colors**: `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`

## Examples

- `examples/hello.zero` - Basic arithmetic (returns 30)
- `examples/hello_world.zero` - Print and log demo
- `examples/calculator.zero` - Full arithmetic operations
- `examples/error_demo.zero` - Error handling showcase

## Project Structure

```
zero-compiler/
├── src/               # Compiler source
│   ├── lexer/         # Tokenization
│   ├── parser/        # AST construction
│   ├── sema/          # Semantic analysis
│   ├── ir/            # IR generation
│   ├── backend/       # Interpreter
│   └── driver/        # CLI (zeroc)
├── external/          # core-runtime submodule
├── tests/             # Unit tests
└── examples/          # Sample Zero programs
```

## Testing

```bash
.\build\bin\Debug\test_lexer.exe      # 11 tests
.\build\bin\Debug\test_parser.exe     # 13 tests
.\build\bin\Debug\test_sema.exe       # 10 tests
.\build\bin\Debug\test_ir.exe         # 10 tests
.\build\bin\Debug\test_backend.exe    # 7 tests
```

## License

See [LICENSE](LICENSE) file.
