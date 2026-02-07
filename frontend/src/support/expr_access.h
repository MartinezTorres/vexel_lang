#pragma once

#include "ast.h"
#include "common.h"

namespace vexel {

inline bool is_loop_expr(const ExprPtr& expr) {
    if (!expr) return false;
    return expr->kind == Expr::Kind::Iteration || expr->kind == Expr::Kind::Repeat;
}

// Canonical loop subject access:
// - Iteration uses operand (iterable)
// - Repeat uses condition (re-evaluated condition)
inline ExprPtr loop_subject(const ExprPtr& expr) {
    if (!expr) return nullptr;
    switch (expr->kind) {
        case Expr::Kind::Iteration:
            return expr->operand;
        case Expr::Kind::Repeat:
            return expr->condition;
        default:
            throw CompileError("Internal error: loop_subject called on non-loop expression", expr->location);
    }
}

// Canonical loop body access for both Iteration and Repeat.
inline ExprPtr loop_body(const ExprPtr& expr) {
    if (!expr) return nullptr;
    if (!is_loop_expr(expr)) {
        throw CompileError("Internal error: loop_body called on non-loop expression", expr->location);
    }
    return expr->right;
}

inline ExprPtr& loop_subject_ref(const ExprPtr& expr) {
    if (!expr) {
        throw CompileError("Internal error: loop_subject_ref called with null expression", SourceLocation());
    }
    switch (expr->kind) {
        case Expr::Kind::Iteration:
            return expr->operand;
        case Expr::Kind::Repeat:
            return expr->condition;
        default:
            throw CompileError("Internal error: loop_subject_ref called on non-loop expression", expr->location);
    }
}

inline ExprPtr& loop_body_ref(const ExprPtr& expr) {
    if (!expr) {
        throw CompileError("Internal error: loop_body_ref called with null expression", SourceLocation());
    }
    if (!is_loop_expr(expr)) {
        throw CompileError("Internal error: loop_body_ref called on non-loop expression", expr->location);
    }
    return expr->right;
}

} // namespace vexel
