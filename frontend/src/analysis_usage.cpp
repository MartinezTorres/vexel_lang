#include "analysis.h"

#include "expr_access.h"
#include "typechecker.h"

#include <algorithm>
#include <deque>
#include <functional>

namespace vexel {

void Analyzer::analyze_usage(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.used_global_vars.clear();
    facts.used_type_names.clear();

    Program* program = type_checker ? type_checker->get_program() : nullptr;
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
            default:
                break;
        }
    };

    std::deque<const Symbol*> global_worklist;
    auto note_global = [&](const Symbol* sym) {
        if (!sym) return;
        if (facts.used_global_vars.insert(sym).second) {
            global_worklist.push_back(sym);
        }
    };

    std::function<void(ExprPtr)> visit_expr_globals;
    std::function<void(StmtPtr)> visit_stmt_globals;
    visit_expr_globals = [&](ExprPtr expr) {
        if (!expr) return;
        if (expr->kind == Expr::Kind::Identifier) {
            Symbol* sym = binding_for(expr);
            if (sym && !sym->is_local && (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant)) {
                note_global(sym);
            }
        }
        switch (expr->kind) {
            case Expr::Kind::Binary:
                visit_expr_globals(expr->left);
                visit_expr_globals(expr->right);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                visit_expr_globals(expr->operand);
                break;
            case Expr::Kind::Call:
                for (const auto& rec : expr->receivers) visit_expr_globals(rec);
                for (const auto& arg : expr->args) visit_expr_globals(arg);
                visit_expr_globals(expr->operand);
                break;
            case Expr::Kind::Index:
                visit_expr_globals(expr->operand);
                if (!expr->args.empty()) visit_expr_globals(expr->args[0]);
                break;
            case Expr::Kind::Member:
                visit_expr_globals(expr->operand);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) visit_expr_globals(elem);
                break;
            case Expr::Kind::Block:
                for (const auto& stmt : expr->statements) {
                    visit_stmt_globals(stmt);
                }
                visit_expr_globals(expr->result_expr);
                break;
            case Expr::Kind::Conditional:
                visit_expr_globals(expr->condition);
                visit_expr_globals(expr->true_expr);
                visit_expr_globals(expr->false_expr);
                break;
            case Expr::Kind::Range:
                visit_expr_globals(expr->left);
                visit_expr_globals(expr->right);
                break;
            case Expr::Kind::Iteration:
            case Expr::Kind::Repeat:
                visit_expr_globals(loop_subject(expr));
                visit_expr_globals(loop_body(expr));
                break;
            default:
                break;
        }
    };

    visit_stmt_globals = [&](StmtPtr stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::Expr:
                visit_expr_globals(stmt->expr);
                break;
            case Stmt::Kind::Return:
                visit_expr_globals(stmt->return_expr);
                break;
            case Stmt::Kind::VarDecl:
                visit_expr_globals(stmt->var_init);
                break;
            case Stmt::Kind::ConditionalStmt:
                visit_expr_globals(stmt->condition);
                visit_stmt_globals(stmt->true_stmt);
                break;
            default:
                break;
        }
    };

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->is_local) continue;
            if (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant) {
                bool exported = false;
                if (sym->declaration) {
                    exported = std::any_of(sym->declaration->annotations.begin(), sym->declaration->annotations.end(),
                                           [](const Annotation& a) { return a.name == "export"; });
                }
                if (exported) {
                    note_global(sym);
                }
            }
        }
    }

    for (const auto& func_sym : facts.reachable_functions) {
        if (!func_sym || !func_sym->declaration || !func_sym->declaration->body) continue;
        current_instance_id = func_sym->instance_id;
        visit_expr_globals(func_sym->declaration->body);
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
        current_instance_id = sym->instance_id;
        mark_type(sym->declaration->var_type);
        visit_expr_globals(sym->declaration->var_init);
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
