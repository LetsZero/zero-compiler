/**
 * @file interpreter.cpp
 * @brief Zero Compiler — ZIR Interpreter Implementation
 */

#include "backend/interpreter.hpp"
#include <iostream>
#include <stdexcept>

namespace zero {
namespace backend {

using namespace ir;

// ─────────────────────────────────────────────────────────────────────────────
// Main execution
// ─────────────────────────────────────────────────────────────────────────────

RuntimeValue Interpreter::execute(Module& mod, const std::string& entry) {
    module_ = &mod;
    values_.clear();
    call_stack_.clear();
    
    // Find entry function
    Function* entry_fn = mod.get_function(entry);
    if (!entry_fn) {
        throw std::runtime_error("Entry function not found: " + entry);
    }
    
    // Call entry function with no arguments
    RuntimeValue result = call_function(*entry_fn, {});
    
    // Set exit code from return value
    if (result.is_int()) {
        exit_code_ = static_cast<int>(result.as_int());
    }
    
    return result;
}

RuntimeValue Interpreter::call_function(const Function& fn, 
                                          std::vector<RuntimeValue> args) {
    // Check for external function
    auto ext_it = externals_.find(fn.name);
    if (ext_it != externals_.end()) {
        return ext_it->second(args);
    }
    
    // Push call frame
    CallFrame frame;
    frame.fn = &fn;
    frame.block_idx = 0;
    frame.instr_idx = 0;
    call_stack_.push_back(frame);
    
    // TODO: Bind arguments to parameter values
    // For now, we don't pass arguments through IR
    
    // Execute blocks
    RuntimeValue result;
    
    while (!call_stack_.empty() && call_stack_.back().fn == &fn) {
        auto& current = call_stack_.back();
        
        if (current.block_idx >= fn.blocks.size()) {
            break;
        }
        
        const BasicBlock& bb = fn.blocks[current.block_idx];
        
        while (current.instr_idx < bb.instrs.size()) {
            const Instruction& instr = bb.instrs[current.instr_idx];
            
            // Check for return
            if (instr.op == OpCode::RET) {
                if (!instr.operands.empty()) {
                    result = get_value(instr.operands[0]);
                }
                call_stack_.pop_back();
                return result;
            }
            
            // Check for branch
            if (instr.op == OpCode::BR) {
                current.block_idx = instr.target_block;
                current.instr_idx = 0;
                break;
            }
            
            if (instr.op == OpCode::COND_BR) {
                RuntimeValue cond = get_value(instr.operands[0]);
                if (cond.to_int() != 0) {
                    current.block_idx = instr.target_block;
                } else {
                    current.block_idx = instr.else_block;
                }
                current.instr_idx = 0;
                break;
            }
            
            // Execute instruction
            result = exec_instruction(instr);
            current.instr_idx++;
        }
        
        // If we finished the block without a branch, move to next
        if (current.instr_idx >= bb.instrs.size() && 
            current.block_idx < fn.blocks.size() - 1) {
            current.block_idx++;
            current.instr_idx = 0;
        } else if (current.instr_idx >= bb.instrs.size()) {
            break;
        }
    }
    
    // Pop frame if still on stack
    if (!call_stack_.empty() && call_stack_.back().fn == &fn) {
        call_stack_.pop_back();
    }
    
    return result;
}

RuntimeValue Interpreter::exec_instruction(const Instruction& instr) {
    RuntimeValue result;
    
    switch (instr.op) {
        case OpCode::NOP:
            break;
            
        case OpCode::CONST_INT:
            result = RuntimeValue(instr.imm_int);
            break;
            
        case OpCode::CONST_FLOAT:
            result = RuntimeValue(instr.imm_float);
            break;
            
        case OpCode::CONST_STR:
            result = RuntimeValue(instr.imm_str);
            break;
            
        case OpCode::ADD: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            if (lhs.is_float() || rhs.is_float()) {
                result = RuntimeValue(lhs.to_float() + rhs.to_float());
            } else {
                result = RuntimeValue(lhs.to_int() + rhs.to_int());
            }
            break;
        }
            
        case OpCode::SUB: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            if (lhs.is_float() || rhs.is_float()) {
                result = RuntimeValue(lhs.to_float() - rhs.to_float());
            } else {
                result = RuntimeValue(lhs.to_int() - rhs.to_int());
            }
            break;
        }
            
