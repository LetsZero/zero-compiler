/**
 * @file ir.cpp
 * @brief Zero Compiler — IR Printer Implementation
 */

#include "ir/ir.hpp"
#include <sstream>

namespace zero {
namespace ir {

std::string print_value(const Value& v) {
    if (!v.valid()) return "void";
    return "%" + std::to_string(v.id);
}

std::string print_instruction(const Instruction& instr) {
    std::ostringstream ss;
    
    if (instr.result.valid()) {
        ss << print_value(instr.result) << " = ";
    }
    
    ss << opcode_name(instr.op);
    
    // Special cases
    switch (instr.op) {
        case OpCode::CONST_INT:
            ss << " " << instr.imm_int;
            break;
        case OpCode::CONST_FLOAT:
            ss << " " << instr.imm_float;
            break;
        case OpCode::CALL:
            ss << " @" << instr.callee << "(";
            for (size_t i = 0; i < instr.operands.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << print_value(instr.operands[i]);
            }
            ss << ")";
            break;
        case OpCode::BR:
            ss << " bb" << instr.target_block;
            break;
        case OpCode::COND_BR:
            ss << " " << print_value(instr.operands[0])
               << ", bb" << instr.target_block
               << ", bb" << instr.else_block;
            break;
        default:
            for (size_t i = 0; i < instr.operands.size(); ++i) {
                ss << " " << print_value(instr.operands[i]);
                if (i + 1 < instr.operands.size()) ss << ",";
            }
            break;
    }
    
    return ss.str();
}

std::string print_block(const BasicBlock& bb) {
    std::ostringstream ss;
    ss << bb.label << ":\n";
    for (const auto& instr : bb.instrs) {
        ss << "  " << print_instruction(instr) << "\n";
    }
    return ss.str();
}

std::string print_function(const Function& fn) {
    std::ostringstream ss;
    ss << "fn @" << fn.name << "(";
    for (size_t i = 0; i < fn.param_types.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << fn.param_types[i].name();
    }
    ss << ") -> " << fn.return_type.name() << " {\n";
    
    for (const auto& bb : fn.blocks) {
        ss << print_block(bb);
    }
    
    ss << "}\n";
    return ss.str();
}

std::string print_module(const Module& mod) {
    std::ostringstream ss;
    for (const auto& fn : mod.functions) {
        ss << print_function(fn) << "\n";
    }
    return ss.str();
}

} // namespace ir
} // namespace zero
