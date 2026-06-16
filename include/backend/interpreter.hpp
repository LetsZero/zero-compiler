#ifndef ZERO_BACKEND_INTERPRETER_HPP
#define ZERO_BACKEND_INTERPRETER_HPP

/**
 * @file interpreter.hpp
 * @brief Zero Compiler — ZIR Interpreter (CPU Backend)
 * 
 * Executes ZIR instructions on CPU.
 */

#include "ir/ir.hpp"
#include "types/types.hpp"
#include "source/source.hpp"            // spec 010: span -> file:line:col
#include "diagnostics/reporter.hpp"     // spec 010: rich runtime diagnostics

// Spec 003: include only what we need from the runtime. The umbrella
// <zero/zero.hpp> would pull in zero::ir::Function / zero::ir::BasicBlock
// from the runtime's own IR layer and collide with this compiler's
// zero::ir:: types.
#include <zero/core/tensor.hpp>
#include <zero/core/status.hpp>

#include <unordered_map>
#include <variant>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace zero {
namespace backend {

// ─────────────────────────────────────────────────────────────────────────────
// Runtime Value
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A runtime value during interpretation.
 *
 * Spec 003: tensors are stored as std::shared_ptr<zero::Tensor> with a deleter
 * that respects owns_data, so the runtime's caller-allocated contract is
 * preserved even as RuntimeValue copies flow through the interpreter.
 */
using TensorPtr = std::shared_ptr<zero::Tensor>;

// A struct value is just an ordered list of field values (interpreter-side
// representation; the runtime's StructLayout/StructData is reserved for the
// future codegen backend). shared_ptr keeps copies cheap and recursion-safe.
struct StructVal;
using StructPtr = std::shared_ptr<StructVal>;

// A tensor-array is a runtime-indexable, mutable vector of tensors. shared_ptr
// gives it reference semantics: passing it into a Zero function and mutating it
// there (ta_set) is visible to the caller — exactly what an autograd tape/pool
// needs. Slots start null; ta_set fills them.
using TensorArrayPtr = std::shared_ptr<std::vector<TensorPtr>>;

struct RuntimeValue {
    std::variant<std::monostate, int64_t, double, void*, std::string, TensorPtr, StructPtr, TensorArrayPtr> data;

    RuntimeValue() : data(std::monostate{}) {}
    explicit RuntimeValue(int64_t v) : data(v) {}
    explicit RuntimeValue(double v) : data(v) {}
    explicit RuntimeValue(void* v) : data(v) {}
    explicit RuntimeValue(const std::string& v) : data(v) {}
    explicit RuntimeValue(TensorPtr t) : data(std::move(t)) {}
    explicit RuntimeValue(StructPtr s) : data(std::move(s)) {}
    explicit RuntimeValue(TensorArrayPtr a) : data(std::move(a)) {}

    bool is_void() const { return std::holds_alternative<std::monostate>(data); }
    bool is_int() const { return std::holds_alternative<int64_t>(data); }
    bool is_float() const { return std::holds_alternative<double>(data); }
    bool is_ptr() const { return std::holds_alternative<void*>(data); }
    bool is_str() const { return std::holds_alternative<std::string>(data); }
    bool is_tensor() const { return std::holds_alternative<TensorPtr>(data); }
    bool is_struct() const { return std::holds_alternative<StructPtr>(data); }
    bool is_tensor_array() const { return std::holds_alternative<TensorArrayPtr>(data); }

    int64_t as_int() const { return std::get<int64_t>(data); }
    double as_float() const { return std::get<double>(data); }
    void* as_ptr() const { return std::get<void*>(data); }
    const std::string& as_str() const { return std::get<std::string>(data); }
    const TensorPtr& as_tensor() const { return std::get<TensorPtr>(data); }
    const StructPtr& as_struct() const { return std::get<StructPtr>(data); }
    const TensorArrayPtr& as_tensor_array() const { return std::get<TensorArrayPtr>(data); }
    
    // Convert to int for comparisons
    int64_t to_int() const {
        if (is_int()) return as_int();
        if (is_float()) return static_cast<int64_t>(as_float());
        return 0;
    }
    
