/**
 * @file test_lexer.cpp
 * @brief Unit tests for Zero Lexer
 */

#include "lexer/lexer.hpp"
#include "source/source.hpp"

#include <iostream>
#include <vector>
#include <cassert>

using namespace zero::lexer;
using namespace zero::source;

// ─────────────────────────────────────────────────────────────────────────────
// Test utilities
// ─────────────────────────────────────────────────────────────────────────────

#define TEST(name) void name(); \
    static struct name##_register { \
        name##_register() { tests.push_back({#name, name}); } \
    } name##_instance; \
    void name()

struct TestCase {
    const char* name;
    void (*func)();
};

static std::vector<TestCase> tests;

static int run_all_tests() {
    int passed = 0;
    int failed = 0;
    
    for (const auto& test : tests) {
        std::cout << "  Running " << test.name << "... ";
        try {
            test.func();
            std::cout << "\033[32mPASS\033[0m\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "\033[31mFAIL\033[0m: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "\033[31mFAIL\033[0m: unknown exception\n";
            ++failed;
        }
    }
    
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(test_empty_input) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "");
    Lexer lexer(sm, id);
    
    Token tok = lexer.next();
    assert(tok.is_eof());
}

TEST(test_single_tokens) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "( ) { } [ ] , : ;");
    Lexer lexer(sm, id);
    
    assert(lexer.next().is(TokenType::LPAREN));
    assert(lexer.next().is(TokenType::RPAREN));
    assert(lexer.next().is(TokenType::LBRACE));
    assert(lexer.next().is(TokenType::RBRACE));
    assert(lexer.next().is(TokenType::LBRACKET));
    assert(lexer.next().is(TokenType::RBRACKET));
    assert(lexer.next().is(TokenType::COMMA));
    assert(lexer.next().is(TokenType::COLON));
    assert(lexer.next().is(TokenType::SEMICOLON));
    assert(lexer.next().is_eof());
}

TEST(test_operators) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "+ - * / = == ! != < > <= >=");
    Lexer lexer(sm, id);
    
    assert(lexer.next().is(TokenType::PLUS));
    assert(lexer.next().is(TokenType::MINUS));
    assert(lexer.next().is(TokenType::STAR));
    assert(lexer.next().is(TokenType::SLASH));
    assert(lexer.next().is(TokenType::EQ));
    assert(lexer.next().is(TokenType::EQ_EQ));
    assert(lexer.next().is(TokenType::BANG));
    assert(lexer.next().is(TokenType::BANG_EQ));
    assert(lexer.next().is(TokenType::LT));
    assert(lexer.next().is(TokenType::GT));
    assert(lexer.next().is(TokenType::LT_EQ));
    assert(lexer.next().is(TokenType::GT_EQ));
}

TEST(test_arrow) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "->");
    Lexer lexer(sm, id);
    
    Token tok = lexer.next();
    assert(tok.is(TokenType::ARROW));
    assert(tok.text == "->");
}

TEST(test_keywords) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn let return if else while");
    Lexer lexer(sm, id);
    
    assert(lexer.next().is(TokenType::FN));
    assert(lexer.next().is(TokenType::LET));
    assert(lexer.next().is(TokenType::RETURN));
    assert(lexer.next().is(TokenType::IF));
    assert(lexer.next().is(TokenType::ELSE));
    assert(lexer.next().is(TokenType::WHILE));
}

TEST(test_identifiers) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "foo bar _test main123");
    Lexer lexer(sm, id);
    
    Token t1 = lexer.next();
    assert(t1.is(TokenType::IDENT));
    assert(t1.text == "foo");
    
    Token t2 = lexer.next();
    assert(t2.is(TokenType::IDENT));
    assert(t2.text == "bar");
    
    Token t3 = lexer.next();
    assert(t3.is(TokenType::IDENT));
    assert(t3.text == "_test");
    
    Token t4 = lexer.next();
    assert(t4.is(TokenType::IDENT));
    assert(t4.text == "main123");
}

TEST(test_numbers) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "42 100 3.14 0.5");
    Lexer lexer(sm, id);
    
    Token t1 = lexer.next();
    assert(t1.is(TokenType::INT_LIT));
    assert(t1.text == "42");
    
    Token t2 = lexer.next();
    assert(t2.is(TokenType::INT_LIT));
    assert(t2.text == "100");
    
    Token t3 = lexer.next();
    assert(t3.is(TokenType::FLOAT_LIT));
    assert(t3.text == "3.14");
    
    Token t4 = lexer.next();
    assert(t4.is(TokenType::FLOAT_LIT));
    assert(t4.text == "0.5");
}

TEST(test_function_definition) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main() { return 0; }");
    Lexer lexer(sm, id);
    
    assert(lexer.next().is(TokenType::FN));
    
    Token ident = lexer.next();
    assert(ident.is(TokenType::IDENT));
    assert(ident.text == "main");
    
    assert(lexer.next().is(TokenType::LPAREN));
    assert(lexer.next().is(TokenType::RPAREN));
    assert(lexer.next().is(TokenType::LBRACE));
    assert(lexer.next().is(TokenType::RETURN));
    
    Token num = lexer.next();
    assert(num.is(TokenType::INT_LIT));
    assert(num.text == "0");
    
    assert(lexer.next().is(TokenType::SEMICOLON));
    assert(lexer.next().is(TokenType::RBRACE));
    assert(lexer.next().is_eof());
}

TEST(test_peek) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "foo bar");
    Lexer lexer(sm, id);
    
    Token peeked = lexer.peek();
    assert(peeked.is(TokenType::IDENT));
    assert(peeked.text == "foo");
    
    // Peek again should return same token
    Token peeked2 = lexer.peek();
    assert(peeked2.text == "foo");
    
    // Next should return same token and advance
    Token next = lexer.next();
    assert(next.text == "foo");
    
    // Now next should be "bar"
    Token next2 = lexer.next();
    assert(next2.text == "bar");
}

TEST(test_comments) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "foo // this is a comment\nbar");
    Lexer lexer(sm, id);
    
    Token t1 = lexer.next();
    assert(t1.is(TokenType::IDENT));
    assert(t1.text == "foo");
    
    Token t2 = lexer.next();
    assert(t2.is(TokenType::NEWLINE));
    
    Token t3 = lexer.next();
    assert(t3.is(TokenType::IDENT));
    assert(t3.text == "bar");
}

TEST(test_span_tracking) {
    SourceManager sm;
    SourceID id = sm.load_from_string("test.zero", "fn main");
    Lexer lexer(sm, id);
    
    Token t1 = lexer.next();
    assert(t1.span.start_offset == 0);
    assert(t1.span.end_offset == 2);
    
    Token t2 = lexer.next();
    assert(t2.span.start_offset == 3);
    assert(t2.span.end_offset == 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  Zero Lexer Tests\n";
    std::cout << "============================================\n\n";
    
    return run_all_tests();
}
