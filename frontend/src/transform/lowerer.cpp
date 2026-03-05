#include "lowerer.h"
#include "expr_access.h"

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

TypePtr Lowerer::lower_type(TypePtr type) {
    if (!type) return nullptr;
    if (type->kind != Type::Kind::Array) return type;
    return Type::make_array(lower_type(type->element_type), type->array_size, type->location);
}

void Lowerer::run(Module& mod) {
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
            for (auto& param : stmt->params) {
                param.type = lower_type(param.type);
            }
            for (auto& ref_type : stmt->ref_param_types) {
                ref_type = lower_type(ref_type);
            }
            stmt->return_type = lower_type(stmt->return_type);
            for (auto& rt : stmt->return_types) {
                rt = lower_type(rt);
            }
            break;
        case Stmt::Kind::VarDecl:
            if (stmt->var_init) {
                stmt->var_init = lower_expr(stmt->var_init);
            }
            stmt->var_type = lower_type(stmt->var_type);
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
            for (auto& field : stmt->fields) {
                field.type = lower_type(field.type);
            }
            break;
        case Stmt::Kind::Import:
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}

ExprPtr Lowerer::lower_expr(ExprPtr expr) {
    if (!expr) return nullptr;

    TypePtr original_type = expr->type;
    TypePtr original_target_type = expr->target_type;
    TypePtr original_declared_var_type = expr->declared_var_type;

    switch (expr->kind) {
        case Expr::Kind::Binary:
            expr->left = lower_expr(expr->left);
            expr->right = lower_expr(expr->right);
            break;
        case Expr::Kind::Unary:
            expr->operand = lower_expr(expr->operand);
            break;
        case Expr::Kind::Cast:
            expr->operand = lower_expr(expr->operand);
            expr->target_type = lower_type(expr->target_type);
            break;
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
        case Expr::Kind::Conditional:
            expr->condition = lower_expr(expr->condition);
            expr->true_expr = lower_expr(expr->true_expr);
            expr->false_expr = lower_expr(expr->false_expr);
            break;
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
            loop_body_ref(expr) = wrap_stmt_block(lower_expr(loop_body(expr)));
            break;
        case Expr::Kind::Repeat:
            loop_subject_ref(expr) = lower_expr(loop_subject(expr));
            loop_body_ref(expr) = wrap_stmt_block(lower_expr(loop_body(expr)));
            break;
        default:
            break;
    }

    expr->type = lower_type(original_type);
    expr->target_type = lower_type(original_target_type);
    expr->declared_var_type = lower_type(original_declared_var_type);
    return expr;
}

} // namespace vexel
