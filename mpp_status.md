# Zero Compiler — MPP Status Report

**Date**: 2026-01-06
**Goal**: Minimal Public Prototype (CPU-only, no GPU, no MLIR, no LLVM)

---

## Status Legend

- ✅ **DONE** - Fully implemented
- 🟡 **PARTIAL** - Partially done
- ❌ **TODO** - Not started

---

## 1. Core Runtime

| Item              | Status  | Notes                                                                                        |
| ----------------- | ------- | -------------------------------------------------------------------------------------------- |
| `print`           | ✅ DONE | `zero_print`, `zero_print_traced`, `zero_print_piped`, `zero_print_fstring`, `zero_print_ex` |
| `log`             | ✅ DONE | `zero_log` with ANSI color support                                                           |
| Diagnostics hooks | ✅ DONE | `src/diagnostics/reporter.cpp` - "Frame & Focus" error display                               |
| `zero_alloc`      | ✅ DONE | `core-runtime`: `zero::mem_alloc()` in `core/memory.hpp`                                     |
| `zero_free`       | ✅ DONE | `core-runtime`: `zero::mem_free()` in `core/memory.hpp`                                      |
| Timing utility    | ✅ DONE | Optional but useful                                                                          |

---

## 2. Source Management

| Item                       | Status  | Notes                                         |
| -------------------------- | ------- | --------------------------------------------- |
| Source file loader         | ✅ DONE | `source::SourceManager::load()` in source.hpp |
| Source ID                  | ✅ DONE | `source::SourceID` type (uint32_t)            |
| Line/column mapping        | ✅ DONE | `SourceFile::offset_to_line_col()`            |
| Span (start, end, file_id) | ✅ DONE | `source::Span` struct with merge support      |

---

## 3. Lexer (MINIMAL)

| Item                            | Status  | Notes                       |
| ------------------------------- | ------- | --------------------------- |
| Identifiers                     | ❌ TODO | Was implemented but deleted |
| Numbers (int, float)            | ❌ TODO | Was implemented but deleted |
| Keywords: `fn`, `let`, `return` | ❌ TODO | Was implemented but deleted |
| Operators: `+ - * / =`          | ❌ TODO | Was implemented but deleted |
| Delimiters: `() {} ,`           | ❌ TODO | Was implemented but deleted |

**Note**: Lexer was previously implemented for F-strings/pipes but was removed. Need to re-implement with MPP scope.

---

## 4. Parser (MINIMAL)

| Item                  | Status  | Notes |
| --------------------- | ------- | ----- |
| Function definitions  | ❌ TODO | -     |
| Variable declarations | ❌ TODO | -     |
| Function calls        | ❌ TODO | -     |
| Return statements     | ❌ TODO | -     |
| Basic expressions     | ❌ TODO | -     |

---

## 5. AST (LEAN)

| Item       | Status  | Notes |
| ---------- | ------- | ----- |
| Program    | ❌ TODO | -     |
| Function   | ❌ TODO | -     |
| Block      | ❌ TODO | -     |
| LetStmt    | ❌ TODO | -     |
| ReturnStmt | ❌ TODO | -     |
| CallExpr   | ❌ TODO | -     |
| BinaryExpr | ❌ TODO | -     |
| Identifier | ❌ TODO | -     |
| Literal    | ❌ TODO | -     |

---

## 6. Type System (ABSOLUTE MINIMUM)

| Item   | Status  | Notes |
| ------ | ------- | ----- |
| Int    | ❌ TODO | -     |
| Float  | ❌ TODO | -     |
| Tensor | ❌ TODO | -     |
| Void   | ❌ TODO | -     |

---

## 7. Semantic Analysis

| Item                      | Status  | Notes |
| ------------------------- | ------- | ----- |
| Undefined variables check | ❌ TODO | -     |
| Function existence check  | ❌ TODO | -     |
| Argument count check      | ❌ TODO | -     |
| Type compatibility check  | ❌ TODO | -     |
| Return type correctness   | ❌ TODO | -     |

---

## 8. Zero IR (ZIR)

