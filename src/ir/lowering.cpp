/**
 * @file lowering.cpp
 * @brief Zero Compiler — AST to IR Lowering
 */

#include "ir/lowering.hpp"
#include "ir/builder.hpp"

namespace zero {
namespace ir {

// ─────────────────────────────────────────────────────────────────────────────
// Helper to convert ast::TypeKind to types::Type
// ─────────────────────────────────────────────────────────────────────────────

static types::Type ast_to_type(const ast::Type& t) {
    switch (t.kind) {
        case ast::TypeKind::INT: return types::Type::make_int();
        case ast::TypeKind::FLOAT: return types::Type::make_float();
        case ast::TypeKind::VOID: return types::Type::make_void();
        case ast::TypeKind::TENSOR: return types::Type::make_tensor();
        case ast::TypeKind::STRUCT: return types::Type::make_struct(t.name);
        default: return types::Type::make_unknown();
    }
}

// Collect the names reassigned anywhere in a statement list (recursing into
// nested control-flow bodies). Assignment is a statement, never nested inside
// an expression, so only compound statements need recursion.
static void collect_mutated(const std::vector<std::unique_ptr<ast::Stmt>>& stmts,
                            std::unordered_set<std::string>& out) {
    for (const auto& sp : stmts) {
        if (!sp) continue;
        std::visit([&out](const auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, ast::AssignStmt>) {
                out.insert(s.name);
            } else if constexpr (std::is_same_v<T, ast::IfStmt>) {
                collect_mutated(s.then_branch, out);
                collect_mutated(s.else_branch, out);
            } else if constexpr (std::is_same_v<T, ast::WhileStmt>) {
                collect_mutated(s.body, out);
            } else if constexpr (std::is_same_v<T, ast::Block>) {
                collect_mutated(s.stmts, out);
            }
        }, sp->data);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lowering Implementation
// ─────────────────────────────────────────────────────────────────────────────

Module Lowering::lower(ast::Program& prog) {
    Module mod;

    // Spec 006: forward pass — collect every user-defined function's
    // declared return type so CallExpr lowering can give CALL a typed
    // result Value. Without this, calls like `let b = double_it(a)`
    // get an invalid (void) result Value and `b` cannot be used.
    fn_return_types_.clear();
    for (auto& fn_ast : prog.functions) {
        types::Type ret_type = fn_ast.return_type
            ? ast_to_type(*fn_ast.return_type)
            : types::Type::make_void();
        fn_return_types_[fn_ast.name] = ret_type;
    }

    // Forward pass: record each struct's ordered (field name, type) so field
    // access can resolve a positional index + result type, and a call whose
    // callee is a struct name can be lowered as construction.
    structs_.clear();
    for (auto& sd : prog.structs) {
        std::vector<std::pair<std::string, types::Type>> fields;
        for (auto& f : sd.fields) fields.emplace_back(f.name, ast_to_type(f.type));
        structs_[sd.name] = std::move(fields);
    }

    // Lower each function
    for (auto& fn_ast : prog.functions) {
        lower_function(mod, fn_ast);
    }

    return mod;
}

void Lowering::lower_function(Module& mod, ast::FnDecl& fn_ast) {
    // Get parameter types
    std::vector<types::Type> param_types;
    for (const auto& p : fn_ast.params) {
        param_types.push_back(ast_to_type(p.type));
    }

    // Get return type
    types::Type ret_type = fn_ast.return_type
        ? ast_to_type(*fn_ast.return_type)
        : types::Type::make_void();

    // Create function
    Function& fn = mod.add_function(fn_ast.name, param_types, ret_type);
    IRBuilder builder(fn);

    // Create parameter values, record them on the IR Function (spec 006
    // — the interpreter binds incoming call args to fn.params[i]), and
    // add them to the lowering's symbol table by name.
    symbols_.clear();
    cell_vars_.clear();
    collect_mutated(fn_ast.body, cell_vars_);

    fn.params.clear();
    for (size_t i = 0; i < fn_ast.params.size(); ++i) {
        Value param_val = fn.new_value(param_types[i]);
        fn.params.push_back(param_val);
        const std::string& pname = fn_ast.params[i].name;
        if (cell_vars_.count(pname)) {
            // A reassigned parameter gets a memory cell initialised from the
            // incoming argument, so later writes are visible across blocks.
            builder.set_current_span(fn_ast.params[i].span);
            Value slot = builder.alloca(param_types[i]);
            builder.store(slot, param_val);
            symbols_[pname] = slot;
        } else {
            symbols_[pname] = param_val;
        }
    }

    // Lower body statements
    for (auto& stmt : fn_ast.body) {
        lower_stmt(builder, *stmt);
    }

    // Add implicit void return if needed. Implicit returns have no source
    // location — explicitly reset to invalid so the body's last span doesn't
    // leak onto a synthesized op (spec 002).
    if (fn.blocks.empty() || fn.blocks.back().instrs.empty() ||
        fn.blocks.back().instrs.back().op != OpCode::RET) {
        builder.set_current_span(source::Span::invalid());
        builder.ret();
    }
}

void Lowering::lower_stmt(IRBuilder& builder, ast::Stmt& stmt) {
    // Spec 002: stamp every instruction lowered from this statement with its
    // source span. Child visitors will overwrite as they descend.
    source::Span stmt_span = std::visit(
        [](const auto& s) -> source::Span { return s.span; },
        stmt.data);
    builder.set_current_span(stmt_span);

    std::visit([this, &builder](auto& s) {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, ast::LetStmt>) {
            if (s.init) {
                Value init_val = lower_expr(builder, *s.init);
                if (cell_vars_.count(s.name)) {
                    // Mutated local: store the initial value into a fresh cell.
                    Value slot = builder.alloca(init_val.type);
                    builder.store(slot, init_val);
                    symbols_[s.name] = slot;
                } else {
                    symbols_[s.name] = init_val;
                }
            }
        }
        else if constexpr (std::is_same_v<T, ast::IndexAssignStmt>) {
            // `target[idx] = value` — in-place element write. We evaluate the
            // target expression to a tensor SSA value (load/field-access/etc.
            // all reduce to that), then emit TENSOR_INDEX_SET. The write
            // mutates the buffer through the tensor's shared_ptr; the tensor's
            // identity / SSA binding is unchanged.
            if (s.target && s.index && s.value) {
                Value tgt = lower_expr(builder, *s.target);
                Value idx = lower_expr(builder, *s.index);
                Value val = lower_expr(builder, *s.value);
                builder.tensor_index_set(tgt, idx, val);
            }
        }
        else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            if (s.value) {
                Value v = lower_expr(builder, *s.value);
                auto it = symbols_.find(s.name);
                // The target is always a cell (it appears as an assignment
                // target, so collect_mutated put it in cell_vars_). Write
                // through the cell pointer. Undefined names were already
                // reported by sema; skip them here.
                if (it != symbols_.end()) {
                    builder.store(it->second, v);
                }
            }
        }
        else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            if (s.value) {
                Value ret_val = lower_expr(builder, *s.value);
                builder.ret(ret_val);
            } else {
                builder.ret();
            }
        }
        else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            if (s.expr) {
                lower_expr(builder, *s.expr);
            }
        }
        else if constexpr (std::is_same_v<T, ast::IfStmt>) {
            lower_if(builder, s);
        }
        else if constexpr (std::is_same_v<T, ast::WhileStmt>) {
            lower_while(builder, s);
        }
        else if constexpr (std::is_same_v<T, ast::Block>) {
            // Block-scoped names: snapshot the symbol table and restore it
            // afterward so a `let` inside the block (including one that
            // shadows an outer variable) does not leak its binding outward.
            auto saved = symbols_;
            for (auto& inner : s.stmts) {
                lower_stmt(builder, *inner);
            }
            symbols_ = std::move(saved);
        }
    }, stmt.data);
}

