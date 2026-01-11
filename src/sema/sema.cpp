/**
 * @file sema.cpp
 * @brief Zero Compiler — Semantic Analysis Implementation
 */

#include "sema/sema.hpp"

namespace zero {
namespace sema {

// Note: We use explicit namespace qualifiers to avoid
// conflicts between ast::Type and types::Type

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Convert ast::TypeKind to types::Type
// ─────────────────────────────────────────────────────────────────────────────

static types::Type ast_to_types(ast::TypeKind kind) {
    switch (kind) {
        case ast::TypeKind::INT: return types::Type::make_int();
        case ast::TypeKind::FLOAT: return types::Type::make_float();
        case ast::TypeKind::VOID: return types::Type::make_void();
        case ast::TypeKind::TENSOR: return types::Type::make_tensor();
        default: return types::Type::make_unknown();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Scope management
// ─────────────────────────────────────────────────────────────────────────────

void Sema::push_scope() {
    scopes_.emplace_back();
}

void Sema::pop_scope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

void Sema::declare(const std::string& name, types::Type type, source::Span span) {
    if (scopes_.empty()) {
        push_scope();
    }
    
    auto& current = scopes_.back();
    if (current.find(name) != current.end()) {
        error(ErrorKind::DUPLICATE_DEFINITION, 
              "Variable '" + name + "' already declared in this scope", span);
        return;
    }
    current[name] = type;
}

std::optional<types::Type> Sema::lookup(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Error reporting
// ─────────────────────────────────────────────────────────────────────────────

void Sema::error(ErrorKind kind, const std::string& msg, source::Span span) {
    errors_.push_back(SemanticError{kind, msg, span});
}

// ─────────────────────────────────────────────────────────────────────────────
// Main analysis
// ─────────────────────────────────────────────────────────────────────────────

void Sema::analyze(ast::Program& prog) {
    // First pass: collect all function signatures
    collect_functions(prog);
    
    // Second pass: check each function body
    for (auto& fn : prog.functions) {
        check_fn(fn);
    }
}

void Sema::collect_functions(ast::Program& prog) {
    for (auto& fn : prog.functions) {
        FnSignature sig;
        sig.name = fn.name;
        
        for (const auto& param : fn.params) {
            sig.param_types.push_back(ast_to_types(param.type.kind));
        }
        
        if (fn.return_type) {
            sig.return_type = ast_to_types(fn.return_type->kind);
        } else {
            sig.return_type = types::Type::make_void();
        }
        
        if (functions_.find(fn.name) != functions_.end()) {
            error(ErrorKind::DUPLICATE_DEFINITION,
                  "Function '" + fn.name + "' already defined", fn.span);
        } else {
            functions_[fn.name] = sig;
        }
    }
}

void Sema::check_fn(ast::FnDecl& fn) {
    push_scope();
    
    // Set current return type for return statement checking
    // If no annotation, use UNKNOWN to skip strict checking (MPP lenient)
    if (fn.return_type) {
        current_return_type_ = ast_to_types(fn.return_type->kind);
    } else {
        current_return_type_ = types::Type::make_unknown();
    }
    
    // Declare parameters
    for (const auto& param : fn.params) {
        declare(param.name, ast_to_types(param.type.kind), param.span);
    }
    
    // Check body statements
    for (auto& stmt : fn.body) {
        check_stmt(*stmt);
    }
    
    pop_scope();
}

// ─────────────────────────────────────────────────────────────────────────────
// Statement checking
// ─────────────────────────────────────────────────────────────────────────────

void Sema::check_stmt(ast::Stmt& stmt) {
    std::visit([this](auto& s) {
        using T = std::decay_t<decltype(s)>;
        
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
            types::Type init_type = types::Type::make_unknown();
            if (s.init) {
                init_type = check_expr(*s.init);
            }
            
            types::Type var_type = init_type;
            if (s.type_annot) {
                var_type = ast_to_types(s.type_annot->kind);
                // Check type compatibility
                if (!init_type.is_unknown() && !types::types_compatible(var_type, init_type)) {
                    error(ErrorKind::TYPE_MISMATCH,
                          "Type mismatch: expected " + var_type.to_string() + 
                          ", got " + init_type.to_string(), s.span);
                }
            }
            
            declare(s.name, var_type, s.span);
        }
        else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            types::Type ret_type = types::Type::make_void();
            if (s.value) {
                ret_type = check_expr(*s.value);
            }
            
            if (!types::types_compatible(current_return_type_, ret_type)) {
                error(ErrorKind::RETURN_TYPE_MISMATCH,
                      "Return type mismatch: expected " + current_return_type_.to_string() +
                      ", got " + ret_type.to_string(), s.span);
            }
        }
        else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            if (s.expr) {
                check_expr(*s.expr);
            }
        }
        else if constexpr (std::is_same_v<T, ast::IfStmt>) {
            if (s.condition) {
                check_expr(*s.condition);
            }
            push_scope();
            for (auto& then_stmt : s.then_branch) {
                check_stmt(*then_stmt);
            }
            pop_scope();
            
            if (!s.else_branch.empty()) {
                push_scope();
                for (auto& else_stmt : s.else_branch) {
                    check_stmt(*else_stmt);
                }
                pop_scope();
            }
        }
        else if constexpr (std::is_same_v<T, ast::WhileStmt>) {
            if (s.condition) {
                check_expr(*s.condition);
            }
            push_scope();
            for (auto& body_stmt : s.body) {
                check_stmt(*body_stmt);
            }
            pop_scope();
        }
        else if constexpr (std::is_same_v<T, ast::Block>) {
            push_scope();
            for (auto& block_stmt : s.stmts) {
                check_stmt(*block_stmt);
            }
            pop_scope();
        }
    }, stmt.data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Expression checking
// ─────────────────────────────────────────────────────────────────────────────

types::Type Sema::check_expr(ast::Expr& expr) {
    return std::visit([this](auto& e) -> types::Type {
        using T = std::decay_t<decltype(e)>;
        
        if constexpr (std::is_same_v<T, ast::Identifier>) {
            auto type = lookup(e.name);
            if (!type) {
                error(ErrorKind::UNDEFINED_VARIABLE,
                      "Undefined variable: " + e.name, e.span);
                return types::Type::make_unknown();
            }
            return *type;
        }
        else if constexpr (std::is_same_v<T, ast::IntLiteral>) {
            return types::Type::make_int();
        }
        else if constexpr (std::is_same_v<T, ast::FloatLiteral>) {
            return types::Type::make_float();
        }
        else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            types::Type left = e.left ? check_expr(*e.left) : types::Type::make_unknown();
            types::Type right = e.right ? check_expr(*e.right) : types::Type::make_unknown();
            return types::binary_result_type(left, right);
        }
        else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            return e.operand ? check_expr(*e.operand) : types::Type::make_unknown();
        }
        else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            auto it = functions_.find(e.callee);
            if (it == functions_.end()) {
                error(ErrorKind::UNDEFINED_FUNCTION,
                      "Undefined function: " + e.callee, e.span);
                return types::Type::make_unknown();
            }
            
            const FnSignature& sig = it->second;
            
            // Check argument count
            if (e.args.size() != sig.param_types.size()) {
                error(ErrorKind::WRONG_ARG_COUNT,
                      "Function '" + e.callee + "' expects " + 
                      std::to_string(sig.param_types.size()) + " arguments, got " +
                      std::to_string(e.args.size()), e.span);
            }
            
            // Check argument types
            for (size_t i = 0; i < e.args.size() && i < sig.param_types.size(); ++i) {
                types::Type arg_type = check_expr(*e.args[i]);
                if (!types::types_compatible(sig.param_types[i], arg_type)) {
                    error(ErrorKind::TYPE_MISMATCH,
                          "Argument " + std::to_string(i + 1) + " type mismatch", 
                          e.args[i]->span());
                }
            }
            
            return sig.return_type;
        }
        else if constexpr (std::is_same_v<T, ast::GroupExpr>) {
            return e.inner ? check_expr(*e.inner) : types::Type::make_unknown();
        }
        else {
            return types::Type::make_unknown();
        }
    }, expr.data);
}

} // namespace sema
} // namespace zero
