#ifndef ZERO_IR_BUILDER_HPP
#define ZERO_IR_BUILDER_HPP

/**
 * @file builder.hpp
 * @brief Zero Compiler — IR Builder
 * 
 * Helper class for constructing IR.
 */

#include "ir/ir.hpp"
#include "ast/ast.hpp"

namespace zero {
namespace ir {

/**
 * IRBuilder - Helper for constructing IR instructions.
 */
class IRBuilder {
public:
    IRBuilder(Function& fn) : fn_(fn), current_block_id_(fn.entry().id) {}

    // ─────────────────────────────────────────────────────────────────────
    // Block management
    //
    // Spec 009: blocks are addressed by id (== index into fn.blocks), never
    // by pointer/reference. Holding a BasicBlock* or BasicBlock& across a
    // create_block() call is a use-after-realloc bug — that is exactly what
    // this refactor removes.
    // ─────────────────────────────────────────────────────────────────────

    void set_insert_point(uint32_t block_id) {
        current_block_id_ = block_id;
    }

    uint32_t current_block_id() const { return current_block_id_; }

    // ─────────────────────────────────────────────────────────────────────
    // Source span attribution (spec 002)
    //
    // The current span is stamped onto every Instruction emitted via emit().
    // The lowering pass calls set_current_span(node.span) on entry to each
    // AST visitor; child visitors overwrite the value as they descend.
    // ─────────────────────────────────────────────────────────────────────

    void set_current_span(source::Span s) noexcept { current_span_ = s; }
    source::Span current_span() const noexcept { return current_span_; }
    
