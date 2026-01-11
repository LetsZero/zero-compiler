#ifndef ZERO_AST_AST_HPP
#define ZERO_AST_AST_HPP

/**
 * @file ast.hpp
 * @brief Zero Compiler — Abstract Syntax Tree
 * 
 * Defines all AST node types using std::variant for type-safe unions.
 */

#include "source/source.hpp"
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>

namespace zero {
namespace ast {

// Forward declarations
struct Expr;
struct Stmt;

// ─────────────────────────────────────────────────────────────────────────────
// Binary operators
// ─────────────────────────────────────────────────────────────────────────────

enum class BinOp {
    ADD, SUB, MUL, DIV,      // + - * /
    EQ, NE,                   // == !=
    LT, GT, LE, GE           // < > <= >=
};

inline const char* binop_str(BinOp op) {
    switch (op) {
        case BinOp::ADD: return "+";
        case BinOp::SUB: return "-";
        case BinOp::MUL: return "*";
        case BinOp::DIV: return "/";
        case BinOp::EQ:  return "==";
        case BinOp::NE:  return "!=";
        case BinOp::LT:  return "<";
        case BinOp::GT:  return ">";
        case BinOp::LE:  return "<=";
        case BinOp::GE:  return ">=";
        default:         return "?";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Unary operators
// ─────────────────────────────────────────────────────────────────────────────

enum class UnaryOp {
    NEG,   // -
    NOT    // !
};

// ─────────────────────────────────────────────────────────────────────────────
// Types (minimal)
// ─────────────────────────────────────────────────────────────────────────────

enum class TypeKind {
    INT,
    FLOAT,
    VOID,
    TENSOR,
    UNKNOWN
};

struct Type {
    TypeKind kind = TypeKind::UNKNOWN;
    source::Span span;
    
    static Type make_int(source::Span s = {}) { return Type{TypeKind::INT, s}; }
    static Type make_float(source::Span s = {}) { return Type{TypeKind::FLOAT, s}; }
    static Type make_void(source::Span s = {}) { return Type{TypeKind::VOID, s}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Expression nodes
// ─────────────────────────────────────────────────────────────────────────────

struct Identifier {
    std::string name;
    source::Span span;
};

struct IntLiteral {
    int64_t value;
    source::Span span;
};

struct FloatLiteral {
    double value;
    source::Span span;
};

struct BinaryExpr {
    BinOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    source::Span span;
};

struct UnaryExpr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
    source::Span span;
};

struct CallExpr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    source::Span span;
};

struct GroupExpr {
    std::unique_ptr<Expr> inner;
    source::Span span;
};

// ─────────────────────────────────────────────────────────────────────────────
// Expr variant
// ─────────────────────────────────────────────────────────────────────────────

using ExprVariant = std::variant<
    Identifier,
    IntLiteral,
    FloatLiteral,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    GroupExpr
>;

struct Expr {
    ExprVariant data;
    
    template<typename T>
    bool is() const { return std::holds_alternative<T>(data); }
    
    template<typename T>
    T& as() { return std::get<T>(data); }
    
    template<typename T>
    const T& as() const { return std::get<T>(data); }
    
    source::Span span() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Statement nodes
// ─────────────────────────────────────────────────────────────────────────────

struct LetStmt {
    std::string name;
    std::optional<Type> type_annot;
    std::unique_ptr<Expr> init;
    source::Span span;
};

struct ReturnStmt {
    std::unique_ptr<Expr> value;  // nullptr for bare return
    source::Span span;
};

struct ExprStmt {
    std::unique_ptr<Expr> expr;
    source::Span span;
};

struct IfStmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> then_branch;
    std::vector<std::unique_ptr<Stmt>> else_branch;
    source::Span span;
};

struct WhileStmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> body;
    source::Span span;
};

struct Block {
    std::vector<std::unique_ptr<Stmt>> stmts;
    source::Span span;
};

// ─────────────────────────────────────────────────────────────────────────────
// Stmt variant
// ─────────────────────────────────────────────────────────────────────────────

using StmtVariant = std::variant<
    LetStmt,
    ReturnStmt,
    ExprStmt,
    IfStmt,
    WhileStmt,
    Block
>;

struct Stmt {
    StmtVariant data;
    
    template<typename T>
    bool is() const { return std::holds_alternative<T>(data); }
    
    template<typename T>
    T& as() { return std::get<T>(data); }
    
    template<typename T>
    const T& as() const { return std::get<T>(data); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Function & Program
// ─────────────────────────────────────────────────────────────────────────────

struct Param {
    std::string name;
    Type type;
    source::Span span;
};

struct FnDecl {
    std::string name;
    std::vector<Param> params;
    std::optional<Type> return_type;
    std::vector<std::unique_ptr<Stmt>> body;
    source::Span span;
};

struct Program {
    std::vector<FnDecl> functions;
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility implementations
// ─────────────────────────────────────────────────────────────────────────────

inline source::Span Expr::span() const {
    return std::visit([](const auto& e) -> source::Span {
        return e.span;
    }, data);
}

// ─────────────────────────────────────────────────────────────────────────────
// AST Helpers
// ─────────────────────────────────────────────────────────────────────────────

inline std::unique_ptr<Expr> make_expr(ExprVariant&& v) {
    auto e = std::make_unique<Expr>();
    e->data = std::move(v);
    return e;
}

inline std::unique_ptr<Stmt> make_stmt(StmtVariant&& v) {
    auto s = std::make_unique<Stmt>();
    s->data = std::move(v);
    return s;
}

} // namespace ast
} // namespace zero

#endif // ZERO_AST_AST_HPP
