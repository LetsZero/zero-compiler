#ifndef ZERO_SEMA_SEMA_HPP
#define ZERO_SEMA_SEMA_HPP

/**
 * @file sema.hpp
 * @brief Zero Compiler — Semantic Analysis
 * 
 * Performs semantic checks: variable resolution, type checking, etc.
 */

#include "ast/ast.hpp"
#include "types/types.hpp"
#include "source/source.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace zero {
namespace sema {

// ─────────────────────────────────────────────────────────────────────────────
// Semantic Error
// ─────────────────────────────────────────────────────────────────────────────

enum class ErrorKind {
    UNDEFINED_VARIABLE,
    UNDEFINED_FUNCTION,
    WRONG_ARG_COUNT,
    TYPE_MISMATCH,
    RETURN_TYPE_MISMATCH,
    DUPLICATE_DEFINITION
};

struct SemanticError {
    ErrorKind kind;
    std::string message;
    source::Span span;
};

// ─────────────────────────────────────────────────────────────────────────────
// Function Signature
// ─────────────────────────────────────────────────────────────────────────────

struct FnSignature {
    std::string name;
    std::vector<types::Type> param_types;
    types::Type return_type;
};

// ─────────────────────────────────────────────────────────────────────────────
// Semantic Analyzer
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Semantic analyzer for Zero programs.
 * 
 * Usage:
 *   Sema sema;
 *   sema.analyze(program);
 *   if (sema.had_error()) { ... }
 */
class Sema {
public:
    Sema() = default;
    
    /**
     * Analyze entire program.
     */
    void analyze(ast::Program& prog);
    
    /**
     * Check if any errors occurred.
     */
    bool had_error() const { return !errors_.empty(); }
    
    /**
     * Get list of semantic errors.
     */
    const std::vector<SemanticError>& errors() const { return errors_; }
    
    /**
     * Clear errors for reuse.
     */
    void reset() {
        errors_.clear();
        scopes_.clear();
        functions_.clear();
    }

private:
    // Scope stack (innermost at back)
    std::vector<std::unordered_map<std::string, types::Type>> scopes_;
    
    // Function signatures
    std::unordered_map<std::string, FnSignature> functions_;
    
    // Current function return type (for checking return statements)
    types::Type current_return_type_;
    
    // Collected errors
    std::vector<SemanticError> errors_;
    
    // ─────────────────────────────────────────────────────────────────────
    // Scope management
    // ─────────────────────────────────────────────────────────────────────
    
    void push_scope();
    void pop_scope();
    void declare(const std::string& name, types::Type type, source::Span span);
    std::optional<types::Type> lookup(const std::string& name);
    
    // ─────────────────────────────────────────────────────────────────────
    // Analysis
    // ─────────────────────────────────────────────────────────────────────
    
    void collect_functions(ast::Program& prog);
    void check_fn(ast::FnDecl& fn);
    void check_stmt(ast::Stmt& stmt);
    types::Type check_expr(ast::Expr& expr);
    
    // ─────────────────────────────────────────────────────────────────────
    // Error reporting
    // ─────────────────────────────────────────────────────────────────────
    
    void error(ErrorKind kind, const std::string& msg, source::Span span);
};

} // namespace sema
} // namespace zero

#endif // ZERO_SEMA_SEMA_HPP
