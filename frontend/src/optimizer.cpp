#include "optimizer.h"

namespace vexel {

OptimizationFacts Optimizer::run(const Module& mod) {
    OptimizationFacts facts;
    if (!type_checker) {
        return facts;
    }

    CompileTimeEvaluator eval(type_checker);
    evaluator = &eval;

    for (const auto& stmt : mod.top_level) {
        visit_stmt(stmt, facts);
    }

    evaluator = nullptr;
    return facts;
}

void Optimizer::mark_constexpr_init(StmtPtr stmt, OptimizationFacts& facts) {
    if (!stmt || stmt->kind != Stmt::Kind::VarDecl || !stmt->var_init) return;
    if (stmt->var_type && stmt->var_type->kind == Type::Kind::Array &&
        (stmt->var_init->kind == Expr::Kind::ArrayLiteral || stmt->var_init->kind == Expr::Kind::Range)) {
        facts.constexpr_inits.insert(stmt.get());
        return;
    }
    if (!evaluator) return;
    CTValue result;
    if (evaluator->try_evaluate(stmt->var_init, result)) {
        facts.constexpr_inits.insert(stmt.get());
        facts.constexpr_values.emplace(stmt->var_init.get(), result);
    }
}

void Optimizer::visit_stmt(StmtPtr stmt, OptimizationFacts& facts) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            if (!stmt->is_external) {
                visit_expr(stmt->body, facts);
            }
            break;
        case Stmt::Kind::VarDecl:
            mark_constexpr_init(stmt, facts);
            visit_expr(stmt->var_init, facts);
            break;
        case Stmt::Kind::Expr:
            visit_expr(stmt->expr, facts);
            break;
        case Stmt::Kind::Return:
            visit_expr(stmt->return_expr, facts);
            break;
        case Stmt::Kind::ConditionalStmt:
            visit_expr(stmt->condition, facts);
            visit_stmt(stmt->true_stmt, facts);
            break;
        default:
            break;
    }
}

void Optimizer::visit_expr(ExprPtr expr, OptimizationFacts& facts) {
    if (!expr) return;

    if (evaluator && !facts.constexpr_values.count(expr.get())) {
        CTValue value;
        if (evaluator->try_evaluate(expr, value)) {
            facts.constexpr_values.emplace(expr.get(), value);
        }
    }

    switch (expr->kind) {
        case Expr::Kind::Call:
            visit_expr(expr->operand, facts);
            for (const auto& rec : expr->receivers) visit_expr(rec, facts);
            for (const auto& arg : expr->args) visit_expr(arg, facts);
            break;
        case Expr::Kind::Binary:
        case Expr::Kind::Assignment:
        case Expr::Kind::Range:
            visit_expr(expr->left, facts);
            visit_expr(expr->right, facts);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
        case Expr::Kind::Member:
            visit_expr(expr->operand, facts);
            break;
        case Expr::Kind::Index:
            visit_expr(expr->operand, facts);
            if (!expr->args.empty()) visit_expr(expr->args[0], facts);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) visit_expr(elem, facts);
            break;
        case Expr::Kind::Block:
            for (const auto& st : expr->statements) visit_stmt(st, facts);
            visit_expr(expr->result_expr, facts);
            break;
        case Expr::Kind::Conditional:
            visit_expr(expr->condition, facts);
            visit_expr(expr->true_expr, facts);
            visit_expr(expr->false_expr, facts);
            break;
        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
            visit_expr(expr->left, facts);
            visit_expr(expr->right, facts);
            break;
        default:
            break;
    }
}

} // namespace vexel
