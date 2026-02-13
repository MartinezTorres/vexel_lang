#include "lowerer.h"
#include "expr_access.h"
#include "typechecker.h"

namespace vexel {

namespace {
ExprPtr wrap_stmt_block(ExprPtr expr) {
    if (!expr || expr->kind == Expr::Kind::Block) return expr;
    std::vector<StmtPtr> stmts;
    stmts.push_back(Stmt::make_expr(expr, expr->location));
    return Expr::make_block(stmts, nullptr, expr->location);
}
} // namespace

Lowerer::Lowerer(TypeChecker* checker)
    : checker(checker) {}

void Lowerer::run(Module& mod) {
    // Invariant: lowering only simplifies expressions; it must not change inferred types.
    for (auto& stmt : mod.top_level) {
        lower_stmt(stmt);
    }
}

void Lowerer::lower_stmt(StmtPtr stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            if (stmt->body) {
                stmt->body = lower_expr(stmt->body);
            }
            break;
        case Stmt::Kind::VarDecl:
            if (stmt->var_init) {
                stmt->var_init = lower_expr(stmt->var_init);
            }
            break;
        case Stmt::Kind::Expr:
            stmt->expr = lower_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            stmt->return_expr = lower_expr(stmt->return_expr);
            break;
        case Stmt::Kind::ConditionalStmt:
            stmt->condition = lower_expr(stmt->condition);
            lower_stmt(stmt->true_stmt);
            break;
        case Stmt::Kind::TypeDecl:
        case Stmt::Kind::Import:
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}

ExprPtr Lowerer::lower_expr(ExprPtr expr) {
    if (!expr) return nullptr;

    switch (expr->kind) {
        case Expr::Kind::Binary:
            expr->left = lower_expr(expr->left);
            expr->right = lower_expr(expr->right);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
            expr->operand = lower_expr(expr->operand);
            break;
        case Expr::Kind::Call:
            for (auto& rec : expr->receivers) {
                rec = lower_expr(rec);
            }
            for (auto& arg : expr->args) {
                arg = lower_expr(arg);
            }
            break;
        case Expr::Kind::Index:
            expr->operand = lower_expr(expr->operand);
            if (!expr->args.empty()) {
                expr->args[0] = lower_expr(expr->args[0]);
            }
            break;
        case Expr::Kind::Member:
            expr->operand = lower_expr(expr->operand);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (auto& elem : expr->elements) {
                elem = lower_expr(elem);
            }
            break;
        case Expr::Kind::Block:
            for (auto& st : expr->statements) {
                lower_stmt(st);
            }
            expr->result_expr = lower_expr(expr->result_expr);
            break;
        case Expr::Kind::Conditional: {
            expr->condition = lower_expr(expr->condition);
            expr->true_expr = lower_expr(expr->true_expr);
            expr->false_expr = lower_expr(expr->false_expr);
            break;
        }
        case Expr::Kind::Assignment:
            expr->left = lower_expr(expr->left);
            expr->right = lower_expr(expr->right);
            break;
        case Expr::Kind::Range:
            expr->left = lower_expr(expr->left);
            expr->right = lower_expr(expr->right);
            break;
        case Expr::Kind::Iteration:
            loop_subject_ref(expr) = lower_expr(loop_subject(expr));
            loop_body_ref(expr) = lower_expr(loop_body(expr));
            loop_body_ref(expr) = wrap_stmt_block(loop_body(expr));
            break;
        case Expr::Kind::Repeat:
            loop_subject_ref(expr) = lower_expr(loop_subject(expr));
            loop_body_ref(expr) = lower_expr(loop_body(expr));
            loop_body_ref(expr) = wrap_stmt_block(loop_body(expr));
            break;
        default:
            break;
    }

    return expr;
}

} // namespace vexel
