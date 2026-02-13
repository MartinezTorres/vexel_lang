#include "annotation_validator.h"
#include "ast_walk.h"
#include "common.h"

#include <unordered_set>

namespace vexel {

namespace {

const std::unordered_set<std::string>& known_annotations() {
    static const std::unordered_set<std::string> kKnown = {
        "nonreentrant",
        "nonbanked",
    };
    return kKnown;
}

void validate_list(const std::vector<Annotation>& anns) {
    const auto& known = known_annotations();
    for (const auto& ann : anns) {
        if (!known.count(ann.name)) {
            throw CompileError("Unknown annotation: [[" + ann.name + "]]", ann.location);
        }
    }
}

void validate_expr_annotations(const ExprPtr& expr);
void validate_stmt_annotations(const StmtPtr& stmt);

void validate_expr_annotations(const ExprPtr& expr) {
    if (!expr) return;
    validate_list(expr->annotations);
    for_each_expr_child(
        expr,
        [&](const ExprPtr& child_expr) { validate_expr_annotations(child_expr); },
        [&](const StmtPtr& child_stmt) { validate_stmt_annotations(child_stmt); });
}

void validate_stmt_annotations(const StmtPtr& stmt) {
    if (!stmt) return;
    validate_list(stmt->annotations);
    if (stmt->kind == Stmt::Kind::FuncDecl) {
        for (const auto& param : stmt->params) {
            validate_list(param.annotations);
        }
    } else if (stmt->kind == Stmt::Kind::TypeDecl) {
        for (const auto& field : stmt->fields) {
            validate_list(field.annotations);
        }
    }
    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& child_expr) { validate_expr_annotations(child_expr); },
        [&](const StmtPtr& child_stmt) { validate_stmt_annotations(child_stmt); });
}

} // namespace

void validate_annotations(const Module& mod) {
    for (const auto& stmt : mod.top_level) {
        validate_stmt_annotations(stmt);
    }
}

} // namespace vexel
