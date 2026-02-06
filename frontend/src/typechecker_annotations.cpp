#include "typechecker.h"
#include <iostream>

namespace vexel {

static bool ann_is(const Annotation& ann, const std::string& name) {
    return ann.name == name;
}
void TypeChecker::warn_annotation(const Annotation& ann, const std::string& msg) {
    std::cerr << "Warning: " << msg;
    if (!ann.location.filename.empty()) {
        std::cerr << " at " << ann.location.filename << ":" << ann.location.line << ":" << ann.location.column;
    }
    std::cerr << " [[" << ann.name << "]]" << std::endl;
}
void TypeChecker::validate_annotations(const Module& mod) {
    for (const auto& stmt : mod.top_level) {
        validate_stmt_annotations(stmt);
    }
}
void TypeChecker::validate_stmt_annotations(StmtPtr stmt) {
    if (!stmt) return;

    auto warn_if = [&](const Annotation& ann, bool condition, const std::string& msg) {
        if (condition) warn_annotation(ann, msg);
    };

    for (const auto& ann : stmt->annotations) {
        bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                          ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
                          ann_is(ann, "inline") || ann_is(ann, "noinline");
        if (!recognized) continue;
        switch (stmt->kind) {
            case Stmt::Kind::FuncDecl:
                // All recognized annotations allowed on functions
                break;
            case Stmt::Kind::VarDecl:
                warn_if(ann, ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                 ann_is(ann, "nonreentrant") || ann_is(ann, "inline") || ann_is(ann, "noinline"),
                        "[[" + ann.name + "]] is only meaningful on functions");
                break;
            default:
                warn_if(ann, true, "[[" + ann.name + "]] is only supported on functions or globals");
                break;
        }
    }

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl: {
            for (const auto& param : stmt->params) {
                for (const auto& ann : param.annotations) {
                    bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                      ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
                                      ann_is(ann, "inline") || ann_is(ann, "noinline");
                    if (recognized) {
                        warn_annotation(ann, "[[" + ann.name + "]] is not used on parameters");
                    }
                }
            }
            if (stmt->body) {
                validate_expr_annotations(stmt->body);
            }
            break;
        }
        case Stmt::Kind::VarDecl:
            validate_expr_annotations(stmt->var_init);
            break;
        case Stmt::Kind::TypeDecl:
            for (const auto& field : stmt->fields) {
                for (const auto& ann : field.annotations) {
                    bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                      ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
                                      ann_is(ann, "inline") || ann_is(ann, "noinline");
                    if (recognized) {
                        warn_annotation(ann, "[[" + ann.name + "]] is not used on struct fields");
                    }
                }
            }
            break;
        case Stmt::Kind::Import:
            break;
        case Stmt::Kind::Expr:
            validate_expr_annotations(stmt->expr);
            break;
        case Stmt::Kind::Return:
            validate_expr_annotations(stmt->return_expr);
            break;
        case Stmt::Kind::ConditionalStmt:
            validate_expr_annotations(stmt->condition);
            validate_stmt_annotations(stmt->true_stmt);
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}
void TypeChecker::validate_expr_annotations(ExprPtr expr) {
    if (!expr) return;

    auto warn_all = [&](const Annotation& ann) {
        if (ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
            ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
            ann_is(ann, "inline") || ann_is(ann, "noinline")) {
            warn_annotation(ann, "[[" + ann.name + "]] is not used on expressions");
        }
    };
    for (const auto& ann : expr->annotations) {
        warn_all(ann);
    }

    validate_expr_annotations(expr->left);
    validate_expr_annotations(expr->right);
    validate_expr_annotations(expr->operand);
    validate_expr_annotations(expr->condition);
    validate_expr_annotations(expr->true_expr);
    validate_expr_annotations(expr->false_expr);
    for (const auto& arg : expr->args) validate_expr_annotations(arg);
    for (const auto& elem : expr->elements) validate_expr_annotations(elem);
    for (const auto& st : expr->statements) validate_stmt_annotations(st);
    validate_expr_annotations(expr->result_expr);
}

} // namespace vexel
