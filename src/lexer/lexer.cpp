/**
 * @file lexer.cpp
 * @brief Zero Compiler — Lexer Implementation
 */

#include "lexer/lexer.hpp"
#include <cstring>

namespace zero {
namespace lexer {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

Lexer::Lexer(source::SourceManager& sm, source::SourceID id)
    : sm_(sm), source_id_(id), source_(sm.get(id)) {
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

Token Lexer::next() {
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_;
    }
    return scan_token();
}

Token Lexer::peek() {
    if (!has_peeked_) {
        peeked_ = scan_token();
        has_peeked_ = true;
    }
    return peeked_;
}

bool Lexer::at_end() const {
    if (has_peeked_) {
        return peeked_.type == TokenType::EOF_TOKEN;
    }
    return is_at_end();
}

// ─────────────────────────────────────────────────────────────────────────────
// Character helpers
// ─────────────────────────────────────────────────────────────────────────────

char Lexer::peek_char() const {
    if (!source_ || current_ >= source_->content.size()) return '\0';
    return source_->content[current_];
}

char Lexer::peek_next() const {
    if (!source_ || current_ + 1 >= source_->content.size()) return '\0';
    return source_->content[current_ + 1];
}

char Lexer::advance() {
    if (!source_ || current_ >= source_->content.size()) return '\0';
    return source_->content[current_++];
}

bool Lexer::match(char expected) {
    if (is_at_end()) return false;
    if (peek_char() != expected) return false;
    current_++;
    return true;
}

bool Lexer::is_at_end() const {
    return !source_ || current_ >= source_->content.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Whitespace and comments
// ─────────────────────────────────────────────────────────────────────────────

void Lexer::skip_whitespace() {
    while (!is_at_end()) {
        char c = peek_char();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    skip_line_comment();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

void Lexer::skip_line_comment() {
    // Skip the //
    advance();
    advance();
    // Skip to end of line
    while (!is_at_end() && peek_char() != '\n') {
        advance();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Token creation
// ─────────────────────────────────────────────────────────────────────────────

Token Lexer::make_token(TokenType type) {
    Token tok;
    tok.type = type;
    tok.span = source::Span::range(source_id_, start_, current_);
    if (source_) {
        tok.text = std::string_view(source_->content).substr(start_, current_ - start_);
    }
    return tok;
}

Token Lexer::error_token(const char* message) {
    Token tok;
    tok.type = TokenType::ERROR;
    tok.span = source::Span::point(source_id_, current_);
    tok.text = message;
    return tok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main scanning
// ─────────────────────────────────────────────────────────────────────────────

Token Lexer::scan_token() {
    skip_whitespace();
    start_ = current_;
    
    if (is_at_end()) {
        return make_token(TokenType::EOF_TOKEN);
    }
    
    char c = advance();
    
    // Identifiers and keywords
    if (is_alpha(c)) {
        return scan_identifier();
    }
    
    // Numbers
    if (is_digit(c)) {
        return scan_number();
    }
    
    // Single and multi-character tokens
    switch (c) {
        // Delimiters
        case '(': return make_token(TokenType::LPAREN);
        case ')': return make_token(TokenType::RPAREN);
        case '{': return make_token(TokenType::LBRACE);
        case '}': return make_token(TokenType::RBRACE);
        case '[': return make_token(TokenType::LBRACKET);
        case ']': return make_token(TokenType::RBRACKET);
        case ',': return make_token(TokenType::COMMA);
        case ':': return make_token(TokenType::COLON);
        case ';': return make_token(TokenType::SEMICOLON);
        case '\n': return make_token(TokenType::NEWLINE);
        
        // Operators
        case '+': return make_token(TokenType::PLUS);
        case '*': return make_token(TokenType::STAR);
        case '/': return make_token(TokenType::SLASH);
        
        case '-':
            return make_token(match('>') ? TokenType::ARROW : TokenType::MINUS);
        case '=':
            return make_token(match('=') ? TokenType::EQ_EQ : TokenType::EQ);
        case '!':
            return make_token(match('=') ? TokenType::BANG_EQ : TokenType::BANG);
        case '<':
            return make_token(match('=') ? TokenType::LT_EQ : TokenType::LT);
        case '>':
            return make_token(match('=') ? TokenType::GT_EQ : TokenType::GT);
    }
    
    return error_token("Unexpected character");
}

// ─────────────────────────────────────────────────────────────────────────────
// Identifier scanning
// ─────────────────────────────────────────────────────────────────────────────

Token Lexer::scan_identifier() {
    while (is_alnum(peek_char())) {
        advance();
    }
    return make_token(identifier_type());
}

TokenType Lexer::identifier_type() {
    // Simple keyword matching using first character
    if (!source_) return TokenType::IDENT;
    
    uint32_t length = current_ - start_;
    const char* text = source_->content.data() + start_;
    
    switch (text[0]) {
        case 'e':
            return check_keyword(1, 3, "lse", TokenType::ELSE);
        case 'f':
            if (length > 1) {
                switch (text[1]) {
                    case 'n': return length == 2 ? TokenType::FN : TokenType::IDENT;
                }
            }
            break;
        case 'i':
            return check_keyword(1, 1, "f", TokenType::IF);
        case 'l':
            return check_keyword(1, 2, "et", TokenType::LET);
        case 'r':
            return check_keyword(1, 5, "eturn", TokenType::RETURN);
        case 'w':
            return check_keyword(1, 4, "hile", TokenType::WHILE);
    }
    
    return TokenType::IDENT;
}

TokenType Lexer::check_keyword(uint32_t start, uint32_t length, 
                                const char* rest, TokenType type) {
    uint32_t tok_len = current_ - start_;
    if (tok_len == start + length) {
        const char* text = source_->content.data() + start_ + start;
        if (std::memcmp(text, rest, length) == 0) {
            return type;
        }
    }
    return TokenType::IDENT;
}

// ─────────────────────────────────────────────────────────────────────────────
// Number scanning
// ─────────────────────────────────────────────────────────────────────────────

Token Lexer::scan_number() {
    // Consume integer part
    while (is_digit(peek_char())) {
        advance();
    }
    
    // Check for decimal
    if (peek_char() == '.' && is_digit(peek_next())) {
        advance();  // consume '.'
        while (is_digit(peek_char())) {
            advance();
        }
        return make_token(TokenType::FLOAT_LIT);
    }
    
    return make_token(TokenType::INT_LIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Character classification
// ─────────────────────────────────────────────────────────────────────────────

bool Lexer::is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

bool Lexer::is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool Lexer::is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

} // namespace lexer
} // namespace zero
