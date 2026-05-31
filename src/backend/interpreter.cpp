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
#include <zero/ops/reshape.hpp>   // permute (transpose view)
#include <zero/ops/layout.hpp>    // contiguous (materialize view)

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

// Spec 017: materialise a scalar RuntimeValue as a [1] F32 tensor so the
// runtime's numel==1 broadcast can apply it against a tensor operand.
static TensorPtr scalar_to_tensor1(const RuntimeValue& v) {
    int64_t one[1] = {1};
    TensorPtr t(
        new zero::Tensor(zero::Tensor::alloc(one, 1, zero::DType::F32)),
        [](zero::Tensor* p) { if (p && p->owns_data) p->free(); delete p; });
    static_cast<float*>(t->data)[0] = static_cast<float>(v.to_float());
    return t;
}

// Materialise a scalar RuntimeValue as a full F32 tensor matching `like`'s
// shape, every element set to the scalar. Used for `scalar OP tensor` with a
// non-commutative op (`s - t`, `s / t`): the runtime only broadcasts a
// numel==1 operand on the RHS, so to compute `s OP t[i]` per element we lift
// the scalar to the tensor's shape and do a same-shape elementwise op.
static TensorPtr scalar_to_tensor_like(const RuntimeValue& v, const zero::Tensor& like) {
    int64_t shape[zero::MAX_DIMS] = {0};
    for (int8_t i = 0; i < like.ndim; ++i) shape[i] = like.shape[i];
    TensorPtr t(
        new zero::Tensor(zero::Tensor::alloc(shape, like.ndim, zero::DType::F32)),
        [](zero::Tensor* p) { if (p && p->owns_data) p->free(); delete p; });
    if (t->data == nullptr) {
        throw std::runtime_error("scalar broadcast: output allocation failed");
    }
    float* d = static_cast<float*>(t->data);
    const int64_t n = t->numel();
    const float fv = static_cast<float>(v.to_float());
    for (int64_t i = 0; i < n; ++i) d[i] = fv;
    return t;
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

    // Call-depth guard. The interpreter recurses on the native C++ stack for
    // every Zero-level call (exec_instruction CALL -> call_function), so an
    // unbounded recursion would overflow the real stack and SIGSEGV. Turn that
    // crash into a catchable diagnostic well before the native limit.
    if (call_stack_.size() >= kMaxCallDepth) {
        throw std::runtime_error(
            "maximum call depth exceeded (" + std::to_string(kMaxCallDepth) +
            ") — unbounded recursion in '" + fn.name + "'?");
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
                // A bare `return` (or implicit end-of-function return) yields a
                // defined void value — never the leftover `result` from the
                // last evaluated instruction. Returning `result` here let a
                // non-void function that fell off the end leak its last
                // expression's value (now also flagged by sema).
                RuntimeValue ret = instr.operands.empty()
                    ? RuntimeValue{}
                    : get_value(instr.operands[0]);
                call_stack_.pop_back();
                return ret;
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
            // Create a memory cell for a mutable variable, identified by this
            // instruction's result id. The "pointer" value itself is never
            // read directly — load/store address the cell by operand id.
            if (!call_stack_.empty()) {
                call_stack_.back().memory[instr.result.id] = RuntimeValue{};
            }
            break;

        case OpCode::LOAD: {
            // Read the cell named by the pointer operand (an ALLOCA result).
            if (!call_stack_.empty()) {
                auto& mem = call_stack_.back().memory;
                auto it = mem.find(instr.operands[0].id);
                if (it != mem.end()) result = it->second;
            }
            break;
        }

        case OpCode::STORE:
            // Write the value operand into the cell named by the pointer operand.
            if (!call_stack_.empty()) {
                call_stack_.back().memory[instr.operands[0].id] =
                    get_value(instr.operands[1]);
            }
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

            // Spec 017: tensor-scalar broadcast. `a` is always the
            // shape-defining tensor operand; `b` is the other (materialised
            // to a [1] tensor if it's a scalar). The runtime broadcasts a
            // numel==1 `b` across `a`.
            TensorPtr a, b;
            const bool lt = lhs_v.is_tensor();
            const bool rt = rhs_v.is_tensor();
            if (lt && rt) {
                a = lhs_v.as_tensor();
                b = rhs_v.as_tensor();
            } else if (lt) {                       // tensor OP scalar
                a = lhs_v.as_tensor();
                b = scalar_to_tensor1(rhs_v);
            } else if (rt) {                       // scalar OP tensor
                const TensorPtr& t = rhs_v.as_tensor();
                if (instr.op == OpCode::TENSOR_ADD || instr.op == OpCode::TENSOR_MUL) {
                    // commutative: swap so the tensor is the shape-defining `a`
                    // and the scalar broadcasts via numel==1.
                    a = t;
                    b = scalar_to_tensor1(lhs_v);
                } else {
                    // non-commutative (s - t, s / t): lift the scalar to the
                    // tensor's shape so the elementwise op yields s OP t[i].
                    a = scalar_to_tensor_like(lhs_v, *t);
                    b = t;
                }
            } else {
                throw std::runtime_error("tensor binary op: non-tensor operand");
            }

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

        // ─── Unary tensor ops (specs 005, 016). Output shape = operand shape.
        case OpCode::TENSOR_NEG:
        case OpCode::TENSOR_RELU:
        case OpCode::TENSOR_EXP:
        case OpCode::TENSOR_LOG:
        case OpCode::TENSOR_SQRT:
        case OpCode::TENSOR_TANH:
        case OpCode::TENSOR_SIGMOID: {
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
                case OpCode::TENSOR_NEG:     s = zero::ops::neg(*x, *out);     op_name = "tensor_neg";     break;
                case OpCode::TENSOR_RELU:    s = zero::ops::relu(*x, *out);    op_name = "tensor_relu";    break;
                case OpCode::TENSOR_EXP:     s = zero::ops::exp(*x, *out);     op_name = "tensor_exp";     break;
                case OpCode::TENSOR_LOG:     s = zero::ops::log(*x, *out);     op_name = "tensor_log";     break;
                case OpCode::TENSOR_SQRT:    s = zero::ops::sqrt(*x, *out);    op_name = "tensor_sqrt";    break;
                case OpCode::TENSOR_TANH:    s = zero::ops::tanh(*x, *out);    op_name = "tensor_tanh";    break;
                case OpCode::TENSOR_SIGMOID: s = zero::ops::sigmoid(*x, *out); op_name = "tensor_sigmoid"; break;
                default: break;  // unreachable
            }
            if (s.is_error()) throw_with_span(op_name, s, instr.span, sm_);
            result = RuntimeValue(std::move(out));
            break;
        }

        // ─── 2-D transpose. permute([1,0]) gives a strided view, which matmul/
        // reduce reject; contiguous() materialises it into a fresh F32 tensor.
        case OpCode::TENSOR_TRANSPOSE: {
            RuntimeValue xv = get_value(instr.operands[0]);
            if (!xv.is_tensor()) throw std::runtime_error("tensor_transpose: non-tensor operand");
            const TensorPtr& x = xv.as_tensor();

            if (x->ndim != 2) {
                // Rank-0/1: transpose is a no-op. Rank>2 is unsupported (the
                // language only produces 1-D/2-D tensors today).
                if (x->ndim < 2) { result = xv; break; }
                throw std::runtime_error("tensor_transpose: only rank-2 tensors supported");
            }

            int8_t perm[2] = {1, 0};
            zero::Tensor view = zero::ops::permute(*x, perm);   // shares x's buffer
            int64_t out_shape[2] = { x->shape[1], x->shape[0] };
            TensorPtr out = alloc_output_like(out_shape, 2);
            zero::Status s = zero::ops::contiguous(view, *out);
            if (s.is_error()) throw_with_span("tensor_transpose", s, instr.span, sm_);
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
            // matmul requires rank-2. Pre-validate the ranks here: otherwise a
            // rank-<2 operand produces a degenerate output shape (e.g. [N, 0]),
            // whose zero-byte allocation fails first and masks the real cause
            // with a misleading "output allocation failed".
            if (a->ndim != 2 || b->ndim != 2) {
                std::ostringstream m;
                m << "tensor_matmul: requires rank-2 operands, got ranks "
                  << static_cast<int>(a->ndim) << " and " << static_cast<int>(b->ndim);
                throw std::runtime_error(m.str());
            }
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
