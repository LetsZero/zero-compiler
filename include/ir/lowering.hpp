#ifndef ZERO_IR_LOWERING_HPP
#define ZERO_IR_LOWERING_HPP

/**
 * @file lowering.hpp
 * @brief Zero Compiler — AST to IR Lowering
 */

#include "ir/ir.hpp"
#include "ir/builder.hpp"
#include "ast/ast.hpp"

#include <unordered_map>
#include <unordered_set>

namespace zero {
namespace ir {

/**
 * Lowers AST to IR.
 */
class Lowering {
public:
    Module lower(ast::Program& prog);

private:
    // Symbol table (variable name -> Value). For an ordinary variable the
    // Value is its current SSA value; for a *mutated* variable (one that is
    // reassigned somewhere in the function — see cell_vars_) the Value is the
    // ALLOCA pointer to its memory cell, and reads go through a LOAD.
    std::unordered_map<std::string, Value> symbols_;

    // Names that are reassigned somewhere in the current function. These get
    // the memory-cell (alloca/load/store) treatment so mutation is visible
    // across basic blocks (e.g. a `while` condition sees the updated value).
    // Variables that are never reassigned keep the direct-SSA binding, so
    // their lowered IR is byte-identical to before this feature existed.
    std::unordered_set<std::string> cell_vars_;

    // Spec 006: function-name -> declared return type. Populated by a
    // forward pass at the top of lower() so CallExpr lowering knows the
    // result type of user-defined functions and can give the CALL
    // instruction a typed result Value.
    std::unordered_map<std::string, types::Type> fn_return_types_;

    void lower_function(Module& mod, ast::FnDecl& fn);
    void lower_stmt(IRBuilder& builder, ast::Stmt& stmt);
    Value lower_expr(IRBuilder& builder, ast::Expr& expr);
    
    void lower_if(IRBuilder& builder, ast::IfStmt& if_stmt);
    void lower_while(IRBuilder& builder, ast::WhileStmt& while_stmt);
};

} // namespace ir
} // namespace zero

#endif // ZERO_IR_LOWERING_HPP
