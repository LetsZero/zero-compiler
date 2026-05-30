/**
 * @file interpreter.cpp
 * @brief Zero Compiler — ZIR Interpreter Implementation
 */

#include "backend/interpreter.hpp"

// Spec 003 / 005: pull in the specific runtime ops we use. We avoid the
// umbrella <zero/zero.hpp> to dodge the zero::ir:: namespace collision
// (see interpreter.hpp).
#include <zero/ops/elementwise.hpp>
#include <zero/ops/matmul.hpp>
#include <zero/ops/reduce.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace zero {
namespace backend {

using namespace ir;

// ─────────────────────────────────────────────────────────────────────
// Spec 005: helpers shared across the wired tensor opcodes.
// ─────────────────────────────────────────────────────────────────────

namespace {

// Allocate a fresh contiguous F32 output tensor with the given shape,
// wrapped in a shared_ptr with an owns_data-respecting deleter. The
// buffer is sentinel-filled with 0xAB so callers can observe the
// runtime's "writes zero bytes on error" contract.
static TensorPtr alloc_output_like(const int64_t* shape, int8_t ndim) {
    TensorPtr out(
        new zero::Tensor(zero::Tensor::alloc(shape, ndim, zero::DType::F32)),
        [](zero::Tensor* p) { if (p && p->owns_data) p->free(); delete p; });
    if (out->data == nullptr) {
        throw std::runtime_error("tensor op: output allocation failed");
    }
    std::memset(out->data, 0xAB, out->nbytes());
    return out;
}

// Format-and-throw helper. Centralises the message format that spec 003
// established (op name + code + msg + @span fragment).
//
// Spec 010: when a SourceManager is provided and the span is valid, emit a
// "Frame & Focus" RUNTIME diagnostic before throwing. The throw itself is
// unchanged — the Reporter call is additive (rich rendering), the throw is
// still the control-flow mechanism.
static void throw_with_span(const char* op_name,
                            const zero::Status& s,
                            const source::Span& span,
                            const source::SourceManager* sm) {
    if (sm != nullptr && span.valid()) {
        auto [line, col] = sm->get_line_col(span);
        diagnostics::SourceLocation loc(
            sm->get_path(span.source_id),
            static_cast<int>(line),
            static_cast<int>(col));
        std::string rich = std::string(op_name) + ": "
            + (s.msg ? s.msg : "runtime error");
        diagnostics::Reporter::reportError(
            diagnostics::ErrorType::RUNTIME, loc, rich,
            /*help=*/"check tensor shapes and dtypes at this call site");
    }

    std::ostringstream msg;
    msg << op_name << " failed: code=" << static_cast<int>(s.code);
    if (s.msg) msg << " (" << s.msg << ")";
    if (span.valid()) {
        msg << " @" << static_cast<uint32_t>(span.source_id)
            << ":" << span.start_offset
            << "-" << span.end_offset;
    }
    throw std::runtime_error(msg.str());
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main execution
// ─────────────────────────────────────────────────────────────────────────────

RuntimeValue Interpreter::execute(Module& mod, const std::string& entry) {
    module_ = &mod;
    call_stack_.clear();
    // Spec 008: call_function now addresses its frame by stable index,
    // so reallocation during nested calls is safe. No reserve needed.
    
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
    
    // Push call frame, binding incoming args to the callee's parameter
    // SSA values before the frame goes on the stack (spec 006). Binding
    // before push means the very first instruction of the callee can
    // already see the args via get_value().
    CallFrame frame;
    frame.fn = &fn;
    frame.block_idx = 0;
    frame.instr_idx = 0;
    for (size_t i = 0; i < args.size() && i < fn.params.size(); ++i) {
        frame.locals[fn.params[i].id] = std::move(args[i]);
    }
    call_stack_.push_back(std::move(frame));

    // Spec 008: address this frame by stable index. References into
    // call_stack_ are invalidated by any push_back that grows the
    // vector — a nested call_function via exec_instruction can do
    // exactly that. Indices are stable as long as we never pop below
    // my_frame_idx, which is the existing interpreter invariant.
    const size_t my_frame_idx = call_stack_.size() - 1;

    RuntimeValue result;

    while (call_stack_.size() > my_frame_idx
           && call_stack_[my_frame_idx].fn == &fn) {
        size_t block_idx = call_stack_[my_frame_idx].block_idx;
        if (block_idx >= fn.blocks.size()) break;
        const BasicBlock& bb = fn.blocks[block_idx];

        while (call_stack_[my_frame_idx].instr_idx < bb.instrs.size()) {
            const Instruction& instr =
                bb.instrs[call_stack_[my_frame_idx].instr_idx];

            if (instr.op == OpCode::RET) {
                if (!instr.operands.empty()) {
                    result = get_value(instr.operands[0]);
                }
                call_stack_.pop_back();
                return result;
            }

            if (instr.op == OpCode::BR) {
                call_stack_[my_frame_idx].block_idx = instr.target_block;
                call_stack_[my_frame_idx].instr_idx = 0;
                break;
            }

            if (instr.op == OpCode::COND_BR) {
                RuntimeValue cond = get_value(instr.operands[0]);
                call_stack_[my_frame_idx].block_idx =
                    (cond.to_int() != 0) ? instr.target_block : instr.else_block;
                call_stack_[my_frame_idx].instr_idx = 0;
                break;
            }

            // exec_instruction may recursively call_function and reallocate
            // call_stack_. Re-derive the frame address after the call before
            // mutating instr_idx.
            result = exec_instruction(instr);
            call_stack_[my_frame_idx].instr_idx++;
        }

        // Fall-through to next block.
        if (call_stack_[my_frame_idx].instr_idx >= bb.instrs.size()
            && call_stack_[my_frame_idx].block_idx < fn.blocks.size() - 1) {
            call_stack_[my_frame_idx].block_idx++;
            call_stack_[my_frame_idx].instr_idx = 0;
        } else if (call_stack_[my_frame_idx].instr_idx >= bb.instrs.size()) {
            break;
        }
    }

    // Pop frame if still on stack
    if (call_stack_.size() > my_frame_idx
        && call_stack_[my_frame_idx].fn == &fn) {
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
            
        // Tensor ops — spec 003 wires TENSOR_CONST_F32 and TENSOR_ADD to
        // the frozen runtime. The remaining TENSOR_* opcodes stay stubbed
        // pending follow-up specs.
        case OpCode::TENSOR_CONST_F32: {
            // Build a fresh contiguous F32 tensor from instr.imm_shape +
            // instr.imm_floats, wrap it in a shared_ptr whose deleter
            // respects owns_data, then store as the result.
            int64_t shape_arr[zero::MAX_DIMS] = {0};
            int8_t ndim = static_cast<int8_t>(instr.imm_shape.size());
            for (int8_t i = 0; i < ndim; ++i) shape_arr[i] = instr.imm_shape[i];

            TensorPtr t(
                new zero::Tensor(zero::Tensor::alloc(shape_arr, ndim, zero::DType::F32)),
                [](zero::Tensor* p) { if (p && p->owns_data) p->free(); delete p; });

            if (t->data == nullptr) {
                throw std::runtime_error("tensor.const.f32: allocation failed");
            }

            // Fill bytes from imm_floats (caller is responsible for sizing).
            const size_t n = std::min<size_t>(instr.imm_floats.size(), static_cast<size_t>(t->numel()));
            float* dst = static_cast<float*>(t->data);
            for (size_t i = 0; i < n; ++i) dst[i] = instr.imm_floats[i];

            result = RuntimeValue(std::move(t));
            break;
        }

        // ─── Binary tensor ops (spec 003 + 005). Output shape = LHS shape.
        case OpCode::TENSOR_ADD:
        case OpCode::TENSOR_SUB:
        case OpCode::TENSOR_MUL:
        case OpCode::TENSOR_DIV: {
            RuntimeValue lhs_v = get_value(instr.operands[0]);
            RuntimeValue rhs_v = get_value(instr.operands[1]);
            if (!lhs_v.is_tensor() || !rhs_v.is_tensor()) {
                throw std::runtime_error("tensor binary op: non-tensor operand");
            }
            const TensorPtr& a = lhs_v.as_tensor();
            const TensorPtr& b = rhs_v.as_tensor();

            int64_t shape_arr[zero::MAX_DIMS] = {0};
            for (int8_t i = 0; i < a->ndim; ++i) shape_arr[i] = a->shape[i];
            TensorPtr out = alloc_output_like(shape_arr, a->ndim);

            zero::Status s;
            const char* op_name = "tensor_op";
            switch (instr.op) {
                case OpCode::TENSOR_ADD: s = zero::ops::add(*a, *b, *out); op_name = "tensor_add"; break;
                case OpCode::TENSOR_SUB: s = zero::ops::sub(*a, *b, *out); op_name = "tensor_sub"; break;
                case OpCode::TENSOR_MUL: s = zero::ops::mul(*a, *b, *out); op_name = "tensor_mul"; break;
                case OpCode::TENSOR_DIV: s = zero::ops::div(*a, *b, *out); op_name = "tensor_div"; break;
                default: break;  // unreachable
            }
            if (s.is_error()) throw_with_span(op_name, s, instr.span, sm_);
            result = RuntimeValue(std::move(out));
            break;
        }

        // ─── Unary tensor ops (spec 005). Output shape = operand shape.
        case OpCode::TENSOR_NEG:
        case OpCode::TENSOR_RELU: {
            RuntimeValue x_v = get_value(instr.operands[0]);
            if (!x_v.is_tensor()) {
                throw std::runtime_error("tensor unary op: non-tensor operand");
            }
            const TensorPtr& x = x_v.as_tensor();

            int64_t shape_arr[zero::MAX_DIMS] = {0};
            for (int8_t i = 0; i < x->ndim; ++i) shape_arr[i] = x->shape[i];
            TensorPtr out = alloc_output_like(shape_arr, x->ndim);

            zero::Status s;
            const char* op_name = "tensor_op";
            switch (instr.op) {
                case OpCode::TENSOR_NEG:  s = zero::ops::neg(*x, *out);  op_name = "tensor_neg";  break;
                case OpCode::TENSOR_RELU: s = zero::ops::relu(*x, *out); op_name = "tensor_relu"; break;
                default: break;  // unreachable
            }
            if (s.is_error()) throw_with_span(op_name, s, instr.span, sm_);
            result = RuntimeValue(std::move(out));
            break;
        }

        // ─── Matrix multiplication (spec 005). Output shape = [A.rows, B.cols].
        case OpCode::TENSOR_MATMUL: {
            RuntimeValue lhs_v = get_value(instr.operands[0]);
            RuntimeValue rhs_v = get_value(instr.operands[1]);
            if (!lhs_v.is_tensor() || !rhs_v.is_tensor()) {
                throw std::runtime_error("tensor_matmul: non-tensor operand");
            }
            const TensorPtr& a = lhs_v.as_tensor();
            const TensorPtr& b = rhs_v.as_tensor();
            // matmul requires rank-2; the runtime returns INVALID_ARGUMENT
            // otherwise. We don't pre-validate; just construct the output
            // shape from A.rows × B.cols and let the runtime decide.
            int64_t shape_arr[2] = {
                a->ndim >= 1 ? a->shape[0] : 0,
                b->ndim >= 2 ? b->shape[1] : 0
            };
            TensorPtr out = alloc_output_like(shape_arr, 2);

            zero::Status s = zero::ops::matmul(*a, *b, *out);
            if (s.is_error()) throw_with_span("tensor_matmul", s, instr.span, sm_);
            result = RuntimeValue(std::move(out));
            break;
        }

        // ─── Full reductions (spec 014). Output is a [1] F32 tensor.
        case OpCode::TENSOR_SUM:
        case OpCode::TENSOR_MEAN: {
            RuntimeValue x = get_value(instr.operands[0]);
            if (!x.is_tensor()) throw std::runtime_error("reduction: non-tensor operand");
            const TensorPtr& t = x.as_tensor();
            zero::ops::ReduceOp op = (instr.op == OpCode::TENSOR_SUM)
                ? zero::ops::ReduceOp::SUM
                : zero::ops::ReduceOp::MEAN;
            float v = zero::ops::reduce_all(*t, op);
            int64_t one[1] = {1};
            TensorPtr out = alloc_output_like(one, 1);
            static_cast<float*>(out->data)[0] = v;
            result = RuntimeValue(std::move(out));
            break;
        }

        case OpCode::TENSOR_ARGMAX: {
            RuntimeValue x = get_value(instr.operands[0]);
            if (!x.is_tensor()) throw std::runtime_error("argmax: non-tensor operand");
            const TensorPtr& t = x.as_tensor();
            const float* d = static_cast<const float*>(t->data);
            int64_t n = t->numel();
            int64_t best = 0;
            for (int64_t i = 1; i < n; ++i) {
                if (d[i] > d[best]) best = i;
            }
            int64_t one[1] = {1};
            TensorPtr out = alloc_output_like(one, 1);
            static_cast<float*>(out->data)[0] = static_cast<float>(best);
            result = RuntimeValue(std::move(out));
            break;
        }

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
