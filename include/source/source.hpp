#ifndef ZERO_SOURCE_SOURCE_HPP
#define ZERO_SOURCE_SOURCE_HPP

/**
 * @file source.hpp
 * @brief Zero Compiler — Source Management
 * 
 * Provides SourceID, Span, SourceFile, and SourceManager for tracking
 * source code locations throughout the compilation pipeline.
 */

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>

namespace zero {
namespace source {

// ─────────────────────────────────────────────────────────────────────────────
// SourceID
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Lightweight identifier for source files.
 * Index into SourceManager's internal file list.
 */
using SourceID = uint32_t;
constexpr SourceID INVALID_SOURCE_ID = UINT32_MAX;

// ─────────────────────────────────────────────────────────────────────────────
// Span
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A span in source code representing a range [start, end).
 * Uses byte offsets for O(1) operations.
 */
struct Span {
    SourceID source_id = INVALID_SOURCE_ID;
    uint32_t start_offset = 0;  // Byte offset from file start (inclusive)
    uint32_t end_offset = 0;    // Byte offset from file start (exclusive)
    
    /**
     * Create an invalid/empty span
     */
    static Span invalid() {
        return Span{INVALID_SOURCE_ID, 0, 0};
    }
    
    /**
     * Create a span for a single position
     */
    static Span point(SourceID id, uint32_t offset) {
        return Span{id, offset, offset + 1};
    }
    
    /**
     * Create a span for a range
     */
    static Span range(SourceID id, uint32_t start, uint32_t end) {
        return Span{id, start, end};
    }
    
    /**
     * Check if span is valid
     */
    bool valid() const {
        return source_id != INVALID_SOURCE_ID && start_offset <= end_offset;
    }
    
    /**
     * Check if offset is within this span
     */
    bool contains(uint32_t offset) const {
        return offset >= start_offset && offset < end_offset;
    }
    
    /**
     * Get length in bytes
     */
    uint32_t length() const {
        return end_offset - start_offset;
    }
    
    /**
     * Merge two spans (union). Both must be from same source.
     * Returns invalid span if sources differ.
     */
    Span merge(const Span& other) const {
        if (source_id != other.source_id) {
            return invalid();
        }
        return Span{
            source_id,
            start_offset < other.start_offset ? start_offset : other.start_offset,
            end_offset > other.end_offset ? end_offset : other.end_offset
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SourceFile
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A loaded source file with content and line offset table.
 */
struct SourceFile {
    std::string path;                     // File path
    std::string content;                  // File contents
    std::vector<uint32_t> line_offsets;   // Byte offset of each line start
    
    /**
     * Convert byte offset to (line, column) pair.
     * Both line and column are 1-indexed.
     * Returns (0, 0) if offset is out of bounds.
     */
    std::pair<uint32_t, uint32_t> offset_to_line_col(uint32_t offset) const;
    
    /**
     * Get the content of a specific line (1-indexed).
     * Returns empty string if line is out of bounds.
     */
    std::string get_line(uint32_t line_number) const;
    
    /**
     * Get total number of lines
     */
    uint32_t line_count() const {
        return static_cast<uint32_t>(line_offsets.size());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SourceManager
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Central manager for all source files.
 * Owns loaded files and provides lookup utilities.
 */
class SourceManager {
public:
    SourceManager() = default;
    ~SourceManager() = default;
    
    // Non-copyable, movable
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(const SourceManager&) = delete;
    SourceManager(SourceManager&&) = default;
    SourceManager& operator=(SourceManager&&) = default;
    
    /**
     * Load a source file from disk.
     * @param path File path to load
     * @return SourceID for the file, or INVALID_SOURCE_ID on failure
     */
    SourceID load(const std::string& path);
    
    /**
     * Load source from a string (for testing or REPL).
     * @param name Virtual filename
     * @param content Source content
     * @return SourceID for the virtual file
     */
    SourceID load_from_string(const std::string& name, const std::string& content);
    
    /**
     * Get source file by ID.
     * @return Pointer to SourceFile, or nullptr if ID is invalid
     */
    const SourceFile* get(SourceID id) const;
    
    /**
     * Get (line, column) for start of a span.
     * @return (line, column) 1-indexed, or (0, 0) on error
     */
    std::pair<uint32_t, uint32_t> get_line_col(const Span& span) const;
    
    /**
     * Get source text for a span.
     * @return View into source content, or empty view on error
     */
    std::string_view get_text(const Span& span) const;
    
    /**
     * Get filename for a source ID.
     * @return File path, or empty string on error
     */
    const std::string& get_path(SourceID id) const;
    
    /**
     * Get number of loaded files
     */
    size_t file_count() const {
        return files_.size();
    }

private:
    std::vector<SourceFile> files_;
    static const std::string empty_string_;
    
    /**
     * Compute line offsets for loaded content
     */
    static std::vector<uint32_t> compute_line_offsets(const std::string& content);
};

} // namespace source
} // namespace zero

#endif // ZERO_SOURCE_SOURCE_HPP