        case OpCode::MUL: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            if (lhs.is_float() || rhs.is_float()) {
                result = RuntimeValue(lhs.to_float() * rhs.to_float());
            } else {
                result = RuntimeValue(lhs.to_int() * rhs.to_int());
            }
            break;
        }
            
        case OpCode::DIV: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            if (lhs.is_float() || rhs.is_float()) {
                result = RuntimeValue(lhs.to_float() / rhs.to_float());
            } else {
                int64_t divisor = rhs.to_int();
                result = RuntimeValue(divisor != 0 ? lhs.to_int() / divisor : 0);
            }
            break;
        }
            
        case OpCode::NEG: {
            auto operand = get_value(instr.operands[0]);
            if (operand.is_float()) {
                result = RuntimeValue(-operand.as_float());
            } else {
                result = RuntimeValue(-operand.to_int());
            }
            break;
        }
            
        case OpCode::CMP_EQ: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            result = RuntimeValue(static_cast<int64_t>(lhs.to_int() == rhs.to_int()));
            break;
        }
            
        case OpCode::CMP_NE: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            result = RuntimeValue(static_cast<int64_t>(lhs.to_int() != rhs.to_int()));
            break;
        }
            
        case OpCode::CMP_LT: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            result = RuntimeValue(static_cast<int64_t>(lhs.to_int() < rhs.to_int()));
            break;
        }
            
        case OpCode::CMP_LE: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            result = RuntimeValue(static_cast<int64_t>(lhs.to_int() <= rhs.to_int()));
            break;
        }
            
        case OpCode::CMP_GT: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            result = RuntimeValue(static_cast<int64_t>(lhs.to_int() > rhs.to_int()));
            break;
        }
            
        case OpCode::CMP_GE: {
            auto lhs = get_value(instr.operands[0]);
            auto rhs = get_value(instr.operands[1]);
            result = RuntimeValue(static_cast<int64_t>(lhs.to_int() >= rhs.to_int()));
            break;
        }
            
        case OpCode::CALL: {
            // Gather arguments
            std::vector<RuntimeValue> args;
            for (const auto& op : instr.operands) {
                args.push_back(get_value(op));
            }
            
            // Check externals first
            auto ext_it = externals_.find(instr.callee);
            if (ext_it != externals_.end()) {
                result = ext_it->second(args);
            } else {
                // Find function in module
                Function* callee = module_->get_function(instr.callee);
                if (callee) {
                    result = call_function(*callee, args);
                }
            }
            break;
        }
            
        case OpCode::ALLOCA:
            // For now, just create a placeholder
            result = RuntimeValue(static_cast<int64_t>(0));
            break;
            
        case OpCode::LOAD:
            // Simplified: return the operand value
            result = get_value(instr.operands[0]);
            break;
            
        case OpCode::STORE:
            // For now, no-op
            break;
            
        // Tensor ops - placeholders for core-runtime integration
        case OpCode::TENSOR_ALLOC:
        case OpCode::TENSOR_ADD:
        case OpCode::TENSOR_SUB:
        case OpCode::TENSOR_MUL:
        case OpCode::TENSOR_MATMUL:
        case OpCode::TENSOR_RELU:
            // TODO: Link to core-runtime
            result = RuntimeValue(static_cast<void*>(nullptr));
            break;
            
        default:
            break;
    }
    
    // Store result
    if (instr.result.valid()) {
        set_value(instr.result, result);
    }
    
    return result;
}

} // namespace backend
} // namespace zero
