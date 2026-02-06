#pragma once

#include "ast.h"
#include "expr_access.h"

namespace vexel {

template <typename ExprFn, typename StmtFn>
inline void for_each_expr_child(const ExprPtr& expr, ExprFn&& on_expr_child, StmtFn&& on_stmt_child) {
    if (!expr) return;
    switch (expr->kind) {
        case Expr::Kind::Binary:
        case Expr::Kind::Assignment:
        case Expr::Kind::Range:
            on_expr_child(expr->left);
            on_expr_child(expr->right);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
            on_expr_child(expr->operand);
            break;
        case Expr::Kind::Call:
            on_expr_child(expr->operand);
            for (const auto& rec : expr->receivers) on_expr_child(rec);
            for (const auto& arg : expr->args) on_expr_child(arg);
            break;
        case Expr::Kind::Index:
            on_expr_child(expr->operand);
            if (!expr->args.empty()) on_expr_child(expr->args[0]);
            break;
        case Expr::Kind::Member:
            on_expr_child(expr->operand);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) on_expr_child(elem);
            break;
        case Expr::Kind::Block:
            for (const auto& st : expr->statements) on_stmt_child(st);
            on_expr_child(expr->result_expr);
            break;
        case Expr::Kind::Conditional:
            on_expr_child(expr->condition);
            on_expr_child(expr->true_expr);
            on_expr_child(expr->false_expr);
            break;
        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
            on_expr_child(loop_subject(expr));
            on_expr_child(loop_body(expr));
            break;
        default:
            break;
    }
}

template <typename ExprFn, typename StmtFn>
inline void for_each_stmt_child(const StmtPtr& stmt, ExprFn&& on_expr_child, StmtFn&& on_stmt_child) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Expr:
            on_expr_child(stmt->expr);
            break;
        case Stmt::Kind::Return:
            on_expr_child(stmt->return_expr);
            break;
        case Stmt::Kind::VarDecl:
            on_expr_child(stmt->var_init);
            break;
        case Stmt::Kind::ConditionalStmt:
            on_expr_child(stmt->condition);
            on_stmt_child(stmt->true_stmt);
            break;
        case Stmt::Kind::FuncDecl:
            on_expr_child(stmt->body);
            break;
        default:
            break;
    }
}

} // namespace vexel

