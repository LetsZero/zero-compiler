# Zero Runtime - Print Functions

## Overview

The Zero runtime provides a comprehensive set of print functions with support for:

- **F-Strings**: String interpolation
- **Trace Mode**: Debug output with `[TRACE]` prefix
- **Pipe Operator**: Print piped values with labels

---

## API Reference

### Basic Functions

#### `zero_print(message)`

Basic print to stdout with automatic newline.

```cpp
void zero_print(const char* message);
```

#### `zero_log(message, color, ansi)`

Colored output with ANSI support.

```cpp
void zero_log(const char* message, const char* color, const char* ansi);
```

**Colors**: `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`

---

### Enhanced Print Functions

#### `zero_print_traced(message, trace)`

Print with optional trace prefix.

```cpp
void zero_print_traced(const char* message, bool trace);
```

| `trace` | Output            |
| ------- | ----------------- |
| `false` | `message`         |
| `true`  | `[TRACE] message` |

#### `zero_print_piped(value, label)`

Print piped value with optional label.

```cpp
void zero_print_piped(const char* value, const char* label);
```

| Label             | Output          |
| ----------------- | --------------- |
| `nullptr` or `""` | `value`         |
| `"result"`        | `result: value` |

#### `zero_print_fstring(parts, count)`

Print concatenated f-string parts.

```cpp
void zero_print_fstring(const char** parts, int count);
```

#### `zero_print_ex(message, mode, extra)`

Unified API for all print modes.

```cpp
void zero_print_ex(const char* message, int mode, const char* extra);
```

| Mode | Description                   |
| ---- | ----------------------------- |
| `0`  | Normal print                  |
| `1`  | Trace mode (`[TRACE]` prefix) |
| `2`  | Piped mode (`extra` = label)  |

---

## Error Handling

All functions include null-pointer safety:

| Error              | Message                                               |
| ------------------ | ----------------------------------------------------- |
| Null message       | `[RUNTIME ERROR] Attempted to print null pointer`     |
| Null value (piped) | `[RUNTIME ERROR] Attempted to print null piped value` |
| Invalid f-string   | `[RUNTIME ERROR] Invalid f-string parts`              |
| Unknown color      | `[RUNTIME WARNING] Unknown color name: {name}`        |

---

## Zero Syntax (Planned)

```zero
// Normal
print("Hello")

// F-String
print(f"Value: {x}")

// Trace
print("Debug info", trace=true)

// Pipe Operator
result |> print(msg="output")
```

---

## Testing

```powershell
.\build\bin\Debug\test_print_enhanced.exe
```
