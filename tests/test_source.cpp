/**
 * @file test_source.cpp
 * @brief Unit tests for Zero Source Management
 */

#include "source/source.hpp"

#include <iostream>
#include <fstream>
#include <cassert>
#include <cstring>

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
    
    std::cout << "\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(test_span_invalid) {
    Span s = Span::invalid();
    assert(!s.valid());
    assert(s.source_id == INVALID_SOURCE_ID);
}

TEST(test_span_point) {
    Span s = Span::point(0, 10);
    assert(s.valid());
    assert(s.start_offset == 10);
    assert(s.end_offset == 11);
    assert(s.length() == 1);
    assert(s.contains(10));
    assert(!s.contains(11));
}

TEST(test_span_range) {
    Span s = Span::range(1, 5, 15);
    assert(s.valid());
    assert(s.length() == 10);
    assert(s.contains(5));
    assert(s.contains(14));
    assert(!s.contains(15));
}

TEST(test_span_merge) {
    Span a = Span::range(0, 10, 20);
    Span b = Span::range(0, 15, 30);
    Span merged = a.merge(b);
    
    assert(merged.valid());
    assert(merged.start_offset == 10);
    assert(merged.end_offset == 30);
}

TEST(test_span_merge_different_sources) {
    Span a = Span::range(0, 10, 20);
    Span b = Span::range(1, 15, 30);
    Span merged = a.merge(b);
    
    assert(!merged.valid());
}

TEST(test_load_from_string) {
    SourceManager sm;
    
    std::string content = "fn main() {\n    return 0;\n}\n";
    SourceID id = sm.load_from_string("test.zero", content);
    
    assert(id != INVALID_SOURCE_ID);
    assert(sm.file_count() == 1);
    
    const SourceFile* sf = sm.get(id);
    assert(sf != nullptr);
    assert(sf->path == "test.zero");
    assert(sf->content == content);
}

TEST(test_line_offsets) {
    SourceManager sm;
    
    std::string content = "line1\nline2\nline3\n";
    SourceID id = sm.load_from_string("test.zero", content);
    
    const SourceFile* sf = sm.get(id);
    assert(sf != nullptr);
    assert(sf->line_count() == 4);  // 3 lines + trailing newline creates 4th entry
    assert(sf->line_offsets[0] == 0);   // "line1" starts at 0
    assert(sf->line_offsets[1] == 6);   // "line2" starts at 6
    assert(sf->line_offsets[2] == 12);  // "line3" starts at 12
}

TEST(test_offset_to_line_col) {
    SourceManager sm;
    
    std::string content = "abc\ndef\nghi\n";
    SourceID id = sm.load_from_string("test.zero", content);
    
    const SourceFile* sf = sm.get(id);
    assert(sf != nullptr);
    
    // 'a' is at offset 0 -> line 1, col 1
    auto [l1, c1] = sf->offset_to_line_col(0);
    assert(l1 == 1 && c1 == 1);
    
    // 'c' is at offset 2 -> line 1, col 3
    auto [l2, c2] = sf->offset_to_line_col(2);
    assert(l2 == 1 && c2 == 3);
    
    // 'd' is at offset 4 -> line 2, col 1
    auto [l3, c3] = sf->offset_to_line_col(4);
    assert(l3 == 2 && c3 == 1);
    
    // 'i' is at offset 10 -> line 3, col 3
    auto [l4, c4] = sf->offset_to_line_col(10);
    assert(l4 == 3 && c4 == 3);
}

TEST(test_get_line) {
    SourceManager sm;
    
    std::string content = "first line\nsecond line\nthird line\n";
    SourceID id = sm.load_from_string("test.zero", content);
    
    const SourceFile* sf = sm.get(id);
    assert(sf != nullptr);
    
    assert(sf->get_line(1) == "first line");
    assert(sf->get_line(2) == "second line");
    assert(sf->get_line(3) == "third line");
    assert(sf->get_line(0) == "");  // Invalid line
    assert(sf->get_line(100) == "");  // Out of bounds
}

TEST(test_get_text) {
    SourceManager sm;
    
    std::string content = "hello world";
    SourceID id = sm.load_from_string("test.zero", content);
    
    Span span = Span::range(id, 0, 5);
    std::string_view text = sm.get_text(span);
    
    assert(text == "hello");
}

TEST(test_get_path) {
    SourceManager sm;
    
    SourceID id = sm.load_from_string("myfile.zero", "content");
    
    assert(sm.get_path(id) == "myfile.zero");
    assert(sm.get_path(INVALID_SOURCE_ID) == "");
}

TEST(test_invalid_file_load) {
    SourceManager sm;
    
    SourceID id = sm.load("nonexistent_file_12345.zero");
    
    assert(id == INVALID_SOURCE_ID);
    assert(sm.get(id) == nullptr);
}

TEST(test_multiple_files) {
    SourceManager sm;
    
    SourceID id1 = sm.load_from_string("file1.zero", "content1");
    SourceID id2 = sm.load_from_string("file2.zero", "content2");
    SourceID id3 = sm.load_from_string("file3.zero", "content3");
    
    assert(sm.file_count() == 3);
    assert(id1 == 0);
    assert(id2 == 1);
    assert(id3 == 2);
    
    assert(sm.get_path(id1) == "file1.zero");
    assert(sm.get_path(id2) == "file2.zero");
    assert(sm.get_path(id3) == "file3.zero");
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "  Zero Source Management Tests\n";
    std::cout << "============================================\n\n";
    
    return run_all_tests();
}
