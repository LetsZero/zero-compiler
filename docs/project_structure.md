## Project Structure

```text
zero/
├── CMakeLists.txt             # Main build configuration
├── bin/                       # Compiled binaries (zero-cli)
├── docs/                      # Documentation
│   ├── ZERO_PROTOTYPE_PLAN.md
│   └── DISPLAY_MODULE.md
├── examples/                  # Sample Zero programs
│   └── hello_world.zero
├── include/                   # C++ Header files (.hpp)
│   ├── ast/                   # Abstract Syntax Tree nodes
│   ├── codegen/               # LLVM IR Generation logic
│   ├── diagnostics/           # Error formatting engine
│   ├── lexer/                 # Tokenizer definitions
│   ├── parser/                # Recursive Descent Parser
│   └── sema/                  # Semantic Analysis & Type Checker
├── src/                       # C++ Implementation files (.cpp)
│   ├── main.cpp               # Compiler Entry Point (CLI)
│   ├── lexer/                 
│   ├── parser/                
│   ├── codegen/               
│   ├── diagnostics/           # Implementation of "Frame & Focus" errors
│   └── sema/                  
├── runtime/                   # The C++ "Engine" (Linked to every Zero app)
│   ├── CMakeLists.txt
│   ├── runtime.cpp            # Implementation of print() and log()
│   └── runtime.h              # C-linkage headers for LLVM
├── stdlib/                    # The Standard Library (Written in Zero)
│   ├── display.zero           # User-facing 'use display' module
│   └── math.zero
└── tests/                     # Unit tests for compiler components
    ├── lexer_test.cpp
    └── parser_test.cpp
```

---

### **Key Components Breakdown**

#### **1. `src/diagnostics/` (The Error Engine)**
This is where you implement the "Unique Format" you described.
*   It should have a function like `reportError(SourceLoc loc, String message, String help)`.
*   It reads the source file, finds the line number, and prints the `^^^` pointers and colors to the terminal.

#### **2. `runtime/runtime.cpp` (The Bridge)**
This file is compiled once into `libzerort.a`. When you compile a Zero program, the compiler tells LLVM to link against this file.
*   **`zero_print`**: Uses `std::cout` (No colors).
*   **`zero_log`**: Uses ANSI escape codes (Colors).

#### **3. `stdlib/display.zero`**
This is what the user sees. It maps the language keywords to the internal runtime functions.
```rust
// display.zero
extern fn zero_print(msg: string)
extern fn zero_log(msg: string, color: string)

fn print(msg: string) {
    zero_print(msg)
}

fn log(msg: string, color: string) {
    zero_log(msg, color)
}
```

#### **4. `src/codegen/`**
This part of the compiler uses the LLVM C++ API. It takes your parsed Zero code and generates the machine-level "Calls" to the `runtime.cpp` functions.

#### **5. `CMakeLists.txt`**
Your build file will need to find LLVM on your system:
```cmake
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
# ... link libraries ...
```