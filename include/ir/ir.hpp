#ifndef ZERO_IR_IR_HPP
#define ZERO_IR_IR_HPP

/**
 * @file ir.hpp
 * @brief Zero Compiler — Intermediate Representation
 * 
 * SSA-based IR for Zero programs.
 */

#include "types/types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace zero {
namespace ir {

// ─────────────────────────────────────────────────────────────────────────────
// Value (SSA)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * An SSA value. Each value has a unique ID within a function.
 */
struct Value {
    uint32_t id = 0;
    types::Type type;
    
    bool valid() const { return id != 0; }
    
    bool operator==(const Value& o) const { return id == o.id; }
    bool operator!=(const Value& o) const { return id != o.id; }
};

// ─────────────────────────────────────────────────────────────────────────────
// OpCodes
// ─────────────────────────────────────────────────────────────────────────────

enum class OpCode {
    // No-op / placeholder
    NOP,
    
    // Constants
    CONST_INT,      // result = constant int
    CONST_FLOAT,    // result = constant float
    
    // Arithmetic
    ADD,            // result = op0 + op1
    SUB,            // result = op0 - op1
    MUL,            // result = op0 * op1
    DIV,            // result = op0 / op1
    NEG,            // result = -op0
    
    // Comparison
    CMP_EQ,         // result = op0 == op1
    CMP_NE,         // result = op0 != op1
    CMP_LT,         // result = op0 < op1
    CMP_LE,         // result = op0 <= op1
    CMP_GT,         // result = op0 > op1
    CMP_GE,         // result = op0 >= op1
    
    // Control flow
    CALL,           // result = call func(args...)
    RET,            // return op0 (or void)
    BR,             // unconditional branch to block
    COND_BR,        // conditional branch: if op0 then block1 else block2
    
    // Memory (for variables)
    ALLOCA,         // result = stack allocation
    LOAD,           // result = *op0
    STORE,          // *op0 = op1
    
    // Tensor operations (link to core-runtime)
    TENSOR_ALLOC,   // result = allocate tensor
    TENSOR_ADD,     // result = tensor_add(op0, op1)
    TENSOR_SUB,     // result = tensor_sub(op0, op1)
    TENSOR_MUL,     // result = tensor_mul(op0, op1)
    TENSOR_MATMUL,  // result = tensor_matmul(op0, op1)
    TENSOR_RELU,    // result = tensor_relu(op0)
};

inline const char* opcode_name(OpCode op) {
    switch (op) {
        case OpCode::NOP: return "nop";
        case OpCode::CONST_INT: return "const.i64";
        case OpCode::CONST_FLOAT: return "const.f32";
        case OpCode::ADD: return "add";
        case OpCode::SUB: return "sub";
        case OpCode::MUL: return "mul";
        case OpCode::DIV: return "div";
        case OpCode::NEG: return "neg";
        case OpCode::CMP_EQ: return "eq";
        case OpCode::CMP_NE: return "ne";
        case OpCode::CMP_LT: return "lt";
        case OpCode::CMP_LE: return "le";
        case OpCode::CMP_GT: return "gt";
        case OpCode::CMP_GE: return "ge";
        case OpCode::CALL: return "call";
        case OpCode::RET: return "ret";
        case OpCode::BR: return "br";
        case OpCode::COND_BR: return "cond_br";
        case OpCode::ALLOCA: return "alloca";
        case OpCode::LOAD: return "load";
        case OpCode::STORE: return "store";
        case OpCode::TENSOR_ALLOC: return "tensor.alloc";
        case OpCode::TENSOR_ADD: return "tensor.add";
        case OpCode::TENSOR_SUB: return "tensor.sub";
        case OpCode::TENSOR_MUL: return "tensor.mul";
        case OpCode::TENSOR_MATMUL: return "tensor.matmul";
        case OpCode::TENSOR_RELU: return "tensor.relu";
        default: return "unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Instruction
// ─────────────────────────────────────────────────────────────────────────────

/**
 * An IR instruction.
 */
struct Instruction {
    OpCode op = OpCode::NOP;
    Value result;                    // Result value (if any)
    std::vector<Value> operands;     // Operand values
    
    // For constants
    int64_t imm_int = 0;
    double imm_float = 0.0;
    
    // For calls
    std::string callee;
    
    // For branches
    uint32_t target_block = 0;       // For BR
    uint32_t else_block = 0;         // For COND_BR
};

// ─────────────────────────────────────────────────────────────────────────────
// BasicBlock
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A basic block containing a sequence of instructions.
 */
struct BasicBlock {
    uint32_t id = 0;
    std::string label;
    std::vector<Instruction> instrs;
    
    void add(Instruction instr) {
        instrs.push_back(std::move(instr));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Function
// ─────────────────────────────────────────────────────────────────────────────

/**
 * An IR function.
 */
struct Function {
    std::string name;
    std::vector<types::Type> param_types;
    types::Type return_type;
    std::vector<BasicBlock> blocks;
    
    // SSA value counter
    uint32_t next_value_id = 1;
    uint32_t next_block_id = 0;
    
    /**
     * Create a new SSA value.
     */
    Value new_value(types::Type type) {
        return Value{next_value_id++, type};
    }
    
    /**
     * Create a new basic block.
     */
    BasicBlock& new_block(const std::string& label = "") {
        BasicBlock bb;
        bb.id = next_block_id++;
        bb.label = label.empty() ? ("bb" + std::to_string(bb.id)) : label;
        blocks.push_back(std::move(bb));
        return blocks.back();
    }
    
    /**
     * Get entry block.
     */
    BasicBlock& entry() {
        if (blocks.empty()) {
            new_block("entry");
        }
        return blocks[0];
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Module
// ─────────────────────────────────────────────────────────────────────────────

/**
 * An IR module containing functions.
 */
struct Module {
    std::vector<Function> functions;
    
    Function& add_function(const std::string& name, 
                           const std::vector<types::Type>& params,
                           types::Type ret) {
        Function fn;
        fn.name = name;
        fn.param_types = params;
        fn.return_type = ret;
        functions.push_back(std::move(fn));
        return functions.back();
    }
    
    Function* get_function(const std::string& name) {
        for (auto& fn : functions) {
            if (fn.name == name) return &fn;
        }
        return nullptr;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// IR Printer (for debugging)
// ─────────────────────────────────────────────────────────────────────────────

std::string print_value(const Value& v);
std::string print_instruction(const Instruction& instr);
std::string print_block(const BasicBlock& bb);
std::string print_function(const Function& fn);
std::string print_module(const Module& mod);

} // namespace ir
} // namespace zero

#endif // ZERO_IR_IR_HPP