| Item                    | Status  | Notes |
| ----------------------- | ------- | ----- |
| SSA form                | ❌ TODO | -     |
| Text dump (inspectable) | ❌ TODO | -     |
| `const`                 | ❌ TODO | -     |
| `alloc`                 | ❌ TODO | -     |
| `load`                  | ❌ TODO | -     |
| `store`                 | ❌ TODO | -     |
| `add`, `mul`            | ❌ TODO | -     |
| `call`                  | ❌ TODO | -     |
| `return`                | ❌ TODO | -     |
| `tensor_create`         | ❌ TODO | -     |
| `tensor_matmul`         | ❌ TODO | -     |
| `tensor_add`            | ❌ TODO | -     |

---

## 9. CPU Backend

| Item                         | Status  | Notes |
| ---------------------------- | ------- | ----- |
| ZIR Interpreter              | ❌ TODO | -     |
| Runtime tensor ops execution | ❌ TODO | -     |

---

## 10. Minimal Tensor Runtime (CPU)

| Item            | Status  | Notes                                                            |
| --------------- | ------- | ---------------------------------------------------------------- |
| Tensor struct   | ✅ DONE | `core-runtime`: `zero::Tensor` in `core/tensor.hpp`              |
| Heap allocation | ✅ DONE | `core-runtime`: `mem_alloc()` + pluggable `Allocator` interface  |
| Shape storage   | ✅ DONE | `core-runtime`: `Tensor::shape[]` and `Tensor::strides[]` arrays |
| `add` op        | ✅ DONE | `core-runtime`: `zero::ops::add()` in `ops/elementwise.hpp`      |
| `matmul` op     | ✅ DONE | `core-runtime`: `zero::ops::matmul()` in `ops/matmul.hpp`        |
| `relu` op       | ✅ DONE | `core-runtime`: `zero::ops::relu()` in `ops/elementwise.hpp`     |

**Note**: `external/core-runtime` v1.2 (SEMI-FROZEN) provides complete tensor primitives.

---

## 11. Compiler Driver (CLI)

| Item                     | Status  | Notes |
| ------------------------ | ------- | ----- |
| `zero run file.zero`     | ❌ TODO | -     |
| `zero check file.zero`   | ❌ TODO | -     |
| `zero emit-ir file.zero` | ❌ TODO | -     |

---

## 12. Tests

| Item                 | Status  | Notes                                         |
| -------------------- | ------- | --------------------------------------------- |
| Lexer sanity         | ❌ TODO | -                                             |
| Parser sanity        | ❌ TODO | -                                             |
| Semantic errors      | ❌ TODO | -                                             |
| IR dump stability    | ❌ TODO | -                                             |
| Tiny NN forward pass | ❌ TODO | -                                             |
| Runtime tests        | ✅ DONE | `test_runtime.cpp`, `test_print_enhanced.cpp` |
| Diagnostics tests    | ✅ DONE | `test_errors.cpp`                             |

---

## Summary

| Category             | Done   | Partial | TODO   |
| -------------------- | ------ | ------- | ------ |
| 1. Core Runtime      | 6      | 0       | 0      |
| 2. Source Management | 4      | 0       | 0      |
| 3. Lexer             | 0      | 0       | 5      |
| 4. Parser            | 0      | 0       | 5      |
| 5. AST               | 0      | 0       | 9      |
| 6. Type System       | 0      | 0       | 4      |
| 7. Semantic Analysis | 0      | 0       | 5      |
| 8. Zero IR           | 0      | 0       | 12     |
| 9. CPU Backend       | 0      | 0       | 2      |
| 10. Tensor Runtime   | 6      | 0       | 0      |
| 11. CLI Driver       | 0      | 0       | 3      |
| 12. Tests            | 2      | 0       | 5      |
| **TOTAL**            | **18** | **0**   | **50** |

---

## Immediate Next Steps (Priority Order)

1. ~~**Complete Core Runtime** - Add `zero_alloc`, `zero_free`~~ ✅ (via core-runtime)
2. ~~**Source Management** - File loader, SourceID, Span~~ ✅
3. **Lexer** - Minimal tokens for NN code
4. **Parser** - Recursive descent for core grammar
5. **AST** - Plain structs for nodes
6. **Type System** - Int, Float, Tensor, Void
7. **Semantic Analysis** - Basic checks
8. **ZIR** - SSA IR with tensor ops
9. **CPU Backend** - Simple interpreter (link to core-runtime ops)
10. ~~**Tensor Runtime** - Initialize core-runtime submodule or build minimal~~ ✅ (core-runtime v1.2)
11. **CLI** - `zero run`, `zero check`, `zero emit-ir`
12. **Tests** - End-to-end NN forward pass
