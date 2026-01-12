# Zero Compiler — MPP Status Report

**Date**: 2026-01-12
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

| Item                            | Status  | Notes                             |
| ------------------------------- | ------- | --------------------------------- |
| Identifiers                     | ✅ DONE | `lexer::Lexer::scan_identifier()` |
| Numbers (int, float)            | ✅ DONE | `INT_LIT`, `FLOAT_LIT` tokens     |
| String literals                 | ✅ DONE | `STRING_LIT` token, escape seqs   |
| Keywords: `fn`, `let`, `return` | ✅ DONE | + `if`, `else`, `while`, `use`    |
| Operators: `+ - * / =`          | ✅ DONE | + `==`, `!=`, `<`, `>`, etc.      |
| Delimiters: `() {} ,`           | ✅ DONE | + `[]`, `:`, `;`, `->`            |

**Note**: Full lexer implementation with 11 passing tests.

---

## 4. Parser (MINIMAL)

| Item                  | Status  | Notes                             |
| --------------------- | ------- | --------------------------------- |
| Function definitions  | ✅ DONE | `parser::Parser::parse_fn_decl()` |
| Variable declarations | ✅ DONE | `parse_let_stmt()`                |
| Function calls        | ✅ DONE | `parse_call()` + keyword args     |
| Return statements     | ✅ DONE | `parse_return_stmt()`             |
| Basic expressions     | ✅ DONE | Precedence climbing               |
| String literals       | ✅ DONE | `StringLiteral` AST node          |
| USE statements        | ✅ DONE | Module imports (no-op for now)    |

---

## 5. AST (LEAN)

| Item          | Status  | Notes                              |
| ------------- | ------- | ---------------------------------- |
| Program       | ✅ DONE | `ast::Program` struct              |
| Function      | ✅ DONE | `ast::FnDecl` struct               |
| Block         | ✅ DONE | `ast::Block` struct                |
| LetStmt       | ✅ DONE | `ast::LetStmt` variant             |
| ReturnStmt    | ✅ DONE | `ast::ReturnStmt` variant          |
| CallExpr      | ✅ DONE | `ast::CallExpr` variant            |
| BinaryExpr    | ✅ DONE | `ast::BinaryExpr` variant          |
| Identifier    | ✅ DONE | `ast::Identifier` variant          |
| Literal       | ✅ DONE | `IntLiteral`, `FloatLiteral`       |
| StringLiteral | ✅ DONE | `ast::StringLiteral` variant (new) |

---

## 6. Type System (ABSOLUTE MINIMUM)

| Item   | Status  | Notes                     |
| ------ | ------- | ------------------------- |
| Int    | ✅ DONE | `types::TypeKind::INT`    |
| Float  | ✅ DONE | `types::TypeKind::FLOAT`  |
| Tensor | ✅ DONE | `types::TypeKind::TENSOR` |
| Void   | ✅ DONE | `types::TypeKind::VOID`   |

---

## 7. Semantic Analysis

| Item                      | Status   | Notes                          |
| ------------------------- | -------- | ------------------------------ |
| Undefined variables check | ✅ DONE  | Reports undefined vars         |
| Function existence check  | ✅ DONE  | Checks fn exists before call   |
| Argument count check      | ✅ DONE  | Validates arg count (variadic) |
| Type compatibility check  | 🟡 BASIC | Int/Float/Unknown types only   |
| Return type correctness   | 🟡 BASIC | Basic return type checking     |
| Built-in functions        | ✅ DONE  | `print`, `log` registered      |

---

## 8. Zero IR (ZIR)

| Item                    | Status  | Notes                                   |
| ----------------------- | ------- | --------------------------------------- |
| SSA form                | ✅ DONE | `ir::Value`, `ir::Function`             |
| Text dump (inspectable) | ✅ DONE | `print_module()`                        |
| `const`                 | ✅ DONE | `CONST_INT`, `CONST_FLOAT`, `CONST_STR` |
| `alloc`                 | ✅ DONE | `ALLOCA` opcode                         |
| `load`                  | ✅ DONE | `LOAD` opcode                           |
| `store`                 | ✅ DONE | `STORE` opcode                          |
| `add`, `mul`            | ✅ DONE | `ADD`, `SUB`, `MUL`, `DIV`              |
| `call`                  | ✅ DONE | `CALL` opcode                           |
| `return`                | ✅ DONE | `RET` opcode                            |
| `tensor_create`         | ✅ DONE | `TENSOR_ALLOC` opcode                   |
| `tensor_matmul`         | ✅ DONE | `TENSOR_MATMUL` opcode                  |
| `tensor_add`            | ✅ DONE | `TENSOR_ADD` opcode                     |

---

## 9. CPU Backend

| Item                         | Status  | Notes                        |
| ---------------------------- | ------- | ---------------------------- |
| ZIR Interpreter              | ✅ DONE | `backend::Interpreter` class |
| Runtime tensor ops execution | ✅ DONE | Linked to core-runtime       |

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

| Item                          | Status  | Notes                               |
| ----------------------------- | ------- | ----------------------------------- |
| `zeroc <file.zero>`           | ✅ DONE | Implementation of `compile_and_run` |
| `zeroc --dump-ir <file.zero>` | ✅ DONE | IR printing support                 |
| `zeroc --help`                | ✅ DONE | CLI argument parsing                |

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

| Category             | Done   | Partial | TODO  |
| -------------------- | ------ | ------- | ----- |
| 1. Core Runtime      | 6      | 0       | 0     |
| 2. Source Management | 4      | 0       | 0     |
| 3. Lexer             | 6      | 0       | 0     |
| 4. Parser            | 7      | 0       | 0     |
| 5. AST               | 10     | 0       | 0     |
| 6. Type System       | 4      | 0       | 0     |
| 7. Semantic Analysis | 4      | 2       | 0     |
| 8. Zero IR           | 13     | 0       | 0     |
| 9. CPU Backend       | 2      | 0       | 0     |
| 10. Tensor Runtime   | 6      | 0       | 0     |
| 11. CLI Driver       | 3      | 0       | 0     |
| 12. Tests            | 2      | 0       | 5     |
| **TOTAL**            | **67** | **2**   | **5** |

---

## Immediate Next Steps (Priority Order)

1. ~~**Complete Core Runtime** - Add `zero_alloc`, `zero_free`~~ ✅ (via core-runtime)
2. ~~**Source Management** - File loader, SourceID, Span~~ ✅
3. ~~**Lexer** - Minimal tokens for NN code~~ ✅ (+ strings, USE keyword)
4. ~~**Parser** - Recursive descent for core grammar~~ ✅ (+ keyword args)
5. ~~**AST** - Plain structs for nodes~~ ✅ (+ StringLiteral)
6. ~~**Type System** - Int, Float, Tensor, Void~~ ✅
7. ~~**Semantic Analysis** - Basic checks~~ ✅ (print/log built-ins)
8. ~~**ZIR** - SSA IR with tensor ops~~ ✅ (+ CONST_STR)
9. ~~**CPU Backend** - Simple interpreter~~ ✅ (string support)
10. ~~**Tensor Runtime** - Initialize core-runtime submodule~~ ✅ (core-runtime v1.2)
11. ~~**CLI** - `zeroc run`, `--dump-ir`, `--help`~~ ✅
12. **Tests** - End-to-end NN forward pass (TODO)
