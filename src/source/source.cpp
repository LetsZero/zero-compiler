/**
 * @file source.cpp
 * @brief Zero Compiler — Source Management Implementation
 */

#include "source/source.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>

namespace zero {
namespace source {

// ─────────────────────────────────────────────────────────────────────────────
// Static members
// ─────────────────────────────────────────────────────────────────────────────

const std::string SourceManager::empty_string_;

// ─────────────────────────────────────────────────────────────────────────────
// SourceFile implementation
// ─────────────────────────────────────────────────────────────────────────────

std::pair<uint32_t, uint32_t> SourceFile::offset_to_line_col(uint32_t offset) const {
    if (line_offsets.empty() || offset > content.size()) {
        return {0, 0};
    }
    
    // Binary search for the line containing this offset
    auto it = std::upper_bound(line_offsets.begin(), line_offsets.end(), offset);
    
    // upper_bound returns iterator to first element > offset
    // We need the line before that
    if (it == line_offsets.begin()) {
        // Offset is before first line (shouldn't happen for valid files)
        return {1, offset + 1};
    }
    
    --it;
    uint32_t line = static_cast<uint32_t>(std::distance(line_offsets.begin(), it)) + 1;
    uint32_t column = offset - *it + 1;
    
    return {line, column};
}

std::string SourceFile::get_line(uint32_t line_number) const {
    if (line_number == 0 || line_number > line_offsets.size()) {
        return "";
    }
    
    uint32_t line_index = line_number - 1;
    uint32_t start = line_offsets[line_index];
    uint32_t end;
    
    if (line_index + 1 < line_offsets.size()) {
        end = line_offsets[line_index + 1];
        // Remove trailing newline if present
        if (end > start && content[end - 1] == '\n') {
            --end;
        }
        // Handle CRLF
        if (end > start && content[end - 1] == '\r') {
            --end;
        }
    } else {
        end = static_cast<uint32_t>(content.size());
    }
    
    return content.substr(start, end - start);
}

// ─────────────────────────────────────────────────────────────────────────────
// SourceManager implementation
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint32_t> SourceManager::compute_line_offsets(const std::string& content) {
    std::vector<uint32_t> offsets;
    offsets.push_back(0);  // First line starts at offset 0
    
    for (uint32_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            offsets.push_back(i + 1);
        }
    }
    
    return offsets;
}

SourceID SourceManager::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return INVALID_SOURCE_ID;
    }
    
    // Read entire file content
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Create SourceFile
    SourceFile sf;
    sf.path = path;
    sf.content = std::move(content);
    sf.line_offsets = compute_line_offsets(sf.content);
    
    // Add to files list
    SourceID id = static_cast<SourceID>(files_.size());
    files_.push_back(std::move(sf));
    
    return id;
}

SourceID SourceManager::load_from_string(const std::string& name, const std::string& content) {
    SourceFile sf;
    sf.path = name;
    sf.content = content;
    sf.line_offsets = compute_line_offsets(sf.content);
    
    SourceID id = static_cast<SourceID>(files_.size());
    files_.push_back(std::move(sf));
    
    return id;
}

const SourceFile* SourceManager::get(SourceID id) const {
    if (id == INVALID_SOURCE_ID || id >= files_.size()) {
        return nullptr;
    }
    return &files_[id];
}

std::pair<uint32_t, uint32_t> SourceManager::get_line_col(const Span& span) const {
    const SourceFile* sf = get(span.source_id);
    if (!sf) {
        return {0, 0};
    }
    return sf->offset_to_line_col(span.start_offset);
}

std::string_view SourceManager::get_text(const Span& span) const {
    const SourceFile* sf = get(span.source_id);
    if (!sf || !span.valid()) {
        return {};
    }
    
    if (span.end_offset > sf->content.size()) {
        return {};
    }
    
    return std::string_view(sf->content).substr(span.start_offset, span.length());
}

const std::string& SourceManager::get_path(SourceID id) const {
    const SourceFile* sf = get(id);
    if (!sf) {
        return empty_string_;
    }
    return sf->path;
}

} // namespace source
} // namespace zero
