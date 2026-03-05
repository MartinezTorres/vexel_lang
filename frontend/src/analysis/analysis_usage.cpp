#include "analysis.h"

#include "typechecker.h"

#include <algorithm>
#include <deque>
#include <functional>

namespace vexel {

void Analyzer::analyze_usage(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.used_global_vars.clear();
    facts.used_type_names.clear();

    Program* program = context().program;
    if (!program) return;

    std::unordered_map<std::string, StmtPtr> type_decls;
    for (const auto& mod_info : program->modules) {
        for (const auto& stmt : mod_info.module.top_level) {
            if (stmt->kind == Stmt::Kind::TypeDecl) {
                type_decls[stmt->type_decl_name] = stmt;
            }
        }
    }

    std::deque<std::string> type_worklist;
    auto add_type_name = [&](const std::string& name) {
        if (name.empty()) return;
        if (facts.used_type_names.insert(name).second) {
            type_worklist.push_back(name);
        }
    };

    std::function<void(TypePtr)> mark_type;
    mark_type = [&](TypePtr type) {
        if (!type) return;
        switch (type->kind) {
            case Type::Kind::Named:
                add_type_name(type->type_name);
                break;
            case Type::Kind::Array:
                mark_type(type->element_type);
                break;
            case Type::Kind::Primitive:
            case Type::Kind::TypeVar:
            case Type::Kind::TypeOf:
            default:
                break;
            }
    };

    auto mark_expr_types = [&](const ExprPtr& expr) {
        if (!expr) return;
        mark_type(expr->type);
        mark_type(expr->declared_var_type);
        mark_type(expr->target_type);
        if (expr->kind == Expr::Kind::Identifier) {
            if (Symbol* sym = binding_for(expr)) {
                mark_type(sym->type);
            }
        }
    };

    auto mark_stmt_types = [&](const StmtPtr& stmt) {
        if (!stmt) return;
        if (stmt->kind == Stmt::Kind::VarDecl) {
            mark_type(stmt->var_type);
        }
    };

    std::deque<const Symbol*> global_worklist;
    auto note_global = [&](const Symbol* sym) {
        if (!sym) return;
        if (facts.used_global_vars.insert(sym).second) {
            global_worklist.push_back(sym);
        }
    };

    // Exported globals are ABI roots and must always be retained.
    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || !sym->is_exported || sym->is_local) continue;
            if (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Constant) continue;
            note_global(sym);
        }
    }

    for (const auto& func_sym : facts.reachable_functions) {
        if (!func_sym || !func_sym->declaration || !func_sym->declaration->body) continue;
        auto func_scope = scoped_instance(func_sym->instance_id);
        (void)func_scope;
        walk_runtime_expr(
            func_sym->declaration->body,
            [&](ExprPtr expr) {
                mark_expr_types(expr);
                if (!expr || expr->kind != Expr::Kind::Identifier) return;
                Symbol* sym = binding_for(expr);
                if (sym && !sym->is_local &&
                    (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant)) {
                    note_global(sym);
                }
            },
            [&](StmtPtr stmt) { mark_stmt_types(stmt); });
        for (const auto& param : func_sym->declaration->params) {
            mark_type(param.type);
        }
        for (const auto& ref_type : func_sym->declaration->ref_param_types) {
            mark_type(ref_type);
        }
        if (func_sym->declaration->return_type) {
            mark_type(func_sym->declaration->return_type);
        }
        for (const auto& rt : func_sym->declaration->return_types) {
            mark_type(rt);
        }
    }

    while (!global_worklist.empty()) {
        const Symbol* sym = global_worklist.front();
        global_worklist.pop_front();
        if (!sym || !sym->declaration) continue;
        auto global_scope = scoped_instance(sym->instance_id);
        (void)global_scope;
        mark_type(sym->declaration->var_type);
        walk_runtime_expr(
            sym->declaration->var_init,
            [&](ExprPtr expr) {
                mark_expr_types(expr);
                if (!expr || expr->kind != Expr::Kind::Identifier) return;
                Symbol* used = binding_for(expr);
                if (used && !used->is_local &&
                    (used->kind == Symbol::Kind::Variable || used->kind == Symbol::Kind::Constant)) {
                    note_global(used);
                }
            },
            [&](StmtPtr stmt) { mark_stmt_types(stmt); });
    }

    while (!type_worklist.empty()) {
        std::string type_name = type_worklist.front();
        type_worklist.pop_front();
        auto it = type_decls.find(type_name);
        if (it == type_decls.end()) continue;
        const StmtPtr& decl = it->second;
        for (const auto& field : decl->fields) {
            mark_type(field.type);
        }
    }
}

} // namespace vexel
