#ifndef ZERO_TYPES_TYPES_HPP
#define ZERO_TYPES_TYPES_HPP

/**
 * @file types.hpp
 * @brief Zero Compiler — Type System
 * 
 * Minimal type system for MPP: Int, Float, Void, Tensor.
 */

#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace zero {
namespace types {

// ─────────────────────────────────────────────────────────────────────────────
// Type Kinds
// ─────────────────────────────────────────────────────────────────────────────

enum class TypeKind {
    INT,        // i64
    FLOAT,      // f32
    VOID,       // No value
    TENSOR,     // Multi-dimensional array
    FUNCTION,   // Function type (for later)
    UNKNOWN     // Placeholder / unresolved
};

// ─────────────────────────────────────────────────────────────────────────────
// Type
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Type representation.
 * 
 * For MPP, types are simple tags. Tensor shape tracking and 
 * function types can be added later.
 */
struct Type {
    TypeKind kind = TypeKind::UNKNOWN;
    
    // ─────────────────────────────────────────────────────────────────────
    // Constructors
    // ─────────────────────────────────────────────────────────────────────
    
    Type() = default;
    explicit Type(TypeKind k) : kind(k) {}
    
    // ─────────────────────────────────────────────────────────────────────
    // Factory methods
    // ─────────────────────────────────────────────────────────────────────
    
    static Type make_int() { return Type(TypeKind::INT); }
    static Type make_float() { return Type(TypeKind::FLOAT); }
    static Type make_void() { return Type(TypeKind::VOID); }
    static Type make_tensor() { return Type(TypeKind::TENSOR); }
    static Type make_unknown() { return Type(TypeKind::UNKNOWN); }
    
    // ─────────────────────────────────────────────────────────────────────
    // Queries
    // ─────────────────────────────────────────────────────────────────────
    
    bool is_int() const { return kind == TypeKind::INT; }
    bool is_float() const { return kind == TypeKind::FLOAT; }
    bool is_void() const { return kind == TypeKind::VOID; }
    bool is_tensor() const { return kind == TypeKind::TENSOR; }
    bool is_numeric() const { return is_int() || is_float(); }
    bool is_unknown() const { return kind == TypeKind::UNKNOWN; }
    
    // ─────────────────────────────────────────────────────────────────────
    // Equality
    // ─────────────────────────────────────────────────────────────────────
    
    bool operator==(const Type& other) const {
        return kind == other.kind;
    }
    
    bool operator!=(const Type& other) const {
        return !(*this == other);
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Debug
    // ─────────────────────────────────────────────────────────────────────
    
    const char* name() const {
        switch (kind) {
            case TypeKind::INT:     return "int";
            case TypeKind::FLOAT:   return "float";
            case TypeKind::VOID:    return "void";
            case TypeKind::TENSOR:  return "tensor";
            case TypeKind::FUNCTION: return "function";
            case TypeKind::UNKNOWN: return "unknown";
            default:                return "?";
        }
    }
    
    std::string to_string() const {
        return name();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Type utilities
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Check if two types are compatible for assignment.
 * For MPP, this is just equality.
 */
inline bool types_compatible(const Type& a, const Type& b) {
    if (a.is_unknown() || b.is_unknown()) return true;
    return a == b;
}

/**
 * Get the result type of a binary operation.
 * Returns UNKNOWN if types are incompatible.
 */
inline Type binary_result_type(const Type& left, const Type& right) {
    // Unknown propagates
    if (left.is_unknown()) return right;
    if (right.is_unknown()) return left;
    
    // Same types -> same result
    if (left == right) return left;
    
    // Int + Float -> Float (promotion)
    if (left.is_numeric() && right.is_numeric()) {
        if (left.is_float() || right.is_float()) {
            return Type::make_float();
        }
        return Type::make_int();
    }
    
    // Incompatible
    return Type::make_unknown();
}

/**
 * Parse a type name string to Type.
 */
inline Type parse_type(const std::string& name) {
    if (name == "int") return Type::make_int();
    if (name == "float") return Type::make_float();
    if (name == "void") return Type::make_void();
    if (name == "tensor") return Type::make_tensor();
    return Type::make_unknown();
}

} // namespace types
} // namespace zero

#endif // ZERO_TYPES_TYPES_HPP
