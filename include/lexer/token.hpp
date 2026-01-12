#ifndef ZERO_LEXER_TOKEN_HPP
#define ZERO_LEXER_TOKEN_HPP

/**
 * @file token.hpp
 * @brief Zero Compiler — Token Types and Token Structure
 */

#include "source/source.hpp"
#include <string_view>
#include <string>

namespace zero {
namespace lexer {

// ─────────────────────────────────────────────────────────────────────────────
// Token Types
// ─────────────────────────────────────────────────────────────────────────────

enum class TokenType {
    // Literals
    IDENT,          // foo, bar, main
    INT_LIT,        // 42, 100
    FLOAT_LIT,      // 3.14, 0.5
    STRING_LIT,     // "hello"
    
    // Keywords
    FN,             // fn
    LET,            // let
    RETURN,         // return
    IF,             // if
    ELSE,           // else
    WHILE,          // while
    USE,            // use
    
    // Operators
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /
    EQ,             // =
    EQ_EQ,          // ==
    BANG,           // !
    BANG_EQ,        // !=
    LT,             // <
    GT,             // >
    LT_EQ,          // <=
    GT_EQ,          // >=
    
    // Delimiters
    LPAREN,         // (
    RPAREN,         // )
    LBRACE,         // {
    RBRACE,         // }
    LBRACKET,       // [
    RBRACKET,       // ]
    COMMA,          // ,
    COLON,          // :
    SEMICOLON,      // ;
    ARROW,          // ->
    
    // Special
    NEWLINE,        // \n
    EOF_TOKEN,      // End of file
    ERROR           // Lexical error
};

// ─────────────────────────────────────────────────────────────────────────────
// Token
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A token produced by the lexer.
 */
struct Token {
    TokenType type = TokenType::EOF_TOKEN;
    source::Span span;
    std::string_view text;  // View into source content
    
    /**
     * Check if token is of given type
     */
    bool is(TokenType t) const { return type == t; }
    
    /**
     * Check if token is an error
     */
    bool is_error() const { return type == TokenType::ERROR; }
    
    /**
     * Check if token is end of file
     */
    bool is_eof() const { return type == TokenType::EOF_TOKEN; }
    
    /**
     * Get token type as string (for debugging)
     */
    static const char* type_name(TokenType t);
};

// ─────────────────────────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Get token type name as string
 */
inline const char* Token::type_name(TokenType t) {
    switch (t) {
        case TokenType::IDENT:      return "IDENT";
        case TokenType::INT_LIT:    return "INT";
        case TokenType::FLOAT_LIT:  return "FLOAT";
        case TokenType::STRING_LIT: return "STRING";
        case TokenType::FN:         return "FN";
        case TokenType::LET:        return "LET";
        case TokenType::RETURN:     return "RETURN";
        case TokenType::IF:         return "IF";
        case TokenType::ELSE:       return "ELSE";
        case TokenType::WHILE:      return "WHILE";
        case TokenType::USE:        return "USE";
        case TokenType::PLUS:       return "PLUS";
        case TokenType::MINUS:      return "MINUS";
        case TokenType::STAR:       return "STAR";
        case TokenType::SLASH:      return "SLASH";
        case TokenType::EQ:         return "EQ";
        case TokenType::EQ_EQ:      return "EQ_EQ";
        case TokenType::BANG:       return "BANG";
        case TokenType::BANG_EQ:    return "BANG_EQ";
        case TokenType::LT:         return "LT";
        case TokenType::GT:         return "GT";
        case TokenType::LT_EQ:      return "LT_EQ";
        case TokenType::GT_EQ:      return "GT_EQ";
        case TokenType::LPAREN:     return "LPAREN";
        case TokenType::RPAREN:     return "RPAREN";
        case TokenType::LBRACE:     return "LBRACE";
        case TokenType::RBRACE:     return "RBRACE";
        case TokenType::LBRACKET:   return "LBRACKET";
        case TokenType::RBRACKET:   return "RBRACKET";
        case TokenType::COMMA:      return "COMMA";
        case TokenType::COLON:      return "COLON";
        case TokenType::SEMICOLON:  return "SEMICOLON";
        case TokenType::ARROW:      return "ARROW";
        case TokenType::NEWLINE:    return "NEWLINE";
        case TokenType::EOF_TOKEN:  return "EOF";
        case TokenType::ERROR:      return "ERROR";
        default:                    return "UNKNOWN";
    }
}

} // namespace lexer
} // namespace zero

#endif // ZERO_LEXER_TOKEN_HPP