    uint32_t create_block(const std::string& label = "") {
        return fn_.new_block(label).id;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Constants
    // ─────────────────────────────────────────────────────────────────────
    
    Value const_int(int64_t value) {
        Instruction instr;
        instr.op = OpCode::CONST_INT;
        instr.result = fn_.new_value(types::Type::make_int());
        instr.imm_int = value;
        emit(instr);
        return instr.result;
    }
    
    Value const_float(double value) {
        Instruction instr;
        instr.op = OpCode::CONST_FLOAT;
        instr.result = fn_.new_value(types::Type::make_float());
        instr.imm_float = value;
        emit(instr);
        return instr.result;
    }
    
    Value const_str(const std::string& value) {
        Instruction instr;
        instr.op = OpCode::CONST_STR;
        instr.result = fn_.new_value(types::Type::make_unknown()); // String type
        instr.imm_str = value;
        emit(instr);
        return instr.result;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Arithmetic
    // ─────────────────────────────────────────────────────────────────────
    
    Value add(Value lhs, Value rhs) {
        return binary_op(OpCode::ADD, lhs, rhs);
    }
    
    Value sub(Value lhs, Value rhs) {
        return binary_op(OpCode::SUB, lhs, rhs);
    }
    
    Value mul(Value lhs, Value rhs) {
        return binary_op(OpCode::MUL, lhs, rhs);
    }
    
    Value div(Value lhs, Value rhs) {
        return binary_op(OpCode::DIV, lhs, rhs);
    }
    
    Value neg(Value operand) {
        Instruction instr;
        instr.op = OpCode::NEG;
        instr.result = fn_.new_value(operand.type);
        instr.operands = {operand};
        emit(instr);
        return instr.result;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Comparison
    // ─────────────────────────────────────────────────────────────────────
    
    Value cmp_eq(Value lhs, Value rhs) { return cmp(OpCode::CMP_EQ, lhs, rhs); }
    Value cmp_ne(Value lhs, Value rhs) { return cmp(OpCode::CMP_NE, lhs, rhs); }
    Value cmp_lt(Value lhs, Value rhs) { return cmp(OpCode::CMP_LT, lhs, rhs); }
    Value cmp_le(Value lhs, Value rhs) { return cmp(OpCode::CMP_LE, lhs, rhs); }
    Value cmp_gt(Value lhs, Value rhs) { return cmp(OpCode::CMP_GT, lhs, rhs); }
    Value cmp_ge(Value lhs, Value rhs) { return cmp(OpCode::CMP_GE, lhs, rhs); }
    
    // ─────────────────────────────────────────────────────────────────────
    // Control flow
    // ─────────────────────────────────────────────────────────────────────
    
    void ret() {
        Instruction instr;
        instr.op = OpCode::RET;
        emit(instr);
    }
    
    void ret(Value value) {
        Instruction instr;
        instr.op = OpCode::RET;
        instr.operands = {value};
        emit(instr);
    }
    
    void br(uint32_t target_id) {
        Instruction instr;
        instr.op = OpCode::BR;
        instr.target_block = target_id;
        emit(instr);
    }

    void cond_br(Value cond, uint32_t then_id, uint32_t else_id) {
        Instruction instr;
        instr.op = OpCode::COND_BR;
        instr.operands = {cond};
        instr.target_block = then_id;
        instr.else_block = else_id;
        emit(instr);
    }
    
    Value call(const std::string& callee, const std::vector<Value>& args, 
               types::Type ret_type) {
        Instruction instr;
        instr.op = OpCode::CALL;
        instr.callee = callee;
        instr.operands = args;
        if (!ret_type.is_void()) {
            instr.result = fn_.new_value(ret_type);
        }
        emit(instr);
        return instr.result;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // Memory
    // ─────────────────────────────────────────────────────────────────────
    
    Value alloca(types::Type type) {
        Instruction instr;
        instr.op = OpCode::ALLOCA;
        instr.result = fn_.new_value(type);
        emit(instr);
        return instr.result;
    }
    
    Value load(Value ptr) {
        Instruction instr;
        instr.op = OpCode::LOAD;
        instr.result = fn_.new_value(ptr.type);
        instr.operands = {ptr};
        emit(instr);
        return instr.result;
    }
    
    void store(Value ptr, Value value) {
        Instruction instr;
        instr.op = OpCode::STORE;
        instr.operands = {ptr, value};
        emit(instr);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Tensor ops (spec 003)
    // ─────────────────────────────────────────────────────────────────────

    /**
     * @brief Emit a TENSOR_CONST_F32 — build a fresh contiguous F32 tensor
     * with the given shape and inline values. Helper for tests; production
     * lowering will not emit this opcode.
     */
    Value tensor_const_f32(std::vector<int64_t> shape, std::vector<float> data) {
        Instruction instr;
        instr.op = OpCode::TENSOR_CONST_F32;
        instr.result = fn_.new_value(types::Type::make_tensor());
        instr.imm_shape = std::move(shape);
        instr.imm_floats = std::move(data);
        emit(instr);
        return instr.result;
    }

    /**
     * @brief Emit a TENSOR_ADD — wires to zero::ops::add in the interpreter.
     * Output shape is taken from the LHS operand (matches the runtime's
     * binary op contract).
     */
    Value tensor_add(Value lhs, Value rhs) {
        Instruction instr;
        instr.op = OpCode::TENSOR_ADD;
        instr.result = fn_.new_value(types::Type::make_tensor());
        instr.operands = {lhs, rhs};
        emit(instr);
        return instr.result;
    }

    // Spec 005: the rest of the binary/unary tensor ops. Same shape and
    // semantics as tensor_add — output type is tensor, operands are the
    // tensor values, the interpreter does the runtime call.
    Value tensor_sub(Value lhs, Value rhs) { return tensor_binary(OpCode::TENSOR_SUB, lhs, rhs); }
    Value tensor_mul(Value lhs, Value rhs) { return tensor_binary(OpCode::TENSOR_MUL, lhs, rhs); }
    Value tensor_div(Value lhs, Value rhs) { return tensor_binary(OpCode::TENSOR_DIV, lhs, rhs); }
    Value tensor_neg(Value x)              { return tensor_unary(OpCode::TENSOR_NEG, x); }
    Value tensor_relu(Value x)             { return tensor_unary(OpCode::TENSOR_RELU, x); }
    Value tensor_matmul(Value lhs, Value rhs) { return tensor_binary(OpCode::TENSOR_MATMUL, lhs, rhs); }

    // Spec 014: full reductions (unary, [1] F32 result).
    Value tensor_sum(Value x)    { return tensor_unary(OpCode::TENSOR_SUM, x); }
    Value tensor_mean(Value x)   { return tensor_unary(OpCode::TENSOR_MEAN, x); }
    Value tensor_argmax(Value x) { return tensor_unary(OpCode::TENSOR_ARGMAX, x); }

    // Spec 016: shape-preserving unary elementwise math.
    Value tensor_exp(Value x)     { return tensor_unary(OpCode::TENSOR_EXP, x); }
    Value tensor_log(Value x)     { return tensor_unary(OpCode::TENSOR_LOG, x); }
    Value tensor_sqrt(Value x)    { return tensor_unary(OpCode::TENSOR_SQRT, x); }
    Value tensor_tanh(Value x)    { return tensor_unary(OpCode::TENSOR_TANH, x); }
    Value tensor_sigmoid(Value x) { return tensor_unary(OpCode::TENSOR_SIGMOID, x); }

    // 2-D transpose (permute [1,0] + contiguous, done in the interpreter).
    Value tensor_transpose(Value x) { return tensor_unary(OpCode::TENSOR_TRANSPOSE, x); }

    // Heaviside step (relu's derivative): 1.0 where x > 0, else 0.0.
    Value tensor_step(Value x) { return tensor_unary(OpCode::TENSOR_STEP, x); }

    // ─────────────────────────────────────────────────────────────────────
    // Structs
    // ─────────────────────────────────────────────────────────────────────

    // Build a struct value from field values (in declaration order).
    Value struct_new(const std::vector<Value>& fields, types::Type struct_type) {
        Instruction instr;
        instr.op = OpCode::STRUCT_NEW;
        instr.result = fn_.new_value(struct_type);
        instr.operands = fields;
        emit(instr);
        return instr.result;
    }

    // Extract field #index from a struct value.
    Value struct_get(Value object, int64_t index, types::Type field_type) {
        Instruction instr;
        instr.op = OpCode::STRUCT_GET;
        instr.result = fn_.new_value(field_type);
        instr.operands = {object};
        instr.imm_int = index;
        emit(instr);
        return instr.result;
    }

    // Element-level tensor indexing (flat).
    Value tensor_index_get(Value tensor_v, Value index_v) {
        Instruction instr;
        instr.op = OpCode::TENSOR_INDEX_GET;
        instr.result = fn_.new_value(types::Type::make_float());
        instr.operands = {tensor_v, index_v};
        emit(instr);
        return instr.result;
    }

    void tensor_index_set(Value tensor_v, Value index_v, Value value_v) {
        Instruction instr;
        instr.op = OpCode::TENSOR_INDEX_SET;
        instr.operands = {tensor_v, index_v, value_v};
        emit(instr);
    }

private:
    Function& fn_;
    uint32_t current_block_id_;
    source::Span current_span_ = source::Span::invalid();

    void emit(Instruction instr) {
        // Spec 002: every Instruction emitted via the builder is stamped with
        // the current span. Lowering sets it; if no one set it, the span is
        // invalid (acceptable for synthesized ops like implicit RET).
        instr.span = current_span_;
        // Spec 009: re-derive the destination block from the stable id every
        // call. Never cache a reference — create_block() may have reallocated
        // fn_.blocks since the last emit.
        fn_.blocks[current_block_id_].add(std::move(instr));
    }
    
    Value binary_op(OpCode op, Value lhs, Value rhs) {
        Instruction instr;
        instr.op = op;
        instr.result = fn_.new_value(types::binary_result_type(lhs.type, rhs.type));
        instr.operands = {lhs, rhs};
        emit(instr);
        return instr.result;
    }
    
    Value cmp(OpCode op, Value lhs, Value rhs) {
        Instruction instr;
        instr.op = op;
        instr.result = fn_.new_value(types::Type::make_int()); // bool as int
        instr.operands = {lhs, rhs};
        emit(instr);
        return instr.result;
    }

    // Spec 005: shared emit paths for the binary and unary tensor ops.
    Value tensor_binary(OpCode op, Value lhs, Value rhs) {
        Instruction instr;
        instr.op = op;
        instr.result = fn_.new_value(types::Type::make_tensor());
        instr.operands = {lhs, rhs};
        emit(instr);
        return instr.result;
    }

    Value tensor_unary(OpCode op, Value x) {
        Instruction instr;
        instr.op = op;
        instr.result = fn_.new_value(types::Type::make_tensor());
        instr.operands = {x};
        emit(instr);
        return instr.result;
    }
};

} // namespace ir
} // namespace zero

#endif // ZERO_IR_BUILDER_HPP
