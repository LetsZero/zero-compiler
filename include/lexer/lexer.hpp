#ifndef ZERO_LEXER_LEXER_HPP
#define ZERO_LEXER_LEXER_HPP

/**
 * @file lexer.hpp
 * @brief Zero Compiler — Lexer
 * 
 * Tokenizes source code into a stream of tokens.
 */

#include "token.hpp"
#include "source/source.hpp"

namespace zero {
namespace lexer {

/**
 * Lexer for Zero source code.
 * 
 * Usage:
 *   SourceManager sm;
 *   SourceID id = sm.load("file.zero");
 *   Lexer lexer(sm, id);
 *   while (!lexer.at_end()) {
 *       Token tok = lexer.next();
 *       // process token
 *   }
 */
class Lexer {
public:
    /**
     * Create a lexer for the given source file.
     */
    Lexer(source::SourceManager& sm, source::SourceID id);
    
    /**
     * Get next token and advance.
     */
    Token next();
    
    /**
     * Peek at next token without consuming.
     */
    Token peek();
    
    /**
     * Check if at end of input.
     */
    bool at_end() const;
    
    /**
     * Get current position (byte offset).
     */
    uint32_t position() const { return current_; }

private:
    source::SourceManager& sm_;
    source::SourceID source_id_;
    const source::SourceFile* source_;
    
    uint32_t start_ = 0;      // Start of current token
    uint32_t current_ = 0;    // Current position
    
    Token peeked_;
    bool has_peeked_ = false;
    
    // ─────────────────────────────────────────────────────────────────────
    // Scanning helpers
    // ─────────────────────────────────────────────────────────────────────
    
    char peek_char() const;
    char peek_next() const;
    char advance();
    bool match(char expected);
    bool is_at_end() const;
    
    void skip_whitespace();
    void skip_line_comment();
    
    // ─────────────────────────────────────────────────────────────────────
    // Token scanning
    // ─────────────────────────────────────────────────────────────────────
    
    Token scan_token();
    Token make_token(TokenType type);
    Token error_token(const char* message);
    
    Token scan_identifier();
    Token scan_number();
    Token scan_string();
    
    TokenType identifier_type();
    TokenType check_keyword(uint32_t start, uint32_t length, 
                            const char* rest, TokenType type);
    
    // ─────────────────────────────────────────────────────────────────────
    // Character classification
    // ─────────────────────────────────────────────────────────────────────
    
    static bool is_alpha(char c);
    static bool is_digit(char c);
    static bool is_alnum(char c);
};

} // namespace lexer
} // namespace zero

#endif // ZERO_LEXER_LEXER_HPP
