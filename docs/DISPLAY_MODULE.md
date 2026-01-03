# **Zero Display & Diagnostics Specification**

**Core Principle:** 
The Display Module is the primary interface between the Zero runtime and the terminal. It is divided into **Standard Output (`print`)**, **Enhanced Logging (`log`)**, and **Unified Diagnostics (Errors)**.

---

# **1. Standard Output: `print`**

The `print` function is the basic utility for writing data to `stdout`.

*   **Behavior:** Appends a newline (`\n`) automatically.
*   **Constraints:** No color support. It outputs raw string data or string-converted primitives.
*   **Syntax:**
    ```rust
    use display

    print("Hello, World")
    print(1024)
    ```

---

# **2. Enhanced Logging: `log`**

The `log` function is a built-in utility for formatted and highlighted terminal output.

*   **Behavior:** High-level output with support for semantic coloring and ANSI escape sequences.
*   **Parameters:**
    *   `message`: The string to display.
    *   `color` (Optional): Named colors (`"red"`, `"green"`, `"yellow"`, `"blue"`, `"magenta"`, `"cyan"`).
    *   `ansi` (Optional): Direct ANSI code injection for custom formatting.

*   **Syntax:**
    ```rust
    use display

    // Named color logging
    log("Task Complete", color="green")
    log("System Warning", color="yellow")

    // Direct ANSI support
    log("Bold Red Text", ansi="\x1b[1;31m")
    ```

---

# **3. Error Handling & Formatting**

Zero uses a **"Frame & Focus"** error format. This format is hard-coded into the compiler and runtime to ensure that every error—whether a Syntax Error, Type Error, or Runtime Crash—looks identical.

### **The Visual Standard**
```text
[ ERROR ] <ErrorType> in '<file_name>'
  --> Line <num>, Col <num>

   <line_prev> |  <code_context>
   <line_err>  |  <offending_code>
               |  <visual_pointer_^^^^>
               |  [ Focus ]: <specific_reason_for_failure>
               |  [ Help ]: <suggested_fix>

  (Trace): <call_stack_if_applicable>
```

### **Error Categories**
1.  **Lexical/Syntax Errors:** Triggered when the parser encounters invalid characters or structure.
2.  **Type Errors:** Triggered when operations are performed on incompatible types (e.g., adding a string to an integer).
3.  **Runtime Errors:** Triggered during execution (e.g., Out of Memory, Division by Zero).

---

# **4. Implementation Logic (C++ Backend)**

### **The Bridge**
The C++ runtime handles these calls via an FFI (Foreign Function Interface) mapping:

| Zero Function | C++ Runtime Function | Implementation Details |
| :--- | :--- | :--- |
| `print(str)` | `void zero_print(char* s)` | Uses `std::cout` |
| `log(str, col)` | `void zero_log(char* s, char* c)` | Maps string names to ANSI codes |
| `Error` | `class ZeroReporter` | Buffers file lines to print the "Context" snippet |

### **The "No-Dependency" Rule**
*   Zero does not rely on external C++ formatting libraries (like `fmt`).
*   All color logic is handled by internal ANSI mapping:
    *   `"red"` -> `\033[31m`
    *   `"green"` -> `\033[32m`
    *   `"reset"` -> `\033[0m`

---

# **5. Usage Example**

```rust
use display

fn main() {
    print("Initializing Zero...")
    
    let status = true
    if status {
        log("Native C++ Runtime Linked", color="green")
    } else {
        log("Failed to link runtime", color="red")
    }
}
```

**Compiler Output on Failure:**
```text
[ ERROR ] NameError in 'main.zero'
  --> Line 5, Col 9

   4 |  let status = true
   5 |  if statux {
     |     ^^^^^^
     |     [ Focus ]: Identifier 'statux' not found in this scope.
     |     [ Help ]: Did you mean 'status'?
```