Value Lowering::lower_expr(IRBuilder& builder, ast::Expr& expr) {
    // Spec 002: stamp every instruction lowered from this expression with its
    // source span. Child visitors will overwrite as they descend.
    builder.set_current_span(expr.span());

    return std::visit([this, &builder](auto& e) -> Value {
        using T = std::decay_t<decltype(e)>;
        
        if constexpr (std::is_same_v<T, ast::Identifier>) {
            auto it = symbols_.find(e.name);
            if (it != symbols_.end()) {
                // A mutated variable is held in a memory cell — read its
                // current value with a LOAD. Non-mutated variables bind
                // directly to their SSA value (unchanged lowering).
                if (cell_vars_.count(e.name)) {
                    return builder.load(it->second);
                }
                return it->second;
            }
            // Undefined variable - return invalid value
            return Value{};
        }
        else if constexpr (std::is_same_v<T, ast::IntLiteral>) {
            return builder.const_int(e.value);
        }
        else if constexpr (std::is_same_v<T, ast::FloatLiteral>) {
            return builder.const_float(e.value);
        }
        else if constexpr (std::is_same_v<T, ast::StringLiteral>) {
            return builder.const_str(e.value);
        }
        else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            Value lhs = e.left ? lower_expr(builder, *e.left) : Value{};
            Value rhs = e.right ? lower_expr(builder, *e.right) : Value{};

            // Spec 004 + 005: dispatch arithmetic ops to TENSOR_* when
            // either operand is tensor-typed. Scalar paths fall through
            // to the existing scalar emit unchanged.
            if (lhs.type.is_tensor() || rhs.type.is_tensor()) {
                switch (e.op) {
                    case ast::BinOp::ADD: return builder.tensor_add(lhs, rhs);
                    case ast::BinOp::SUB: return builder.tensor_sub(lhs, rhs);
                    case ast::BinOp::MUL: return builder.tensor_mul(lhs, rhs);
                    case ast::BinOp::DIV: return builder.tensor_div(lhs, rhs);
                    default: break;  // comparisons / other ops: fall through
                }
            }

            switch (e.op) {
                case ast::BinOp::ADD: return builder.add(lhs, rhs);
                case ast::BinOp::SUB: return builder.sub(lhs, rhs);
                case ast::BinOp::MUL: return builder.mul(lhs, rhs);
                case ast::BinOp::DIV: return builder.div(lhs, rhs);
                case ast::BinOp::EQ:  return builder.cmp_eq(lhs, rhs);
                case ast::BinOp::NE:  return builder.cmp_ne(lhs, rhs);
                case ast::BinOp::LT:  return builder.cmp_lt(lhs, rhs);
                case ast::BinOp::LE:  return builder.cmp_le(lhs, rhs);
                case ast::BinOp::GT:  return builder.cmp_gt(lhs, rhs);
                case ast::BinOp::GE:  return builder.cmp_ge(lhs, rhs);
                default: return Value{};
            }
        }
        else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            Value operand = e.operand ? lower_expr(builder, *e.operand) : Value{};
            if (e.op == ast::UnaryOp::NEG) {
                // Spec 005: tensor unary minus dispatches to TENSOR_NEG.
                if (operand.type.is_tensor()) return builder.tensor_neg(operand);
                return builder.neg(operand);
            }
            return operand;
        }
        else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            std::vector<Value> args;
            for (auto& arg : e.args) {
                args.push_back(lower_expr(builder, *arg));
            }
            // Spec 005: dispatch builtin tensor-aware calls. `relu(t)`
            // with a single tensor argument lowers to TENSOR_RELU.
            if (args.size() == 1 && args[0].type.is_tensor()) {
                if (e.callee == "relu")    return builder.tensor_relu(args[0]);
                // Spec 016: shape-preserving unary math.
                if (e.callee == "exp")     return builder.tensor_exp(args[0]);
                if (e.callee == "log")     return builder.tensor_log(args[0]);
                if (e.callee == "sqrt")    return builder.tensor_sqrt(args[0]);
                if (e.callee == "tanh")    return builder.tensor_tanh(args[0]);
                if (e.callee == "sigmoid") return builder.tensor_sigmoid(args[0]);
                if (e.callee == "transpose") return builder.tensor_transpose(args[0]);
                if (e.callee == "step")      return builder.tensor_step(args[0]);
            }
            // Spec 014: reductions on a single tensor argument.
            if (args.size() == 1 && args[0].type.is_tensor()) {
                if (e.callee == "sum")    return builder.tensor_sum(args[0]);
                if (e.callee == "mean")   return builder.tensor_mean(args[0]);
                if (e.callee == "argmax") return builder.tensor_argmax(args[0]);
            }
            // Spec 015: matmul on two tensor arguments.
            if (e.callee == "matmul" && args.size() == 2
                && args[0].type.is_tensor() && args[1].type.is_tensor()) {
                return builder.tensor_matmul(args[0], args[1]);
            }
            // Struct construction: callee is a struct name -> STRUCT_NEW with
            // the args as fields (positional, declaration order).
            if (structs_.count(e.callee)) {
                return builder.struct_new(args, types::Type::make_struct(e.callee));
            }
            // Spec 006: use the declared return type of the user function
            // when known; fall back to void for builtins like print/capture.
            auto ret_it = fn_return_types_.find(e.callee);
            types::Type ret_type = (ret_it != fn_return_types_.end())
                ? ret_it->second
                : types::Type::make_void();
            return builder.call(e.callee, args, ret_type);
        }
        else if constexpr (std::is_same_v<T, ast::IndexExpr>) {
            Value obj = e.object ? lower_expr(builder, *e.object) : Value{};
            Value idx = e.index  ? lower_expr(builder, *e.index)  : Value{};
            return builder.tensor_index_get(obj, idx);
        }
        else if constexpr (std::is_same_v<T, ast::FieldAccess>) {
            Value obj = e.object ? lower_expr(builder, *e.object) : Value{};
            // Resolve the field's positional index and type from the object's
            // struct layout (carried on its Value type).
            auto sit = structs_.find(obj.type.struct_name);
            if (sit != structs_.end()) {
                const auto& fields = sit->second;
                for (size_t i = 0; i < fields.size(); ++i) {
                    if (fields[i].first == e.field) {
                        return builder.struct_get(obj, static_cast<int64_t>(i),
                                                  fields[i].second);
                    }
                }
            }
            return Value{};   // unknown struct/field (sema already reported it)
        }
        else if constexpr (std::is_same_v<T, ast::GroupExpr>) {
            return e.inner ? lower_expr(builder, *e.inner) : Value{};
        }
        else if constexpr (std::is_same_v<T, ast::TensorLiteral>) {
            // Spec 004/015: lower to TENSOR_CONST_F32. The parser has inferred
            // the shape ({N} for 1-D, {rows, cols} for 2-D); values are
            // flattened row-major. Integer elements were widened to double
            // at parse time; cast to float here.
            std::vector<float> data;
            data.reserve(e.values.size());
            for (double v : e.values) data.push_back(static_cast<float>(v));
            return builder.tensor_const_f32(e.shape, std::move(data));
        }
        else {
            return Value{};
        }
    }, expr.data);
}

