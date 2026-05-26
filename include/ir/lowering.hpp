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

namespace zero {
namespace ir {

/**
 * Lowers AST to IR.
 */
class Lowering {
public:
    Module lower(ast::Program& prog);

private:
    // Symbol table (variable name -> Value)
    std::unordered_map<std::string, Value> symbols_;

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
