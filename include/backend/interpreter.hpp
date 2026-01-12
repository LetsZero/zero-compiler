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

#include <unordered_map>
#include <variant>
#include <vector>
#include <string>
#include <functional>

namespace zero {
namespace backend {

// ─────────────────────────────────────────────────────────────────────────────
// Runtime Value
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A runtime value during interpretation.
 */
struct RuntimeValue {
    std::variant<std::monostate, int64_t, double, void*, std::string> data;
    
    RuntimeValue() : data(std::monostate{}) {}
    explicit RuntimeValue(int64_t v) : data(v) {}
    explicit RuntimeValue(double v) : data(v) {}
    explicit RuntimeValue(void* v) : data(v) {}
    explicit RuntimeValue(const std::string& v) : data(v) {}
    
    bool is_void() const { return std::holds_alternative<std::monostate>(data); }
    bool is_int() const { return std::holds_alternative<int64_t>(data); }
    bool is_float() const { return std::holds_alternative<double>(data); }
    bool is_ptr() const { return std::holds_alternative<void*>(data); }
    bool is_str() const { return std::holds_alternative<std::string>(data); }
    
    int64_t as_int() const { return std::get<int64_t>(data); }
    double as_float() const { return std::get<double>(data); }
    void* as_ptr() const { return std::get<void*>(data); }
    const std::string& as_str() const { return std::get<std::string>(data); }
    
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
    
    // External functions
    std::unordered_map<std::string, ExternalFn> externals_;
    
    // Value storage (SSA id -> runtime value)
    std::unordered_map<uint32_t, RuntimeValue> values_;
    
    // Call stack for functions
    struct CallFrame {
        const ir::Function* fn;
        size_t block_idx;
        size_t instr_idx;
        std::unordered_map<uint32_t, RuntimeValue> locals;
    };
    std::vector<CallFrame> call_stack_;
    
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
    // Value access
    // ─────────────────────────────────────────────────────────────────────
    
    RuntimeValue get_value(const ir::Value& v) {
        auto it = values_.find(v.id);
        if (it != values_.end()) return it->second;
        return RuntimeValue{};
    }
    
    void set_value(const ir::Value& v, RuntimeValue rv) {
        values_[v.id] = rv;
    }
};

} // namespace backend
} // namespace zero

#endif // ZERO_BACKEND_INTERPRETER_HPP
