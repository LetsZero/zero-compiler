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
// Definite-return analysis (conservative; see header)
// ─────────────────────────────────────────────────────────────────────────────

bool Sema::stmt_definitely_returns(const ast::Stmt& stmt) {
    return std::visit([](const auto& s) -> bool {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            return true;
        } else if constexpr (std::is_same_v<T, ast::IfStmt>) {
            // Both arms must exist and both must definitely return.
            return !s.else_branch.empty()
                && block_definitely_returns(s.then_branch)
                && block_definitely_returns(s.else_branch);
        } else if constexpr (std::is_same_v<T, ast::Block>) {
            return block_definitely_returns(s.stmts);
        } else {
            // While/Let/Expr never guarantee a return.
            return false;
        }
    }, stmt.data);
}

bool Sema::block_definitely_returns(const std::vector<std::unique_ptr<ast::Stmt>>& stmts) {
    for (const auto& s : stmts) {
        if (s && stmt_definitely_returns(*s)) return true;
    }
    return false;
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
    // Register built-in functions
    register_builtins();
    
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

void Sema::register_builtins() {
    // Register built-in functions that are available in Zero
    
    // print(...) - variadic print function
    FnSignature print_sig;
    print_sig.name = "print";
    // Empty param_types = accepts any number of arguments
    print_sig.return_type = types::Type::make_void();
    print_sig.is_variadic = true;
    functions_["print"] = print_sig;
    
    // log(msg, color=...) - logging function with optional color
    FnSignature log_sig;
    log_sig.name = "log";
    log_sig.return_type = types::Type::make_void();
    log_sig.is_variadic = true;
    functions_["log"] = log_sig;

    // capture(value) - test helper that lets a test observe a single
    // RuntimeValue post-execution. Spec 004 uses this for end-to-end
    // tensor verification. Variadic to accept any single arg without
    // type-checking it for v1.
    FnSignature capture_sig;
    capture_sig.name = "capture";
    capture_sig.return_type = types::Type::make_void();
    capture_sig.is_variadic = true;
    functions_["capture"] = capture_sig;

    // relu(x) - dispatched to TENSOR_RELU at lowering when arg is tensor
    // (spec 005). Variadic so we don't yet enforce arity; lowering and
    // the runtime do their own checks.
    FnSignature relu_sig;
    relu_sig.name = "relu";
    relu_sig.return_type = types::Type::make_tensor();
    relu_sig.is_variadic = true;
    functions_["relu"] = relu_sig;

    // sum/mean/argmax(t) - reductions (spec 014). matmul(a, b) (spec 015).
    // All dispatched to TENSOR_* at lowering when args are tensors; return
    // a tensor.
    for (const char* name : {"sum", "mean", "argmax", "matmul",
                             "exp", "log", "sqrt", "tanh", "sigmoid", "transpose"}) {
        FnSignature sig;
        sig.name = name;
        sig.return_type = types::Type::make_tensor();
        sig.is_variadic = true;
        functions_[name] = sig;
    }
}

void Sema::check_fn(ast::FnDecl& fn) {
    push_scope();
    // Spec 018: shape tracking is per-function. Parameters are intentionally
    // left unrecorded (unknown shape).
    var_shapes_.clear();

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

    // A non-void function must return a value on every path. Without this,
    // falling off the end leaves the interpreter returning an undefined
    // leftover value (the last evaluated instruction). Only enforced when the
    // return type is explicitly declared and non-void; an absent annotation
    // stays lenient (UNKNOWN), matching the MPP's treatment of `fn main()`.
    if (fn.return_type) {
        types::Type rt = ast_to_types(fn.return_type->kind);
        if (!rt.is_void() && !rt.is_unknown()
            && !block_definitely_returns(fn.body)) {
            error(ErrorKind::MISSING_RETURN,
                  "function '" + fn.name + "' has return type " + rt.to_string() +
                  " but may reach the end without returning a value", fn.span);
        }
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
                // Spec 018: record the init's static shape (if known) so
                // later uses of this variable can be shape-checked. This
                // call also reports any shape mismatch inside the init.
                if (auto sh = infer_shape(*s.init)) {
                    var_shapes_[s.name] = *sh;
                }
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
        else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            // Assignment requires a prior declaration (unlike `let`).
            auto existing = lookup(s.name);
            types::Type val_type = types::Type::make_unknown();
            std::optional<Shape> sh;
            if (s.value) {
                val_type = check_expr(*s.value);
                sh = infer_shape(*s.value);   // also emits any shape mismatch
            }
            if (!existing) {
                error(ErrorKind::UNDEFINED_VARIABLE,
                      "Assignment to undefined variable: " + s.name, s.span);
            } else if (!val_type.is_unknown()
                       && !types::types_compatible(*existing, val_type)) {
                error(ErrorKind::TYPE_MISMATCH,
                      "Type mismatch in assignment to '" + s.name + "': expected " +
                      existing->to_string() + ", got " + val_type.to_string(), s.span);
            }
            // Refresh tracked literal-derived shape (or drop it if now unknown,
            // so a stale shape can't trigger a false mismatch later).
            if (sh) var_shapes_[s.name] = *sh;
            else    var_shapes_.erase(s.name);
        }
        else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            types::Type ret_type = types::Type::make_void();
            if (s.value) {
                ret_type = check_expr(*s.value);
                infer_shape(*s.value);   // spec 018: run shape checks
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
                infer_shape(*s.expr);   // spec 018: run shape checks
            }
        }
        else if constexpr (std::is_same_v<T, ast::IfStmt>) {
            if (s.condition) {
                types::Type ct = check_expr(*s.condition);
                if (ct.is_tensor()) {
                    error(ErrorKind::TYPE_MISMATCH,
                          "condition must be a scalar, got tensor", s.condition->span());
                }
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
                types::Type ct = check_expr(*s.condition);
                if (ct.is_tensor()) {
                    error(ErrorKind::TYPE_MISMATCH,
                          "condition must be a scalar, got tensor", s.condition->span());
                }
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
        else if constexpr (std::is_same_v<T, ast::StringLiteral>) {
            // String literals - MPP uses unknown type for now
            return types::Type::make_unknown();
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
            
            // Check argument count (skip for variadic functions)
            if (!sig.is_variadic && e.args.size() != sig.param_types.size()) {
                error(ErrorKind::WRONG_ARG_COUNT,
                      "Function '" + e.callee + "' expects " + 
                      std::to_string(sig.param_types.size()) + " arguments, got " +
                      std::to_string(e.args.size()), e.span);
            }
            
            // Check arguments. Recurse into EVERY argument so sub-expression
            // errors (undefined variables, nested bad arity, type mismatches)
            // surface even for variadic functions — whose param_types is empty.
            // The earlier `i < sig.param_types.size()` bound skipped the loop
            // body entirely for variadics, silently leaving their arguments
            // unchecked (e.g. relu/sum/matmul/print/capture). Only perform the
            // declared-param-type comparison for positions that have one.
            for (size_t i = 0; i < e.args.size(); ++i) {
                types::Type arg_type = check_expr(*e.args[i]);
                if (i < sig.param_types.size()
                    && !types::types_compatible(sig.param_types[i], arg_type)) {
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
        else if constexpr (std::is_same_v<T, ast::TensorLiteral>) {
            // Spec 004: tensor literals are well-formed by parse-time;
            // the parser rejects the empty list. No further sema checks
            // for v1; richer shape/dtype checking comes later.
            return types::Type::make_tensor();
        }
        else {
            return types::Type::make_unknown();
        }
    }, expr.data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static shape inference (spec 018)
// ─────────────────────────────────────────────────────────────────────────────

namespace {
int64_t shape_numel(const std::vector<int64_t>& s) {
    int64_t n = 1;
    for (int64_t d : s) n *= d;
    return n;
}
std::string shape_str(const std::vector<int64_t>& s) {
    std::string out = "[";
    for (size_t i = 0; i < s.size(); ++i) {
        if (i) out += ", ";
        out += std::to_string(s[i]);
    }
    return out + "]";
}
} // namespace

std::optional<Sema::Shape> Sema::infer_shape(ast::Expr& expr) {
    return std::visit([this](auto& e) -> std::optional<Shape> {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, ast::TensorLiteral>) {
            return e.shape;
        }
        else if constexpr (std::is_same_v<T, ast::Identifier>) {
            auto it = var_shapes_.find(e.name);
            if (it != var_shapes_.end()) return it->second;
            return std::nullopt;
        }
        else if constexpr (std::is_same_v<T, ast::GroupExpr>) {
            return e.inner ? infer_shape(*e.inner) : std::nullopt;
        }
        else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            // Unary '-' is shape-preserving on a tensor.
            return e.operand ? infer_shape(*e.operand) : std::nullopt;
        }
        else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            auto ls = e.left  ? infer_shape(*e.left)  : std::nullopt;
            auto rs = e.right ? infer_shape(*e.right) : std::nullopt;

            // Only the arithmetic ops produce a tensor; comparisons yield a
            // scalar bool (unknown/uninteresting for shape).
            switch (e.op) {
                case ast::BinOp::ADD:
                case ast::BinOp::SUB:
                case ast::BinOp::MUL:
                case ast::BinOp::DIV:
                    break;
                default:
                    return std::nullopt;
            }

            if (ls && rs) {
                if (*ls == *rs) return ls;
                // scalar / [1] broadcast either direction.
                if (shape_numel(*ls) == 1) return rs;
                if (shape_numel(*rs) == 1) return ls;
                error(ErrorKind::SHAPE_MISMATCH,
                      "shape mismatch in '" + std::string(ast::binop_str(e.op)) +
                      "': " + shape_str(*ls) + " vs " + shape_str(*rs),
                      e.span);
                return std::nullopt;
            }
            // One side known (e.g. tensor * scalar) -> propagate it.
            if (ls) return ls;
            if (rs) return rs;
            return std::nullopt;
        }
        else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            if ((e.callee == "sum" || e.callee == "mean" || e.callee == "argmax")
                && e.args.size() == 1) {
                return Shape{1};   // full reduction -> [1]
            }
            if ((e.callee == "exp" || e.callee == "log" || e.callee == "sqrt"
                 || e.callee == "tanh" || e.callee == "sigmoid" || e.callee == "relu")
                && e.args.size() == 1) {
                return infer_shape(*e.args[0]);   // shape-preserving
            }
            if (e.callee == "transpose" && e.args.size() == 1) {
                auto a = infer_shape(*e.args[0]);
                if (a && a->size() == 2) return Shape{ (*a)[1], (*a)[0] };
                return a;   // rank<2: identity; unknown: unknown
            }
            if (e.callee == "matmul" && e.args.size() == 2) {
                auto a = infer_shape(*e.args[0]);
                auto b = infer_shape(*e.args[1]);
                if (a && b && a->size() == 2 && b->size() == 2) {
                    if ((*a)[1] != (*b)[0]) {
                        error(ErrorKind::SHAPE_MISMATCH,
                              "matmul inner-dimension mismatch: " +
                              shape_str(*a) + " x " + shape_str(*b),
                              e.span);
                        return std::nullopt;
                    }
                    return Shape{ (*a)[0], (*b)[1] };
                }
                return std::nullopt;
            }
            return std::nullopt;   // user function: result shape unknown
        }
        else {
            return std::nullopt;   // numeric/string literals: scalar
        }
    }, expr.data);
}

} // namespace sema
} // namespace zero
