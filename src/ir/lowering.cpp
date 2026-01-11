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

static types::Type ast_to_type(ast::TypeKind kind) {
    switch (kind) {
        case ast::TypeKind::INT: return types::Type::make_int();
        case ast::TypeKind::FLOAT: return types::Type::make_float();
        case ast::TypeKind::VOID: return types::Type::make_void();
        case ast::TypeKind::TENSOR: return types::Type::make_tensor();
        default: return types::Type::make_unknown();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lowering Implementation
// ─────────────────────────────────────────────────────────────────────────────

Module Lowering::lower(ast::Program& prog) {
    Module mod;
    
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
        param_types.push_back(ast_to_type(p.type.kind));
    }
    
    // Get return type
    types::Type ret_type = fn_ast.return_type 
        ? ast_to_type(fn_ast.return_type->kind)
        : types::Type::make_void();
    
    // Create function
    Function& fn = mod.add_function(fn_ast.name, param_types, ret_type);
    IRBuilder builder(fn);
    
    // Create parameter values and add to symbol table
    symbols_.clear();
    for (size_t i = 0; i < fn_ast.params.size(); ++i) {
        Value param_val = fn.new_value(param_types[i]);
        symbols_[fn_ast.params[i].name] = param_val;
    }
    
    // Lower body statements
    for (auto& stmt : fn_ast.body) {
        lower_stmt(builder, *stmt);
    }
    
    // Add implicit void return if needed
    if (fn.blocks.empty() || fn.blocks.back().instrs.empty() ||
        fn.blocks.back().instrs.back().op != OpCode::RET) {
        builder.ret();
    }
}

void Lowering::lower_stmt(IRBuilder& builder, ast::Stmt& stmt) {
    std::visit([this, &builder](auto& s) {
        using T = std::decay_t<decltype(s)>;
        
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
            if (s.init) {
                Value init_val = lower_expr(builder, *s.init);
                symbols_[s.name] = init_val;
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
            for (auto& inner : s.stmts) {
                lower_stmt(builder, *inner);
            }
        }
    }, stmt.data);
}

Value Lowering::lower_expr(IRBuilder& builder, ast::Expr& expr) {
    return std::visit([this, &builder](auto& e) -> Value {
        using T = std::decay_t<decltype(e)>;
        
        if constexpr (std::is_same_v<T, ast::Identifier>) {
            auto it = symbols_.find(e.name);
            if (it != symbols_.end()) {
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
        else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            Value lhs = e.left ? lower_expr(builder, *e.left) : Value{};
            Value rhs = e.right ? lower_expr(builder, *e.right) : Value{};
            
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
                return builder.neg(operand);
            }
            return operand;
        }
        else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            std::vector<Value> args;
            for (auto& arg : e.args) {
                args.push_back(lower_expr(builder, *arg));
            }
            // For now, assume void return type
            return builder.call(e.callee, args, types::Type::make_void());
        }
        else if constexpr (std::is_same_v<T, ast::GroupExpr>) {
            return e.inner ? lower_expr(builder, *e.inner) : Value{};
        }
        else {
            return Value{};
        }
    }, expr.data);
}

void Lowering::lower_if(IRBuilder& builder, ast::IfStmt& if_stmt) {
    Value cond = if_stmt.condition ? lower_expr(builder, *if_stmt.condition) : Value{};
    
    BasicBlock& then_bb = builder.create_block("if.then");
    BasicBlock& merge_bb = builder.create_block("if.end");
    
    if (if_stmt.else_branch.empty()) {
        builder.cond_br(cond, then_bb, merge_bb);
    } else {
        BasicBlock& else_bb = builder.create_block("if.else");
        builder.cond_br(cond, then_bb, else_bb);
        
        builder.set_insert_point(else_bb);
        for (auto& stmt : if_stmt.else_branch) {
            lower_stmt(builder, *stmt);
        }
        builder.br(merge_bb);
    }
    
    builder.set_insert_point(then_bb);
    for (auto& stmt : if_stmt.then_branch) {
        lower_stmt(builder, *stmt);
    }
    builder.br(merge_bb);
    
    builder.set_insert_point(merge_bb);
}

void Lowering::lower_while(IRBuilder& builder, ast::WhileStmt& while_stmt) {
    BasicBlock& cond_bb = builder.create_block("while.cond");
    BasicBlock& body_bb = builder.create_block("while.body");
    BasicBlock& end_bb = builder.create_block("while.end");
    
    builder.br(cond_bb);
    
    builder.set_insert_point(cond_bb);
    Value cond = while_stmt.condition ? lower_expr(builder, *while_stmt.condition) : Value{};
    builder.cond_br(cond, body_bb, end_bb);
    
    builder.set_insert_point(body_bb);
    for (auto& stmt : while_stmt.body) {
        lower_stmt(builder, *stmt);
    }
    builder.br(cond_bb);
    
    builder.set_insert_point(end_bb);
}

} // namespace ir
} // namespace zero