    double to_float() const {
        if (is_float()) return as_float();
        if (is_int()) return static_cast<double>(as_int());
        return 0.0;
    }
};

// Defined after RuntimeValue (it holds field values). Field order matches the
// struct's declaration order; `names` is kept for debug printing only.
struct StructVal {
    std::vector<RuntimeValue> fields;
    std::vector<std::string> names;
};

// ─────────────────────────────────────────────────────────────────────────────
// Interpreter
// ─────────────────────────────────────────────────────────────────────────────

/**
 * ZIR Interpreter - executes IR on CPU.
 * 
 * Usage:
 *   Interpreter interp;
 *   RuntimeValue result = interp.execute(module);
 */
class Interpreter {
public:
    using ExternalFn = std::function<RuntimeValue(const std::vector<RuntimeValue>&)>;
    
    Interpreter() = default;
    
    /**
     * Execute a module, starting from the specified entry function.
     */
    RuntimeValue execute(ir::Module& mod, const std::string& entry = "main");
    
    /**
     * Spec 010: provide a SourceManager so runtime errors can be rendered
     * as "Frame & Focus" diagnostics with source-line context. Optional —
     * when unset, runtime errors only throw (no rich rendering).
     */
    void set_source_manager(const source::SourceManager* sm) { sm_ = sm; }

    /**
     * Register an external function (for FFI).
     */
    void register_external(const std::string& name, ExternalFn fn) {
        externals_[name] = fn;
    }
    
    /**
     * Get exit code (from main's return value).
     */
    int exit_code() const { return exit_code_; }

private:
    // Module being executed
    ir::Module* module_ = nullptr;

    // Optional source manager for rich runtime diagnostics (spec 010).
    // Null by default — runtime errors then only throw.
    const source::SourceManager* sm_ = nullptr;

    // External functions
    std::unordered_map<std::string, ExternalFn> externals_;
    
    // Call stack for functions. SSA value storage lives on each frame's
    // `locals` map — spec 006 migrated away from a global value map so
    // function calls don't have id collisions across frames.
    struct CallFrame {
        const ir::Function* fn;
        size_t block_idx;
        size_t instr_idx;
        std::unordered_map<uint32_t, RuntimeValue> locals;
        // Memory cells for mutable variables (alloca/load/store), keyed by the
        // ALLOCA result's SSA id. Per-frame, so recursion is isolated. A cell
        // outlives individual basic blocks (it lives for the whole call),
        // which is what lets a reassignment be visible to a later loop check.
        std::unordered_map<uint32_t, RuntimeValue> memory;
    };
    std::vector<CallFrame> call_stack_;

    // Maximum interpreter call depth. The interpreter recurses on the native
    // C++ stack per Zero call, so this bounds recursion below the point where
    // the real stack would overflow (turning a SIGSEGV into a diagnostic).
    // Measured native overflow is ~1500-2000 frames on an 8 MB stack; 512
    // leaves a wide margin below that (frame sizes vary by build/platform)
    // while staying well above legitimate nesting (deepest existing test ~200).
    static constexpr size_t kMaxCallDepth = 512;

    // Exit code
    int exit_code_ = 0;

    // ─────────────────────────────────────────────────────────────────────
    // Execution
    // ─────────────────────────────────────────────────────────────────────

    RuntimeValue call_function(const ir::Function& fn,
                                std::vector<RuntimeValue> args);
    RuntimeValue exec_block(const ir::BasicBlock& bb);
    RuntimeValue exec_instruction(const ir::Instruction& instr);

    // ─────────────────────────────────────────────────────────────────────
    // Value access — read/write the active frame's locals (spec 006).
    // ─────────────────────────────────────────────────────────────────────

    RuntimeValue get_value(const ir::Value& v) {
        if (call_stack_.empty()) return RuntimeValue{};
        auto& locals = call_stack_.back().locals;
        auto it = locals.find(v.id);
        if (it != locals.end()) return it->second;
        return RuntimeValue{};
    }

    void set_value(const ir::Value& v, RuntimeValue rv) {
        if (call_stack_.empty()) return;
        call_stack_.back().locals[v.id] = std::move(rv);
    }
};

} // namespace backend
} // namespace zero

#endif // ZERO_BACKEND_INTERPRETER_HPP
