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
    IRBuilder(Function& fn) : fn_(fn), current_block_(&fn.entry()) {}
    
    // ─────────────────────────────────────────────────────────────────────
    // Block management
    // ─────────────────────────────────────────────────────────────────────
    
    void set_insert_point(BasicBlock& bb) {
        current_block_ = &bb;
    }
    
    BasicBlock& current_block() { return *current_block_; }
    
    BasicBlock& create_block(const std::string& label = "") {
        return fn_.new_block(label);
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
    
    void br(BasicBlock& target) {
        Instruction instr;
        instr.op = OpCode::BR;
        instr.target_block = target.id;
        emit(instr);
    }
    
    void cond_br(Value cond, BasicBlock& then_bb, BasicBlock& else_bb) {
        Instruction instr;
        instr.op = OpCode::COND_BR;
        instr.operands = {cond};
        instr.target_block = then_bb.id;
        instr.else_block = else_bb.id;
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

private:
    Function& fn_;
    BasicBlock* current_block_;
    
    void emit(Instruction instr) {
        current_block_->add(std::move(instr));
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
};

} // namespace ir
} // namespace zero

#endif // ZERO_IR_BUILDER_HPP
