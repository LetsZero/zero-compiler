# Spec 004: Source-level tensor literals and `+` dispatch

**Status:** Approved
**Depends on:** spec 001 (build), spec 002 (IR spans), spec 003 (TENSOR_ADD wiring)
**PR:** (pending)
**Author:** Ritwik
**Repo:** zero-compiler

---

## 1. Goal

Make tensor `add` expressible in `.zero` source. Today tensors can only be constructed by building IR by hand — the language itself has no syntax for them. This spec adds the minimum syntactic surface to let a real `.zero` program do tensor arithmetic end-to-end:

```zero
fn main() {
    let a = tensor([1.0, 2.0, 3.0, 4.0]);
    let b = tensor([10.0, 10.0, 10.0, 10.0]);
    let c = a + b;
    capture(c);
}
```

After this spec, that source compiles and runs through `zeroc`'s interpreter, producing a 1-D F32 tensor with bytes `{11, 12, 13, 14}`.

The surface is intentionally minimal: 1-D F32 only, no type annotations, no other tensor operators. The point is to prove the source → AST → IR → runtime path works for tensors; richer syntax follows once the bottom of the stack is exercised through real source.

## 2. Invariants

- A new keyword `tensor` is recognized by the lexer. The keyword is **only** valid as the head of a tensor literal expression — `tensor` as a bare identifier remains an error (the parser will fail with a clear message when it sees `tensor` not followed by `(`).
- Source form: `tensor ( [ numeric_literal (, numeric_literal)* ] )`. Whitespace tolerated. Integer literals (`tensor([1, 2, 3])`) are accepted and silently widened to `float`. At least one element is required; `tensor([])` is a parse error.
- A new AST expression variant `TensorLiteral` exists, carrying `std::vector<double> values` and a `source::Span span`. It is added to `ExprVariant`.
- The parser produces a `TensorLiteral` for the source form above. The span covers from the `tensor` keyword to the closing `)`.
- Sema accepts `TensorLiteral` as a well-formed expression with no further checks for v1. (Empty-value list is already rejected at parse.)
- Lowering: `TensorLiteral` lowers to `IRBuilder::tensor_const_f32(shape, data)` where `shape == {values.size()}` and `data` is the values cast element-wise from `double` to `float`. The resulting IR value carries type `tensor`.
- Lowering `BinaryExpr` with `op == ADD`: if either operand's IR `Value::type` is tensor-typed, the builder emits `OpCode::TENSOR_ADD` (via the existing `tensor_add` builder method from spec 003). Otherwise it emits `OpCode::ADD` as before. Scalar `+` paths are completely unchanged.
- The end-to-end source above runs through `zeroc` (or the test harness equivalent) and produces a tensor `{11, 12, 13, 14}`, captured by an external `capture` registered with the interpreter.
- The other binary operators (`-`, `*`, `/`) on tensors are **not** wired in this spec — using them with a tensor operand produces a `TENSOR_SUB` / `TENSOR_MUL` / `TENSOR_DIV` opcode (which doesn't yet exist as `TENSOR_DIV`, and the others are stubbed in the interpreter), so the result is a `nullptr` `RuntimeValue` and likely a downstream crash. The user-facing error story for these is the next spec's job; this spec keeps the surface to `+`.
- All pre-existing tests continue to pass without modification.

## 3. API surface

Files modified:

1. `include/lexer/token.hpp` — append `TENSOR` to `TokenType` enum (after `USE`) and add it to `Token::type_name`.
2. `src/lexer/lexer.cpp` — add a `'t'` branch to `identifier_type()` matching `"ensor"`.
3. `include/ast/ast.hpp` — add `struct TensorLiteral { std::vector<double> values; source::Span span; };` before the `ExprVariant` alias; add it to the variant; ensure the `Expr::span()` visitor still works (it already uses `auto&` so it will).
4. `include/parser/parser.hpp` / `src/parser/parser.cpp` — in the primary-expression parser, handle `TokenType::TENSOR` by consuming `(`, `[`, comma-separated numeric literals (integer or float), `]`, `)`, and producing a `TensorLiteral`. Parse errors on: empty list, missing brackets, missing `(` after `tensor`, non-numeric element.
5. `src/sema/sema.cpp` — visitor recognizes `TensorLiteral` as well-formed (no-op check, but the visitor must not crash on the new variant).
6. `src/ir/lowering.cpp` — in `lower_expr`, handle `TensorLiteral` by calling `builder.tensor_const_f32(shape, data)`. In the `BinaryExpr` branch, after lowering operands, dispatch on operand `Value::type`: if either is tensor and `op == ADD`, emit `tensor_add`; else fall through to existing behavior.

Files added:

7. `tests/test_source_tensor.cpp` — end-to-end test that compiles a real source string, runs the interpreter with a `capture` external, and asserts the captured tensor's bytes.

No changes to the runtime, the runtime adapter under `runtime/`, the IR struct layout (spec 003 already added the fields we need), or the diagnostics layer.

### Sketch — parser

```cpp
// In parse_primary() — added branch:
if (current_.is(TokenType::TENSOR)) {
    Token tk_tensor = current_;
    advance();
    expect(TokenType::LPAREN, "expected '(' after 'tensor'");
    expect(TokenType::LBRACKET, "expected '[' to start tensor element list");
    std::vector<double> values;
    if (!current_.is(TokenType::RBRACKET)) {
        for (;;) {
            if (current_.is(TokenType::INT_LIT)) {
                values.push_back(static_cast<double>(std::stoll(std::string(current_.text))));
                advance();
            } else if (current_.is(TokenType::FLOAT_LIT)) {
                values.push_back(std::stod(std::string(current_.text)));
                advance();
            } else {
                error("expected numeric literal in tensor element list");
                break;
            }
            if (!current_.is(TokenType::COMMA)) break;
            advance();
        }
    }
    expect(TokenType::RBRACKET, "expected ']' to end tensor element list");
    Token tk_rparen = current_;
    expect(TokenType::RPAREN, "expected ')' to end tensor literal");
    if (values.empty()) error("tensor literal must have at least one element");
    ast::TensorLiteral lit;
    lit.values = std::move(values);
    lit.span = source::Span::range(tk_tensor.span.source_id,
                                   tk_tensor.span.start_offset,
                                   tk_rparen.span.end_offset);
    return ast::make_expr(std::move(lit));
}
```

### Sketch — lowering

```cpp
// In lower_expr, new branch:
else if constexpr (std::is_same_v<T, ast::TensorLiteral>) {
    std::vector<int64_t> shape = { static_cast<int64_t>(e.values.size()) };
    std::vector<float> data;
    data.reserve(e.values.size());
    for (double v : e.values) data.push_back(static_cast<float>(v));
    return builder.tensor_const_f32(std::move(shape), std::move(data));
}

// In BinaryExpr branch, replace the ADD case:
case ast::BinOp::ADD:
    if (lhs.type.is_tensor() || rhs.type.is_tensor()) {
        return builder.tensor_add(lhs, rhs);
    }
    return builder.add(lhs, rhs);
```

## 4. Acceptance tests

New test file: `tests/test_source_tensor.cpp`. End-to-end through `SourceManager → Parser → Lowering → Interpreter`.

1. **Happy path through real source.** Compile and run:
   ```zero
   fn main() {
       let a = tensor([1.0, 2.0, 3.0, 4.0]);
       let b = tensor([10.0, 10.0, 10.0, 10.0]);
       let c = a + b;
       capture(c);
   }
   ```
   The captured `RuntimeValue` is a 1-D F32 tensor of shape `[4]` containing `{11, 12, 13, 14}` bit-exact.

2. **Integer elements widen to float.** `tensor([1, 2, 3, 4])` parses, lowers, and produces a tensor with values `{1.0f, 2.0f, 3.0f, 4.0f}`.

3. **Scalar `+` is unaffected.** `let x = 1 + 2;` lowers to `OpCode::ADD`, not `OpCode::TENSOR_ADD`. (Verified by dumping the IR and grepping for `add` vs `tensor.add`.)

4. **Empty tensor literal is a parse error.** `tensor([])` produces `parser.had_error() == true`. The error message mentions "at least one element."

5. **`tensor` without parens is a parse error.** `let x = tensor;` produces `parser.had_error() == true`.

6. **Span attribution.** The `TENSOR_CONST_F32` instruction emitted for `tensor([...])` carries a valid `source::Span` whose `[start, end)` covers the source substring of the literal. The `TENSOR_ADD` instruction emitted for `a + b` carries the span of the `BinaryExpr`.

7. **No regressions.** All 13 pre-existing test binaries pass with zero source changes.

## 5. Out of scope

- **Type annotations.** No `let a: tensor = ...` or `tensor<f32, 4>` syntax. The type system still treats tensors as a single unparameterised `tensor` type.
- **Multi-dimensional tensor literals.** No `tensor([[1, 2], [3, 4]])`. 1-D only.
- **Other tensor operators.** `-`, `*`, `/` on tensors are not dispatched. The other `TENSOR_*` opcodes stay stubbed; using them via source produces UB-style behavior in the interpreter (this is acceptable since no test source path will hit it). A follow-up spec wires them.
- **Tensor function parameters and return values.** `fn f(t: tensor) -> tensor` is not supported syntactically. v1 only allows tensors as locals.
- **Tensor element access.** No `a[0]` indexing yet.
- **`capture` as a language builtin.** The test registers `capture` as an external function via the interpreter's `register_external` API; the source treats it as a normal call. No language-level magic.
- **Reporter-based diagnostics for parse errors.** Existing parser error machinery is used as-is.
- **Sema type-checking of tensor operations.** If a user writes `let c = a + 1` (tensor + int), v1 silently dispatches to `tensor_add` and the runtime will reject the call at execution time. A future sema spec will catch this at compile time.
- **Tensor literals as compile-time constants for folding / dedup.** Each `tensor([...])` becomes a fresh allocation at run time.

## 6. Open questions

(none — all resolved before approval)

---

## Amendment log

- *Pre-approval* — Resolved autonomously: (a) keyword chosen as `tensor` rather than `[1, 2, 3]: tensor` style annotations — keyword form is unambiguous and parser-cheap; (b) integer literals inside the bracket list silently widen to float, matching how most ML languages handle mixed-int-float literals; (c) only `+` is dispatched in this spec; other operators wait for their own spec; (d) `capture` is a registered external, not a language builtin — keeps the language surface small; (e) sema gets a no-op pass for `TensorLiteral` to avoid crashes; type-checking of tensor ops is explicitly deferred to a future spec.
