/**
 * @file parser.cpp
 * @brief Zero Compiler — Parser Implementation
 */

#include "parser/parser.hpp"
#include <charconv>

namespace zero {
namespace parser {

using namespace lexer;
using namespace ast;
using namespace source;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

Parser::Parser(SourceManager& sm, SourceID id)
    : lexer_(sm, id), sm_(sm), source_id_(id) {
    advance();
}

// ─────────────────────────────────────────────────────────────────────────────
// Token handling
// ─────────────────────────────────────────────────────────────────────────────

void Parser::advance() {
    previous_ = current_;
    
    while (true) {
        current_ = lexer_.next();
        if (!current_.is_error()) break;
        error_at(current_, current_.text.data());
    }
}

bool Parser::check(TokenType type) const {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

void Parser::consume(TokenType type, const char* message) {
    if (check(type)) {
        advance();
        return;
    }
    error(message);
}

void Parser::skip_newlines() {
    while (match(TokenType::NEWLINE)) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// Error handling
// ─────────────────────────────────────────────────────────────────────────────

void Parser::error(const char* message) {
    error_at(current_, message);
}

void Parser::error_at(const Token& token, const char* message) {
    if (panic_mode_) return;
    panic_mode_ = true;
    had_error_ = true;
    
    errors_.push_back(ParseError{message, token.span});
}

void Parser::synchronize() {
    panic_mode_ = false;
    
    while (!current_.is_eof()) {
        if (previous_.type == TokenType::SEMICOLON) return;
        if (previous_.type == TokenType::NEWLINE) return;
        
        switch (current_.type) {
            case TokenType::FN:
            case TokenType::LET:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::RETURN:
                return;
            default:
                break;
        }
        advance();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main parse entry
// ─────────────────────────────────────────────────────────────────────────────

Program Parser::parse() {
    Program program;
    
    skip_newlines();
    
    while (!current_.is_eof()) {
        // Skip use statements (not yet implemented)
        if (check(TokenType::USE)) {
            advance();  // consume 'use'
            if (check(TokenType::IDENT)) {
                advance();  // consume module name
            }
            skip_newlines();
            continue;
        }
        
        if (check(TokenType::FN)) {
            program.functions.push_back(parse_fn_decl());
        } else {
            error("Expected function declaration");
            synchronize();
        }
        skip_newlines();
    }
    
    return program;
}

// ─────────────────────────────────────────────────────────────────────────────
// Function declaration
// ─────────────────────────────────────────────────────────────────────────────

FnDecl Parser::parse_fn_decl() {
    FnDecl fn;
    Span start = current_.span;
    
    consume(TokenType::FN, "Expected 'fn'");
    
    if (!check(TokenType::IDENT)) {
        error("Expected function name");
        return fn;
    }
    fn.name = std::string(current_.text);
    advance();
    
    consume(TokenType::LPAREN, "Expected '(' after function name");
    fn.params = parse_params();
    consume(TokenType::RPAREN, "Expected ')' after parameters");
    
    // Optional return type: -> Type
    if (match(TokenType::ARROW)) {
        fn.return_type = parse_type();
    }
    
    skip_newlines();
    consume(TokenType::LBRACE, "Expected '{' before function body");
    skip_newlines();
    
    // Parse body statements
    while (!check(TokenType::RBRACE) && !current_.is_eof()) {
        auto stmt = parse_stmt();
        if (stmt) {
            fn.body.push_back(std::move(stmt));
        }
        skip_newlines();
    }
    
    consume(TokenType::RBRACE, "Expected '}' after function body");
    fn.span = start.merge(previous_.span);
    
    return fn;
}

std::vector<Param> Parser::parse_params() {
    std::vector<Param> params;
    
    if (check(TokenType::RPAREN)) return params;
    
    do {
        Param p;
        if (!check(TokenType::IDENT)) {
            error("Expected parameter name");
            break;
        }
        p.name = std::string(current_.text);
        p.span = current_.span;
        advance();
        
        // Optional type annotation: : Type
        if (match(TokenType::COLON)) {
            p.type = parse_type();
        }
        
        params.push_back(std::move(p));
    } while (match(TokenType::COMMA));
    
    return params;
}

Type Parser::parse_type() {
    Type t;
    t.span = current_.span;
    
    if (check(TokenType::IDENT)) {
        std::string_view name = current_.text;
        advance();
        
        if (name == "int") t.kind = TypeKind::INT;
        else if (name == "float") t.kind = TypeKind::FLOAT;
        else if (name == "void") t.kind = TypeKind::VOID;
        else if (name == "tensor") t.kind = TypeKind::TENSOR;
        else t.kind = TypeKind::UNKNOWN;
    } else {
        error("Expected type");
    }
    
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Statements
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<Stmt> Parser::parse_stmt() {
    skip_newlines();
    
    if (check(TokenType::LET)) return parse_let_stmt();
    if (check(TokenType::RETURN)) return parse_return_stmt();
    if (check(TokenType::IF)) return parse_if_stmt();
    if (check(TokenType::WHILE)) return parse_while_stmt();
    if (check(TokenType::LBRACE)) return parse_block();
    
    return parse_expr_stmt();
}

std::unique_ptr<Stmt> Parser::parse_let_stmt() {
    LetStmt let;
    let.span = current_.span;
    
    consume(TokenType::LET, "Expected 'let'");
    
    if (!check(TokenType::IDENT)) {
        error("Expected variable name");
        return nullptr;
    }
    let.name = std::string(current_.text);
    advance();
    
    // Optional type annotation
    if (match(TokenType::COLON)) {
        let.type_annot = parse_type();
    }
    
    consume(TokenType::EQ, "Expected '=' after variable name");
    let.init = parse_expr();
    
    // Optional semicolon
    match(TokenType::SEMICOLON);
    
    let.span = let.span.merge(previous_.span);
    return make_stmt(std::move(let));
}

std::unique_ptr<Stmt> Parser::parse_return_stmt() {
    ReturnStmt ret;
    ret.span = current_.span;
    
    consume(TokenType::RETURN, "Expected 'return'");
    
    // Optional return value
    if (!check(TokenType::SEMICOLON) && !check(TokenType::NEWLINE) && 
        !check(TokenType::RBRACE) && !current_.is_eof()) {
        ret.value = parse_expr();
    }
    
    match(TokenType::SEMICOLON);
    ret.span = ret.span.merge(previous_.span);
    return make_stmt(std::move(ret));
}

std::unique_ptr<Stmt> Parser::parse_if_stmt() {
    IfStmt if_stmt;
    if_stmt.span = current_.span;
    
    consume(TokenType::IF, "Expected 'if'");
    if_stmt.condition = parse_expr();
    
    skip_newlines();
    consume(TokenType::LBRACE, "Expected '{' after if condition");
    skip_newlines();
    
    while (!check(TokenType::RBRACE) && !current_.is_eof()) {
        auto stmt = parse_stmt();
        if (stmt) if_stmt.then_branch.push_back(std::move(stmt));
        skip_newlines();
    }
    consume(TokenType::RBRACE, "Expected '}' after if body");
    
    // Optional else
    skip_newlines();
    if (match(TokenType::ELSE)) {
        skip_newlines();
        consume(TokenType::LBRACE, "Expected '{' after else");
        skip_newlines();
        
        while (!check(TokenType::RBRACE) && !current_.is_eof()) {
            auto stmt = parse_stmt();
            if (stmt) if_stmt.else_branch.push_back(std::move(stmt));
            skip_newlines();
        }
        consume(TokenType::RBRACE, "Expected '}' after else body");
    }
    
    if_stmt.span = if_stmt.span.merge(previous_.span);
    return make_stmt(std::move(if_stmt));
}

std::unique_ptr<Stmt> Parser::parse_while_stmt() {
    WhileStmt while_stmt;
    while_stmt.span = current_.span;
    
    consume(TokenType::WHILE, "Expected 'while'");
    while_stmt.condition = parse_expr();
    
    skip_newlines();
    consume(TokenType::LBRACE, "Expected '{' after while condition");
    skip_newlines();
    
    while (!check(TokenType::RBRACE) && !current_.is_eof()) {
        auto stmt = parse_stmt();
        if (stmt) while_stmt.body.push_back(std::move(stmt));
        skip_newlines();
    }
    consume(TokenType::RBRACE, "Expected '}' after while body");
    
    while_stmt.span = while_stmt.span.merge(previous_.span);
    return make_stmt(std::move(while_stmt));
}

std::unique_ptr<Stmt> Parser::parse_block() {
    Block block;
    block.span = current_.span;
    
    consume(TokenType::LBRACE, "Expected '{'");
    skip_newlines();
    
    while (!check(TokenType::RBRACE) && !current_.is_eof()) {
        auto stmt = parse_stmt();
        if (stmt) block.stmts.push_back(std::move(stmt));
        skip_newlines();
    }
    
    consume(TokenType::RBRACE, "Expected '}'");
    block.span = block.span.merge(previous_.span);
    return make_stmt(std::move(block));
}

std::unique_ptr<Stmt> Parser::parse_expr_stmt() {
    ExprStmt expr_stmt;
    expr_stmt.span = current_.span;
    
    expr_stmt.expr = parse_expr();
    match(TokenType::SEMICOLON);
    
    if (expr_stmt.expr) {
        expr_stmt.span = expr_stmt.expr->span();
    }
    return make_stmt(std::move(expr_stmt));
}

// ─────────────────────────────────────────────────────────────────────────────
// Expressions (precedence climbing)
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<Expr> Parser::parse_expr() {
    return parse_equality();
}

std::unique_ptr<Expr> Parser::parse_equality() {
    auto expr = parse_comparison();
    
    while (match(TokenType::EQ_EQ) || match(TokenType::BANG_EQ)) {
        BinOp op = previous_.type == TokenType::EQ_EQ ? BinOp::EQ : BinOp::NE;
        auto right = parse_comparison();
        
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(expr);
        bin.right = std::move(right);
        bin.span = bin.left->span().merge(bin.right->span());
        
        expr = make_expr(std::move(bin));
    }
    
    return expr;
}

std::unique_ptr<Expr> Parser::parse_comparison() {
    auto expr = parse_term();
    
    while (match(TokenType::LT) || match(TokenType::GT) ||
           match(TokenType::LT_EQ) || match(TokenType::GT_EQ)) {
        BinOp op;
        switch (previous_.type) {
            case TokenType::LT: op = BinOp::LT; break;
            case TokenType::GT: op = BinOp::GT; break;
            case TokenType::LT_EQ: op = BinOp::LE; break;
            case TokenType::GT_EQ: op = BinOp::GE; break;
            default: op = BinOp::LT; break;
        }
        auto right = parse_term();
        
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(expr);
        bin.right = std::move(right);
        bin.span = bin.left->span().merge(bin.right->span());
        
        expr = make_expr(std::move(bin));
    }
    
    return expr;
}

std::unique_ptr<Expr> Parser::parse_term() {
    auto expr = parse_factor();
    
    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        BinOp op = previous_.type == TokenType::PLUS ? BinOp::ADD : BinOp::SUB;
        auto right = parse_factor();
        
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(expr);
        bin.right = std::move(right);
        bin.span = bin.left->span().merge(bin.right->span());
        
        expr = make_expr(std::move(bin));
    }
    
    return expr;
}

std::unique_ptr<Expr> Parser::parse_factor() {
    auto expr = parse_unary();
    
    while (match(TokenType::STAR) || match(TokenType::SLASH)) {
        BinOp op = previous_.type == TokenType::STAR ? BinOp::MUL : BinOp::DIV;
        auto right = parse_unary();
        
        BinaryExpr bin;
        bin.op = op;
        bin.left = std::move(expr);
        bin.right = std::move(right);
        bin.span = bin.left->span().merge(bin.right->span());
        
        expr = make_expr(std::move(bin));
    }
    
    return expr;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (match(TokenType::MINUS) || match(TokenType::BANG)) {
        UnaryOp op = previous_.type == TokenType::MINUS ? UnaryOp::NEG : UnaryOp::NOT;
        Span start = previous_.span;
        auto operand = parse_unary();
        
        UnaryExpr un;
        un.op = op;
        un.operand = std::move(operand);
        un.span = start.merge(un.operand->span());
        
        return make_expr(std::move(un));
    }
    
    return parse_call();
}

std::unique_ptr<Expr> Parser::parse_call() {
    auto expr = parse_primary();
    
    // Check for function call: identifier followed by (
    if (expr && expr->is<Identifier>() && match(TokenType::LPAREN)) {
        CallExpr call;
        call.callee = expr->as<Identifier>().name;
        call.span = expr->span();
        
        // Parse arguments (including keyword arguments)
        if (!check(TokenType::RPAREN)) {
            do {
                // Check for keyword argument: name = expr
                if (check(TokenType::IDENT) && lexer_.peek().type == TokenType::EQ) {
                    advance();  // consume identifier (keyword name)
                    advance();  // consume '='
                }
                call.args.push_back(parse_expr());
            } while (match(TokenType::COMMA));
        }
        
        consume(TokenType::RPAREN, "Expected ')' after arguments");
        call.span = call.span.merge(previous_.span);
        
        return make_expr(std::move(call));
    }
    
    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    // Integer literal
    if (match(TokenType::INT_LIT)) {
        IntLiteral lit;
        lit.span = previous_.span;
        lit.value = 0;
        
        auto [ptr, ec] = std::from_chars(
            previous_.text.data(), 
            previous_.text.data() + previous_.text.size(),
            lit.value
        );
        
        return make_expr(std::move(lit));
    }
    
    // Float literal
    if (match(TokenType::FLOAT_LIT)) {
        FloatLiteral lit;
        lit.span = previous_.span;
        lit.value = std::stod(std::string(previous_.text));
        return make_expr(std::move(lit));
    }
    
    // String literal
    if (match(TokenType::STRING_LIT)) {
        StringLiteral lit;
        lit.span = previous_.span;
        // Strip quotes from the text
        std::string_view text = previous_.text;
        if (text.size() >= 2) {
            lit.value = std::string(text.substr(1, text.size() - 2));
        }
        return make_expr(std::move(lit));
    }
    
    // Identifier
    if (match(TokenType::IDENT)) {
        Identifier id;
        id.name = std::string(previous_.text);
        id.span = previous_.span;
        return make_expr(std::move(id));
    }
    
    // Grouped expression
    if (match(TokenType::LPAREN)) {
        Span start = previous_.span;
        auto inner = parse_expr();
        consume(TokenType::RPAREN, "Expected ')' after expression");
        
        GroupExpr group;
        group.inner = std::move(inner);
        group.span = start.merge(previous_.span);
        
        return make_expr(std::move(group));
    }
    
    error("Expected expression");
    return nullptr;
}

} // namespace parser
} // namespace zero
