#ifndef ZERO_IR_IR_HPP
#define ZERO_IR_IR_HPP

/**
 * @file ir.hpp
 * @brief Zero Compiler — Intermediate Representation
 * 
 * SSA-based IR for Zero programs.
 */

#include "types/types.hpp"
#include "source/source.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>

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
    CONST_STR,      // result = constant string
    
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
    TENSOR_ADD,     // result = tensor_add(op0, op1)   — wired in spec 003
    TENSOR_SUB,     // result = tensor_sub(op0, op1)
    TENSOR_MUL,     // result = tensor_mul(op0, op1)
    TENSOR_MATMUL,  // result = tensor_matmul(op0, op1)
    TENSOR_RELU,    // result = tensor_relu(op0)

    // Spec 003: build a fresh contiguous F32 tensor from immediate data.
    // Pure helper op for tests; production lowering will not emit it.
    // shape lives in imm_shape, values in imm_floats.
    TENSOR_CONST_F32,

    // Spec 005: more tensor ops wired to the runtime.
    TENSOR_NEG,     // result = -op0
    TENSOR_DIV,     // result = op0 / op1 (elementwise)

    // Spec 014: full reductions over all elements -> [1] F32 tensor.
    TENSOR_SUM,     // result = sum(op0)
    TENSOR_MEAN,    // result = mean(op0)
    TENSOR_ARGMAX,  // result = flat index of max(op0), as float

    // Spec 016: shape-preserving unary elementwise math.
    TENSOR_EXP,     // result = exp(op0)
    TENSOR_LOG,     // result = log(op0)
    TENSOR_SQRT,    // result = sqrt(op0)
    TENSOR_TANH,    // result = tanh(op0)
    TENSOR_SIGMOID, // result = sigmoid(op0)

    // 2-D transpose: permute axes [1,0], then materialize contiguous.
    // Needed to hand-write weight gradients (x^T @ err) for matmul layers.
    TENSOR_TRANSPOSE,

    // Heaviside step: 1.0 where x > 0, else 0.0. This is relu's derivative —
    // needed to hand-write backprop through a relu. Computed in the interpreter
    // (not a frozen-runtime op): activation math beyond the core set is the
    // compiler/stdlib's job, never the runtime's.
    TENSOR_STEP,

    // Aggregates. STRUCT_NEW builds a struct from its operands (field values
    // in declaration order). STRUCT_GET extracts field #imm_int from operand 0.
    STRUCT_NEW,
    STRUCT_GET,

    // Element-level tensor indexing (flat / row-major).
    // TENSOR_INDEX_GET: result = float at op0[op1].
    // TENSOR_INDEX_SET: op0[op1] = op2 (in-place; mutates the tensor buffer).
    TENSOR_INDEX_GET,
    TENSOR_INDEX_SET,
};

inline const char* opcode_name(OpCode op) {
    switch (op) {
        case OpCode::NOP: return "nop";
        case OpCode::CONST_INT: return "const.i64";
        case OpCode::CONST_FLOAT: return "const.f32";
        case OpCode::CONST_STR: return "const.str";
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
        case OpCode::TENSOR_ADD: return "tensor.add";
        case OpCode::TENSOR_SUB: return "tensor.sub";
        case OpCode::TENSOR_MUL: return "tensor.mul";
        case OpCode::TENSOR_MATMUL: return "tensor.matmul";
        case OpCode::TENSOR_RELU: return "tensor.relu";
        case OpCode::TENSOR_CONST_F32: return "tensor.const.f32";
        case OpCode::TENSOR_NEG: return "tensor.neg";
        case OpCode::TENSOR_DIV: return "tensor.div";
        case OpCode::TENSOR_SUM: return "tensor.sum";
        case OpCode::TENSOR_MEAN: return "tensor.mean";
        case OpCode::TENSOR_ARGMAX: return "tensor.argmax";
        case OpCode::TENSOR_EXP: return "tensor.exp";
        case OpCode::TENSOR_LOG: return "tensor.log";
        case OpCode::TENSOR_SQRT: return "tensor.sqrt";
        case OpCode::TENSOR_TANH: return "tensor.tanh";
        case OpCode::TENSOR_SIGMOID: return "tensor.sigmoid";
        case OpCode::TENSOR_TRANSPOSE: return "tensor.transpose";
        case OpCode::TENSOR_STEP: return "tensor.step";
        case OpCode::STRUCT_NEW: return "struct.new";
        case OpCode::STRUCT_GET: return "struct.get";
        case OpCode::TENSOR_INDEX_GET: return "tensor.index.get";
        case OpCode::TENSOR_INDEX_SET: return "tensor.index.set";
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
    std::string imm_str;

    // For calls
    std::string callee;

    // For branches
    uint32_t target_block = 0;       // For BR
    uint32_t else_block = 0;         // For COND_BR

    // Tensor-shape / inline data (spec 003).
    // Used by TENSOR_CONST_F32; ignored by other opcodes in v1.
    std::vector<int64_t> imm_shape;
    std::vector<float>   imm_floats;

    // Source attribution (spec 002).
    // Spec 002 contract: every Instruction emitted by IRBuilder carries the
    // source span of the AST node it was lowered from. Instructions synthesized
    // without a clear source location (e.g., implicit RET) carry Span::invalid().
    source::Span span = source::Span::invalid();
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

    // Parameter SSA values, in declaration order. Populated by the
    // lowering pass (spec 006). params.size() == param_types.size().
    // The interpreter binds incoming call arguments to these Values.
    std::vector<Value> params;

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
     *
     * INVARIANT (load-bearing): a block's `id` always equals its index in
     * `blocks`. This holds because `next_block_id` and `blocks` advance in
     * lockstep from 0 and nothing ever removes a block. Both IRBuilder
     * (spec 009) and the interpreter (`fn.blocks[instr.target_block]`)
     * rely on it. A future block-removal pass must preserve it.
     */
    BasicBlock& new_block(const std::string& label = "") {
        BasicBlock bb;
        bb.id = next_block_id++;
        // id == index invariant: the block we are about to push lands at
        // index `blocks.size()`, which must equal its id.
        assert(bb.id == blocks.size() && "block id must equal its index");
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
