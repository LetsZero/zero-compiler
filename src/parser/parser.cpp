/**
 * @file parser.cpp
 * @brief Zero Compiler — Parser Implementation
 */

#include "parser/parser.hpp"
#include <charconv>
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include <string>

namespace zero {
namespace parser {

using namespace lexer;
using namespace ast;
using namespace source;

namespace {
// Parse a double from token text, guarding against overflow and malformed
// input. Returns true on success. `std::stod` (used previously) throws
// std::out_of_range on overflow — an *uncaught* exception that aborted the
// whole compiler, violating the "errors are data, not control flow" rule.
// Underflow to a subnormal/zero is accepted (not an error).
bool try_parse_double(std::string_view text, double& out) {
    std::string s(text);
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return false;                                  // no conversion
    if (errno == ERANGE && (v == HUGE_VAL || v == -HUGE_VAL)) return false; // overflow
    out = v;
    return true;
}
} // namespace

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
    
    // Parse body statements (spec 018b: shared loop with no-progress guard).
    parse_stmt_block(fn.body);

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

    // `tensor` became a keyword in spec 004, so it lexes as TokenType::TENSOR
    // rather than IDENT. Accept either spelling: the keyword in type
    // position means TypeKind::TENSOR; an identifier matches by name.
    if (check(TokenType::TENSOR)) {
        advance();
        t.kind = TypeKind::TENSOR;
    } else if (check(TokenType::IDENT)) {
        std::string_view name = current_.text;
        advance();

        if (name == "int") t.kind = TypeKind::INT;
        else if (name == "float") t.kind = TypeKind::FLOAT;
        else if (name == "void") t.kind = TypeKind::VOID;
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

    // Assignment: `name = expr`. Disambiguated from an expression statement by
    // one token of lookahead — an IDENT immediately followed by '=' (a single
    // '=', not '==', which lexes as EQ_EQ).
    if (check(TokenType::IDENT) && lexer_.peek().type == TokenType::EQ) {
        return parse_assign_stmt();
    }

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

std::unique_ptr<Stmt> Parser::parse_assign_stmt() {
    AssignStmt assign;
    assign.span = current_.span;

    assign.name = std::string(current_.text);
    advance();  // consume IDENT
    consume(TokenType::EQ, "Expected '=' in assignment");
    assign.value = parse_expr();

    match(TokenType::SEMICOLON);
    assign.span = assign.span.merge(previous_.span);
    return make_stmt(std::move(assign));
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
    
    parse_stmt_block(if_stmt.then_branch);
    consume(TokenType::RBRACE, "Expected '}' after if body");
    
    // Optional else
    skip_newlines();
    if (match(TokenType::ELSE)) {
        skip_newlines();
        consume(TokenType::LBRACE, "Expected '{' after else");
        skip_newlines();
        
        parse_stmt_block(if_stmt.else_branch);
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
    
    parse_stmt_block(while_stmt.body);
    consume(TokenType::RBRACE, "Expected '}' after while body");
    
    while_stmt.span = while_stmt.span.merge(previous_.span);
    return make_stmt(std::move(while_stmt));
}

std::unique_ptr<Stmt> Parser::parse_block() {
    Block block;
    block.span = current_.span;
    
    consume(TokenType::LBRACE, "Expected '{'");
    skip_newlines();
    
    parse_stmt_block(block.stmts);

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
        
        // Parse arguments. Spec 018b: newlines are insignificant inside the
        // argument list, so multi-line calls (matmul(a,\n b)) parse.
        skip_newlines();
        if (!check(TokenType::RPAREN)) {
            do {
                skip_newlines();
                // Check for keyword argument: name = expr
                if (check(TokenType::IDENT) && lexer_.peek().type == TokenType::EQ) {
                    advance();  // consume identifier (keyword name)
                    advance();  // consume '='
                }
                call.args.push_back(parse_expr());
                skip_newlines();
            } while (match(TokenType::COMMA));
        }

        skip_newlines();
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
        (void)ptr;
        if (ec != std::errc()) {
            // Out-of-range integers previously parsed to a silent 0.
            error_at(previous_, "integer literal out of range");
        }

        return make_expr(std::move(lit));
    }

    // Float literal
    if (match(TokenType::FLOAT_LIT)) {
        FloatLiteral lit;
        lit.span = previous_.span;
        if (!try_parse_double(previous_.text, lit.value)) {
            lit.value = 0.0;
            error_at(previous_, "floating-point literal out of range");
        }
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
        skip_newlines();   // spec 018b: newlines insignificant inside parens
        auto inner = parse_expr();
        skip_newlines();
        consume(TokenType::RPAREN, "Expected ')' after expression");

        GroupExpr group;
        group.inner = std::move(inner);
        group.span = start.merge(previous_.span);

        return make_expr(std::move(group));
    }

    // Tensor literal (spec 004 = 1-D, spec 015 = 2-D):
    //   1-D: tensor([ a, b, c ])
    //   2-D: tensor([ [a,b], [c,d] ])
    if (match(TokenType::TENSOR)) {
        Span start = previous_.span;
        consume(TokenType::LPAREN, "Expected '(' after 'tensor'");
        skip_newlines();   // spec 018b: newlines insignificant inside the literal
        consume(TokenType::LBRACKET, "Expected '[' to start tensor literal");
        skip_newlines();

        TensorLiteral lit;
        bool failed = false;

        if (check(TokenType::LBRACKET)) {
            // 2-D: a list of rows. Each row is itself a bracketed number list.
            int64_t cols = -1;
            int64_t rows = 0;
            while (!check(TokenType::RBRACKET) && !current_.is_eof()) {
                consume(TokenType::LBRACKET, "Expected '[' to start a tensor row");
                skip_newlines();
                if (check(TokenType::LBRACKET)) {
                    error("only 1-D and 2-D tensor literals are supported");
                    failed = true;
                    break;
                }
                size_t n = 0;
                if (!parse_number_row(lit.values, n)) { failed = true; break; }
                skip_newlines();
                consume(TokenType::RBRACKET, "Expected ']' to end a tensor row");
                if (cols < 0) {
                    cols = static_cast<int64_t>(n);
                } else if (static_cast<int64_t>(n) != cols) {
                    error("ragged tensor literal: rows have differing lengths");
                    failed = true;
                    break;
                }
                ++rows;
                if (!match(TokenType::COMMA)) break;
                skip_newlines();   // newlines between rows
            }
            lit.shape = { rows, cols < 0 ? 0 : cols };
        } else {
            // 1-D: a single number list.
            size_t n = 0;
            if (!parse_number_row(lit.values, n)) failed = true;
            lit.shape = { static_cast<int64_t>(n) };
        }

        // Recovery (spec 007, hardened in spec 015): on any malformed
        // literal, skip to the enclosing statement boundary (';' / '}' / EOF)
        // and return immediately, WITHOUT running the closing consume()s.
        // This guarantees the cursor advances to a clean resync point no
        // matter how the brackets are malformed (ragged, 3-D, stray tokens),
        // so the enclosing statement-body loop always makes forward progress
        // and cannot spin. An error has already been reported, so the
        // (incomplete) literal node is never executed.
        if (failed) {
            while (!current_.is_eof()
                   && !check(TokenType::SEMICOLON)
                   && !check(TokenType::RBRACE)) {
                advance();
            }
            lit.span = start.merge(previous_.span);
            return make_expr(std::move(lit));
        }

        skip_newlines();   // spec 018b: newlines before the closing ] )
        consume(TokenType::RBRACKET, "Expected ']' to end tensor literal");
        skip_newlines();
        consume(TokenType::RPAREN, "Expected ')' to end tensor literal");

        if (lit.values.empty()) {
            error("tensor literal must have at least one element");
        }

        lit.span = start.merge(previous_.span);
        return make_expr(std::move(lit));
    }

    error("Expected expression");
    return nullptr;
}

bool Parser::parse_number_row(std::vector<double>& out, size_t& count) {
    count = 0;
    if (check(TokenType::RBRACKET)) return true;  // empty row
    for (;;) {
        bool negate = match(TokenType::MINUS);
        if (match(TokenType::INT_LIT)) {
            int64_t v = 0;
            auto [p, ec] = std::from_chars(
                previous_.text.data(),
                previous_.text.data() + previous_.text.size(),
                v);
            (void)p;
            if (ec != std::errc()) {
                error_at(previous_, "integer literal out of range");
                return false;
            }
            double val = static_cast<double>(v);
            out.push_back(negate ? -val : val);
        } else if (match(TokenType::FLOAT_LIT)) {
            double val = 0.0;
            if (!try_parse_double(previous_.text, val)) {
                error_at(previous_, "floating-point literal out of range");
                return false;
            }
            out.push_back(negate ? -val : val);
        } else {
            error("Expected numeric literal in tensor element list");
            return false;
        }
        ++count;
        if (!match(TokenType::COMMA)) break;
        skip_newlines();   // spec 018b: newlines insignificant between elements
    }
    return true;
}

void Parser::parse_stmt_block(std::vector<std::unique_ptr<ast::Stmt>>& out) {
    // Spec 018b: the single statement-body loop shared by fn/if/else/while/
    // block. The no-progress guard guarantees forward progress so that
    // malformed input can never spin the loop forever (consume() does not
    // advance on a mismatch, which previously allowed an infinite loop).
    while (!check(TokenType::RBRACE) && !current_.is_eof()) {
        const uint32_t before = current_.span.start_offset;
        auto stmt = parse_stmt();
        if (stmt) out.push_back(std::move(stmt));
        skip_newlines();
        if (!current_.is_eof() && current_.span.start_offset == before) {
            advance();
        }
    }
}

} // namespace parser
} // namespace zero
