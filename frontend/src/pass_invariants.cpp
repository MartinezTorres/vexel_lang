#include "pass_invariants.h"

#include "ast_walk.h"
#include "common.h"

namespace vexel {
namespace {

[[noreturn]] void invariant_fail(const std::string& stage, const SourceLocation& loc, const std::string& msg) {
    throw CompileError("Invariant failure [" + stage + "]: " + msg, loc);
}

void validate_expr(const ExprPtr& expr, const std::string& stage);
void validate_stmt(const StmtPtr& stmt, const std::string& stage);

void validate_expr(const ExprPtr& expr, const std::string& stage) {
    if (!expr) return;

    switch (expr->kind) {
        case Expr::Kind::Binary:
        case Expr::Kind::Assignment:
        case Expr::Kind::Range:
            if (!expr->left || !expr->right) {
                invariant_fail(stage, expr->location, "binary/assignment/range node missing operand");
            }
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
            if (!expr->operand) {
                invariant_fail(stage, expr->location, "unary/cast/length node missing operand");
            }
            break;
        case Expr::Kind::Call:
            if (!expr->operand) {
                invariant_fail(stage, expr->location, "call node missing callee operand");
            }
            break;
        case Expr::Kind::Index:
            if (!expr->operand || expr->args.empty()) {
                invariant_fail(stage, expr->location, "index node missing array or index expression");
            }
            break;
        case Expr::Kind::Member:
            if (!expr->operand) {
                invariant_fail(stage, expr->location, "member node missing base operand");
            }
            break;
        case Expr::Kind::Conditional:
            if (!expr->condition || !expr->true_expr || !expr->false_expr) {
                invariant_fail(stage, expr->location, "conditional node missing branch expression");
            }
            break;
        case Expr::Kind::Iteration:
            if (!expr->operand || !expr->right) {
                invariant_fail(stage, expr->location, "iteration node missing iterable or body");
            }
            if (expr->condition || expr->left) {
                invariant_fail(stage, expr->location, "iteration node has unexpected field populated");
            }
            break;
        case Expr::Kind::Repeat:
            if (!expr->condition || !expr->right) {
                invariant_fail(stage, expr->location, "repeat node missing condition or body");
            }
            if (expr->operand || expr->left) {
                invariant_fail(stage, expr->location, "repeat node has unexpected field populated");
            }
            break;
        default:
            break;
    }

    for_each_expr_child(
        expr,
        [&](const ExprPtr& child) { validate_expr(child, stage); },
        [&](const StmtPtr& child) { validate_stmt(child, stage); });
}

void validate_stmt(const StmtPtr& stmt, const std::string& stage) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::VarDecl:
            if (stmt->var_name.empty()) {
                invariant_fail(stage, stmt->location, "variable declaration has empty name");
            }
            break;
        case Stmt::Kind::FuncDecl:
            if (stmt->func_name.empty()) {
                invariant_fail(stage, stmt->location, "function declaration has empty name");
            }
            if (!stmt->is_external && !stmt->body) {
                invariant_fail(stage, stmt->location, "non-external function has no body");
            }
            break;
        case Stmt::Kind::TypeDecl:
            if (stmt->type_decl_name.empty()) {
                invariant_fail(stage, stmt->location, "type declaration has empty name");
            }
            break;
        case Stmt::Kind::Import:
            if (stmt->import_path.empty()) {
                invariant_fail(stage, stmt->location, "import declaration has empty path");
            }
            break;
        case Stmt::Kind::ConditionalStmt:
            if (!stmt->condition || !stmt->true_stmt) {
                invariant_fail(stage, stmt->location, "statement conditional missing condition or body");
            }
            break;
        default:
            break;
    }

    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& child) { validate_expr(child, stage); },
        [&](const StmtPtr& child) { validate_stmt(child, stage); });
}

void validate_module(const Module& mod, const std::string& stage) {
    for (const auto& stmt : mod.top_level) {
        if (!stmt) {
            invariant_fail(stage, mod.location, "top-level statement is null");
        }
        validate_stmt(stmt, stage);
    }
}

} // namespace

void validate_module_invariants(const Module& mod, const char* stage) {
    validate_module(mod, stage ? stage : "unknown");
}

void validate_program_invariants(const Program& program, const char* stage) {
    std::string stage_name = stage ? stage : "unknown";
    for (const auto& mod_info : program.modules) {
        validate_module(mod_info.module, stage_name);
    }
}

} // namespace vexel

