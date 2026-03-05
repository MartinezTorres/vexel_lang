#pragma once

#include "ast_walk.h"
#include "bindings.h"

namespace vexel {

inline void unbind_type_tree(Bindings& bindings, int instance_id, const TypePtr& type);
inline void unbind_expr_tree(Bindings& bindings, int instance_id, const ExprPtr& expr);
inline void unbind_stmt_tree(Bindings& bindings, int instance_id, const StmtPtr& stmt);

inline void unbind_type_tree(Bindings& bindings, int instance_id, const TypePtr& type) {
    if (!type) return;
    bindings.unbind(instance_id, type.get());
    if (type->kind == Type::Kind::Array) {
        unbind_type_tree(bindings, instance_id, type->element_type);
        unbind_expr_tree(bindings, instance_id, type->array_size);
    } else if (type->kind == Type::Kind::TypeOf) {
        unbind_expr_tree(bindings, instance_id, type->typeof_expr);
    }
}

inline void unbind_expr_tree(Bindings& bindings, int instance_id, const ExprPtr& expr) {
    if (!expr) return;
    bindings.unbind(instance_id, expr.get());
    if (expr->target_type) {
        unbind_type_tree(bindings, instance_id, expr->target_type);
    }
    for_each_expr_child(
        expr,
        [&](const ExprPtr& child) { unbind_expr_tree(bindings, instance_id, child); },
        [&](const StmtPtr& child) { unbind_stmt_tree(bindings, instance_id, child); });
}

inline void unbind_stmt_tree(Bindings& bindings, int instance_id, const StmtPtr& stmt) {
    if (!stmt) return;
    bindings.unbind(instance_id, stmt.get());

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            for (const auto& ref : stmt->ref_params) {
                bindings.unbind(instance_id, &ref);
            }
            for (const auto& param : stmt->params) {
                bindings.unbind(instance_id, &param);
                unbind_type_tree(bindings, instance_id, param.type);
            }
            unbind_type_tree(bindings, instance_id, stmt->return_type);
            for (const auto& ret_type : stmt->return_types) {
                unbind_type_tree(bindings, instance_id, ret_type);
            }
            for (const auto& ref_type : stmt->ref_param_types) {
                unbind_type_tree(bindings, instance_id, ref_type);
            }
            break;
        case Stmt::Kind::VarDecl:
            unbind_type_tree(bindings, instance_id, stmt->var_type);
            break;
        case Stmt::Kind::TypeDecl:
            for (const auto& field : stmt->fields) {
                unbind_type_tree(bindings, instance_id, field.type);
            }
            break;
        default:
            break;
    }

    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& child) { unbind_expr_tree(bindings, instance_id, child); },
        [&](const StmtPtr& child) { unbind_stmt_tree(bindings, instance_id, child); });
}

} // namespace vexel
