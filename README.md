# Zero Compiler

A high-performance compiler for the Zero programming language, designed for AI/ML workloads.

## Project Structure

```
zero-compiler/
├── runtime/           # C++ runtime library (libzerort)
│   ├── runtime.h      # Public API declarations
│   └── runtime.cpp    # Print, log, trace implementations
├── src/diagnostics/   # Error reporting ("Frame & Focus")
├── external/          # Submodules (core-runtime)
├── tests/             # Unit tests
├── docs/              # Documentation
└── examples/          # Sample Zero programs
```

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Runtime Features

| Feature  | Function                         | Example              |
| -------- | -------------------------------- | -------------------- |
| Print    | `zero_print(msg)`                | Basic output         |
| Log      | `zero_log(msg, color, ansi)`     | Colored output       |
| Trace    | `zero_print_traced(msg, trace)`  | Debug with `[TRACE]` |
| Pipe     | `zero_print_piped(value, label)` | Pipeline output      |
| F-String | `zero_print_fstring(parts, n)`   | Interpolation        |

## Testing

```bash
.\build\bin\Debug\test_runtime.exe
.\build\bin\Debug\test_print_enhanced.exe
.\build\bin\Debug\test_errors.exe
```

## License

See [LICENSE](LICENSE) file.
