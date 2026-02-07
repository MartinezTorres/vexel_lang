#include "analysis.h"
#include "ast_walk.h"
#include "evaluator.h"
#include "expr_access.h"
#include "optimizer.h"
#include "typechecker.h"

#include <optional>

namespace vexel {

bool Analyzer::is_foldable(const Symbol* func_sym) const {
    if (!optimization) return false;
    return optimization->foldable_functions.count(func_sym) > 0;
}

std::optional<bool> Analyzer::constexpr_condition(ExprPtr expr) const {
    if (!expr || !optimization) return std::nullopt;
    auto it = optimization->constexpr_conditions.find(expr.get());
    if (it != optimization->constexpr_conditions.end()) {
        return it->second;
    }
    return std::nullopt;
}

Symbol* Analyzer::binding_for(ExprPtr expr) const {
    if (!type_checker) return nullptr;
    return type_checker->binding_for(current_instance_id, expr.get());
}

bool Analyzer::is_addressable_lvalue(ExprPtr expr) const {
    if (!expr) return false;
    switch (expr->kind) {
        case Expr::Kind::Identifier:
            return true;
        case Expr::Kind::Member:
        case Expr::Kind::Index:
            return is_addressable_lvalue(expr->operand);
        default:
            return false;
    }
}

bool Analyzer::is_mutable_lvalue(ExprPtr expr) const {
    if (!expr) return false;
    switch (expr->kind) {
        case Expr::Kind::Identifier: {
            Symbol* sym = binding_for(expr);
            return sym && sym->is_mutable;
        }
        case Expr::Kind::Member:
        case Expr::Kind::Index:
            return is_mutable_lvalue(expr->operand);
        default:
            return false;
    }
}

bool Analyzer::receiver_is_mutable_arg(ExprPtr expr) const {
    return is_addressable_lvalue(expr) && is_mutable_lvalue(expr);
}

std::optional<const Symbol*> Analyzer::base_identifier_symbol(ExprPtr expr) const {
    while (expr) {
        if (expr->kind == Expr::Kind::Identifier) {
            if (Symbol* sym = binding_for(expr)) {
                return sym;
            }
            return std::nullopt;
        }
        if (expr->kind == Expr::Kind::Member || expr->kind == Expr::Kind::Index) {
            expr = expr->operand;
            continue;
        }
        break;
    }
    return std::nullopt;
}

AnalysisFacts Analyzer::run(const Module& mod) {
    AnalysisFacts facts;
    analyze_reachability(mod, facts);
    analyze_reentrancy(mod, facts);
    analyze_mutability(mod, facts);
    analyze_ref_variants(mod, facts);
    analyze_effects(mod, facts);
    analyze_usage(mod, facts);
    return facts;
}

void Analyzer::analyze_reachability(const Module& mod, AnalysisFacts& facts) {
    Program* program = type_checker ? type_checker->get_program() : nullptr;
    if (!program) return;

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (!sym->is_exported) continue;
            mark_reachable(sym, mod, facts);
        }
    }

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Constant)) continue;
            if (!sym->declaration || !sym->declaration->var_init) continue;
            bool evaluated_at_compile_time = false;
            if (type_checker) {
                CompileTimeEvaluator evaluator(type_checker);
                CTValue result;
                if (evaluator.try_evaluate(sym->declaration->var_init, result)) {
                    evaluated_at_compile_time = true;
                }
            }
            if (!evaluated_at_compile_time) {
                std::unordered_set<const Symbol*> calls;
                collect_calls(sym->declaration->var_init, calls);
                for (const Symbol* callee : calls) {
                    mark_reachable(callee, mod, facts);
                }
            }
        }
    }
}

void Analyzer::mark_reachable(const Symbol* func_sym,
                              const Module& mod, AnalysisFacts& facts) {
    if (!func_sym) return;
    if (facts.reachable_functions.count(func_sym)) {
        return;
    }

    facts.reachable_functions.insert(func_sym);

    if (func_sym->kind != Symbol::Kind::Function || !func_sym->declaration || func_sym->declaration->is_external) {
        return;
    }

    if (is_foldable(func_sym)) {
        return;
    }

    int saved_instance = current_instance_id;
    current_instance_id = func_sym->instance_id;

    std::unordered_set<const Symbol*> calls;
    collect_calls(func_sym->declaration->body, calls);
    for (const auto& called_sym : calls) {
        mark_reachable(called_sym, mod, facts);
    }

    current_instance_id = saved_instance;
}

void Analyzer::collect_calls_stmt(StmtPtr stmt, std::unordered_set<const Symbol*>& calls) {
    if (!stmt) return;
    if (stmt->kind == Stmt::Kind::ConditionalStmt) {
        if (auto cond = constexpr_condition(stmt->condition)) {
            if (cond.value()) {
                collect_calls_stmt(stmt->true_stmt, calls);
            }
        } else {
            collect_calls(stmt->condition, calls);
            collect_calls_stmt(stmt->true_stmt, calls);
        }
        return;
    }

    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& child) { collect_calls(child, calls); },
        [&](const StmtPtr& child) { collect_calls_stmt(child, calls); });
}

void Analyzer::collect_calls(ExprPtr expr, std::unordered_set<const Symbol*>& calls) {
    if (!expr) return;

    if (expr->kind == Expr::Kind::Call &&
        expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
        Symbol* sym = binding_for(expr->operand);
        if (sym && sym->kind == Symbol::Kind::Function) {
            calls.insert(sym);
        }
    }

    if (expr->kind == Expr::Kind::Conditional) {
        auto cond = constexpr_condition(expr->condition);
        if (cond.has_value()) {
            if (cond.value()) {
                collect_calls(expr->true_expr, calls);
            } else if (expr->false_expr) {
                collect_calls(expr->false_expr, calls);
            }
        } else {
            collect_calls(expr->condition, calls);
            collect_calls(expr->true_expr, calls);
            if (expr->false_expr) collect_calls(expr->false_expr, calls);
        }
        return;
    }

    for_each_expr_child(
        expr,
        [&](const ExprPtr& child) { collect_calls(child, calls); },
        [&](const StmtPtr& child) { collect_calls_stmt(child, calls); });
}

} // namespace vexel