void Lowering::lower_if(IRBuilder& builder, ast::IfStmt& if_stmt) {
    // Spec 002: synthesized control-flow ops (cond_br, br to merge) belong to
    // the if-statement, not to the last sub-expression that ran. Re-set the
    // span around each emit boundary.
    builder.set_current_span(if_stmt.span);
    Value cond = if_stmt.condition ? lower_expr(builder, *if_stmt.condition) : Value{};

    // Spec 009: hold block ids, not BasicBlock& — create_block() may
    // reallocate fn.blocks, which would dangle any held reference.
    uint32_t then_id  = builder.create_block("if.then");
    uint32_t merge_id = builder.create_block("if.end");

    builder.set_current_span(if_stmt.span);
    if (if_stmt.else_branch.empty()) {
        builder.cond_br(cond, then_id, merge_id);
    } else {
        uint32_t else_id = builder.create_block("if.else");
        builder.cond_br(cond, then_id, else_id);

        builder.set_insert_point(else_id);
        auto saved_else = symbols_;
        for (auto& stmt : if_stmt.else_branch) {
            lower_stmt(builder, *stmt);
        }
        symbols_ = std::move(saved_else);   // else body is its own scope
        builder.set_current_span(if_stmt.span);
        builder.br(merge_id);
    }

    builder.set_insert_point(then_id);
    auto saved_then = symbols_;
    for (auto& stmt : if_stmt.then_branch) {
        lower_stmt(builder, *stmt);
    }
    symbols_ = std::move(saved_then);        // then body is its own scope
    builder.set_current_span(if_stmt.span);
    builder.br(merge_id);

    builder.set_insert_point(merge_id);
}

void Lowering::lower_while(IRBuilder& builder, ast::WhileStmt& while_stmt) {
    // Spec 002: same approach as lower_if — synthesized control-flow ops
    // belong to the while-statement.
    // Spec 009: hold block ids, not BasicBlock&.
    uint32_t cond_id = builder.create_block("while.cond");
    uint32_t body_id = builder.create_block("while.body");
    uint32_t end_id  = builder.create_block("while.end");

    builder.set_current_span(while_stmt.span);
    builder.br(cond_id);

    builder.set_insert_point(cond_id);
    Value cond = while_stmt.condition ? lower_expr(builder, *while_stmt.condition) : Value{};
    builder.set_current_span(while_stmt.span);
    builder.cond_br(cond, body_id, end_id);

    builder.set_insert_point(body_id);
    auto saved_body = symbols_;
    for (auto& stmt : while_stmt.body) {
        lower_stmt(builder, *stmt);
    }
    symbols_ = std::move(saved_body);        // loop body is its own scope
    builder.set_current_span(while_stmt.span);
    builder.br(cond_id);

    builder.set_insert_point(end_id);
}

} // namespace ir
} // namespace zero
