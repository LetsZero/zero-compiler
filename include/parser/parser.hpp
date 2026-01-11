#ifndef ZERO_PARSER_PARSER_HPP
#define ZERO_PARSER_PARSER_HPP

/**
 * @file parser.hpp
 * @brief Zero Compiler — Parser
 * 
 * Recursive descent parser that produces AST from token stream.
 */

#include "ast/ast.hpp"
#include "lexer/lexer.hpp"
#include "source/source.hpp"

#include <string>
#include <vector>

namespace zero {
namespace parser {

/**
 * Parser error information
 */
struct ParseError {
    std::string message;
    source::Span span;
};

/**
 * Recursive descent parser for Zero language.
 * 
 * Usage:
 *   SourceManager sm;
 *   SourceID id = sm.load("file.zero");
 *   Parser parser(sm, id);
 *   auto program = parser.parse();
 *   if (parser.had_error()) { ... }
 */
class Parser {
public:
    Parser(source::SourceManager& sm, source::SourceID id);
    
    /**
     * Parse the entire program.
     */
    ast::Program parse();
    
    /**
     * Check if any errors occurred during parsing.
     */
    bool had_error() const { return had_error_; }
    
    /**
     * Get list of parse errors.
     */
    const std::vector<ParseError>& errors() const { return errors_; }

private:
    lexer::Lexer lexer_;
    source::SourceManager& sm_;
    source::SourceID source_id_;
    
    lexer::Token current_;
    lexer::Token previous_;
    
    bool had_error_ = false;
    bool panic_mode_ = false;
    std::vector<ParseError> errors_;
    
    // ─────────────────────────────────────────────────────────────────────
    // Token handling
    // ─────────────────────────────────────────────────────────────────────
    
    void advance();
    bool check(lexer::TokenType type) const;
    bool match(lexer::TokenType type);
    void consume(lexer::TokenType type, const char* message);
    void skip_newlines();
    
    // ─────────────────────────────────────────────────────────────────────
    // Error handling
    // ─────────────────────────────────────────────────────────────────────
    
    void error(const char* message);
    void error_at(const lexer::Token& token, const char* message);
    void synchronize();
    
    // ─────────────────────────────────────────────────────────────────────
    // Parsing rules
    // ─────────────────────────────────────────────────────────────────────
    
    // Top-level
    ast::FnDecl parse_fn_decl();
    std::vector<ast::Param> parse_params();
    ast::Type parse_type();
    
    // Statements
    std::unique_ptr<ast::Stmt> parse_stmt();
    std::unique_ptr<ast::Stmt> parse_let_stmt();
    std::unique_ptr<ast::Stmt> parse_return_stmt();
    std::unique_ptr<ast::Stmt> parse_if_stmt();
    std::unique_ptr<ast::Stmt> parse_while_stmt();
    std::unique_ptr<ast::Stmt> parse_block();
    std::unique_ptr<ast::Stmt> parse_expr_stmt();
    
    // Expressions (precedence climbing)
    std::unique_ptr<ast::Expr> parse_expr();
    std::unique_ptr<ast::Expr> parse_equality();
    std::unique_ptr<ast::Expr> parse_comparison();
    std::unique_ptr<ast::Expr> parse_term();
    std::unique_ptr<ast::Expr> parse_factor();
    std::unique_ptr<ast::Expr> parse_unary();
    std::unique_ptr<ast::Expr> parse_call();
    std::unique_ptr<ast::Expr> parse_primary();
};

} // namespace parser
} // namespace zero

#endif // ZERO_PARSER_PARSER_HPP
