# **ZERO Prototype Roadmap (C++ Native Version)**

**Goal:**
Build a high-performance native compiler for Zero using C++. This prototype will compile Zero code into machine code via LLVM, utilizing a C++ runtime for tensor operations.

---

# **1. Architecture Overview**

### **Layer A — Implementation Layer (C++ latest stable version)**
The "Engine" that compiles and runs Zero code:
*   **Frontend:** Hand-written Recursive Descent Parser or Flex/Bison.
*   **IR Bridge:** Direct use of `llvm::IRBuilder`.
*   **JIT Engine:** LLVM OrcJIT or LLVM MCJIT for immediate execution.
*   **Memory Manager:** C++ based heap management for Tensors.

### **Layer B — Language Standard Library (Zero itself)**
Written in Zero, compiled by Layer A:
*   `tensor.zero`, `autograd.zero`, `nn.zero`.

---

# **2. Development Flow**

```
(1) Setup LLVM C++ Environment (CMake)
(2) C++ Lexer & Parser (AST)
(3) Semantic Analysis (Symbol Tables in C++)
(4) LLVM Codegen (The C++ LLVM API)
(5) Native Runtime (C++ Memory/Math)
(6) Zero Standard Library (Written in Zero)
(7) Ship Native Binary (zero-cli)
```

---

# **3. Step-by-Step Plan**

### **STEP 1 — Toolchain Setup (C++)**
*   **Build System:** CMake.
*   **Dependencies:** LLVM latest version, `Clang` (for linking), `googletest` (optional).
*   **Output:** A single `zero` compiler binary.

### **STEP 2 — Lexer & Parser (C++)**
*   **Lexer:** Use `std::string_view` for fast tokenization.
*   **Parser:** Build a **Recursive Descent Parser**. 
*   **AST:** Use `std::unique_ptr<ASTNode>` hierarchy to prevent memory leaks.

### **STEP 3 — Semantic Layer (C++)**
*   Implement `SymbolTable` using `std::unordered_map`.
*   **Type Checker:** Verify types before passing to LLVM. Since Zero is for ML, ensure `tensor` is a first-class citizen in the type system.

### **STEP 4 — LLVM Codegen (The Core)**
Instead of Python's `llvmlite`, use:
*   `llvm::LLVMContext`
*   `llvm::Module`
*   `llvm::IRBuilder<>`
*   **Task:** Map Zero functions to `llvm::Function*` and Zero variables to `llvm::Value*`.

### **STEP 5 — The Native Runtime (C++)**
Zero needs a small shared library (`libzerort.so`) written in C++ that the compiler links against.
*   **Tensor Allocation:** `posix_memalign` for SIMD alignment.
*   **Math Kernels:** Simple C++ loops for `add`, `mul`. (Later: link to OpenBLAS/OneDNN).

### **STEP 6 — LLVM Optimization Pass Manager**
Leverage C++ LLVM passes:
*   `PassBuilder` to run `O2` or `O3` pipelines.
*   Focus on **Vectorization** (`LoopVectorizePass`)—this is why you chose C++.

### **STEP 7 — Module System & Header Search**
*   Implement a logic to find and parse `.zero` files.
*   Since it's C++, use `std::filesystem` to manage module paths.

### **STEP 8 — Zero "Struct" & "Impl" (OOP)**
*   Lower Zero `structs` to `llvm::StructType`.
*   Implement "Method Mangling" (e.g., `Tensor::backward` becomes `_ZN6Tensor8backwardE`).

### **STEP 9 — The Tensor Bridge** [EXAMPLE]
The C++ runtime must define a `struct` that matches the Zero-side definition:
```cpp
// C++ side
struct ZeroTensor {
    float* data;
    int64_t* shape;
    int64_t rank;
};
```

### **STEP 10 — Implement the ML Lib (Zero)**
Now switch to writing **Zero code**.
*   Write `autograd.zero` using the compiler you just built.
*   Define high-level `nn.Linear` layers.

### **STEP 11 — JIT vs AOT**
*   **JIT:** Use LLVM OrcJIT so users can run `zero run model.zero` like a script.
*   **AOT:** Use LLVM to emit `.o` files so users can compile standalone binaries.

---

# **4. Why C++ changes the game**

| Feature | Python Prototype | C++ Prototype |
| :--- | :--- | :--- |
| **Speed** | 10x - 100x slower | **Near-zero overhead** |
| **LLVM Access** | Limited (llvmlite) | **Full API access** |
| **Distribution** | Needs Python Env | **Single static binary** |
| **Concurrency** | Blocked by GIL | **Native Threading** |

---

# **5. Updated TL;DR Workflow**

1.  **C++:** Write the `Parser` and `Codegen`.
2.  **C++:** Write the `Runtime` (Memory/Math).
3.  **LLVM:** Optimize the generated IR.
4.  **Zero:** Write the ML logic (Layers/Grads).
5.  **Result:** A lightning-fast, professional-grade ML language toolchain.