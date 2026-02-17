#include "analysis.h"
#include "ast_walk.h"
#include "optimizer.h"
#include "program.h"
#include "typechecker.h"

#include <optional>

namespace vexel {

Analyzer::AnalysisContext Analyzer::context() const {
    AnalysisContext ctx;
    ctx.program = type_checker ? type_checker->get_program() : nullptr;
    return ctx;
}

Analyzer::InstanceScope Analyzer::scoped_instance(int instance_id) {
    return InstanceScope(*this, instance_id);
}

bool Analyzer::pass_enabled(AnalysisPass pass) const {
    return (analysis_config.enabled_passes & static_cast<uint32_t>(pass)) != 0;
}

bool Analyzer::global_initializer_runs_at_runtime(const Symbol* sym) const {
    if (!sym || !sym->declaration || !sym->declaration->var_init) {
        return false;
    }
    if (!optimization) {
        return true;
    }
    const ExprFactKey key = expr_fact_key(sym->instance_id, sym->declaration->var_init.get());
    return optimization->constexpr_values.count(key) == 0;
}

void Analyzer::build_run_summary(const AnalysisFacts& facts) {
    run_summary_ = AnalysisRunSummary{};
    run_summary_.program = context().program;
    if (!run_summary_.program) {
        return;
    }

    for (const auto& instance : run_summary_.program->instances) {
        [[maybe_unused]] auto instance_scope = scoped_instance(instance.id);

        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || !sym->declaration) {
                continue;
            }

            if (sym->kind == Symbol::Kind::Function) {
                if (!sym->is_external && facts.reachable_functions.count(sym)) {
                    run_summary_.reachable_function_decls[sym] = sym->declaration;
                    if (sym->declaration->body) {
                        std::unordered_set<const Symbol*> calls;
                        collect_calls(sym->declaration->body, calls);
                        run_summary_.reachable_calls[sym] = std::move(calls);
                    }
                }
                continue;
            }

            if (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Constant) {
                continue;
            }
            if (!global_initializer_runs_at_runtime(sym)) {
                continue;
            }

            run_summary_.runtime_initialized_globals.insert(sym);
            std::unordered_set<const Symbol*> calls;
            collect_calls(sym->declaration->var_init, calls);
            run_summary_.global_initializer_calls[sym] = std::move(calls);
        }
    }
}

std::optional<bool> Analyzer::constexpr_condition(ExprPtr expr) const {
    if (!expr || !optimization) return std::nullopt;
    auto it = optimization->constexpr_conditions.find(expr_fact_key(current_instance_id, expr.get()));
    if (it != optimization->constexpr_conditions.end()) {
        return it->second;
    }
    return std::nullopt;
}

void Analyzer::walk_pruned_expr(ExprPtr expr, const ExprVisitor& on_expr, const StmtVisitor& on_stmt) {
    if (!expr) return;
    on_expr(expr);

    if (expr->kind == Expr::Kind::Conditional) {
        if (auto cond = constexpr_condition(expr->condition)) {
            if (cond.value()) {
                walk_pruned_expr(expr->true_expr, on_expr, on_stmt);
            } else if (expr->false_expr) {
                walk_pruned_expr(expr->false_expr, on_expr, on_stmt);
            }
        } else {
            walk_pruned_expr(expr->condition, on_expr, on_stmt);
            walk_pruned_expr(expr->true_expr, on_expr, on_stmt);
            if (expr->false_expr) {
                walk_pruned_expr(expr->false_expr, on_expr, on_stmt);
            }
        }
        return;
    }

    for_each_expr_child(
        expr,
        [&](const ExprPtr& child) { walk_pruned_expr(child, on_expr, on_stmt); },
        [&](const StmtPtr& child) { walk_pruned_stmt(child, on_expr, on_stmt); });
}

void Analyzer::walk_pruned_stmt(StmtPtr stmt, const ExprVisitor& on_expr, const StmtVisitor& on_stmt) {
    if (!stmt) return;
    on_stmt(stmt);

    if (stmt->kind == Stmt::Kind::ConditionalStmt) {
        if (auto cond = constexpr_condition(stmt->condition)) {
            if (cond.value()) {
                walk_pruned_stmt(stmt->true_stmt, on_expr, on_stmt);
            }
        } else {
            walk_pruned_expr(stmt->condition, on_expr, on_stmt);
            walk_pruned_stmt(stmt->true_stmt, on_expr, on_stmt);
        }
        return;
    }

    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& child) { walk_pruned_expr(child, on_expr, on_stmt); },
        [&](const StmtPtr& child) { walk_pruned_stmt(child, on_expr, on_stmt); });
}

void Analyzer::walk_runtime_expr(ExprPtr expr, const ExprVisitor& on_expr, const StmtVisitor& on_stmt) {
    walk_pruned_expr(expr, on_expr, on_stmt);
}

void Analyzer::walk_runtime_stmt(StmtPtr stmt, const ExprVisitor& on_expr, const StmtVisitor& on_stmt) {
    walk_pruned_stmt(stmt, on_expr, on_stmt);
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
    run_summary_ = AnalysisRunSummary{};

    const bool needs_reachability =
        pass_enabled(AnalysisPass::Reachability) ||
        pass_enabled(AnalysisPass::Reentrancy) ||
        pass_enabled(AnalysisPass::Mutability) ||
        pass_enabled(AnalysisPass::RefVariants) ||
        pass_enabled(AnalysisPass::Effects) ||
        pass_enabled(AnalysisPass::Usage);
    if (needs_reachability) {
        analyze_reachability(mod, facts);
        build_run_summary(facts);
    }

    if (pass_enabled(AnalysisPass::Reentrancy)) {
        analyze_reentrancy(mod, facts);
    }

    const bool needs_mutability =
        pass_enabled(AnalysisPass::Mutability) ||
        pass_enabled(AnalysisPass::RefVariants) ||
        pass_enabled(AnalysisPass::Effects);
    if (needs_mutability) {
        analyze_mutability(mod, facts);
    }

    if (pass_enabled(AnalysisPass::RefVariants)) {
        analyze_ref_variants(mod, facts);
    }
    if (pass_enabled(AnalysisPass::Effects)) {
        analyze_effects(mod, facts);
    }
    if (pass_enabled(AnalysisPass::Usage)) {
        analyze_usage(mod, facts);
    }
    return facts;
}

void Analyzer::analyze_reachability(const Module& /*mod*/, AnalysisFacts& facts) {
    Program* program = context().program;
    if (!program) return;

    for (const auto& instance : program->instances) {
        [[maybe_unused]] auto instance_scope = scoped_instance(instance.id);
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (!sym->is_exported) continue;
            mark_reachable(sym, facts);
        }
    }

    for (const auto& instance : program->instances) {
        [[maybe_unused]] auto instance_scope = scoped_instance(instance.id);
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Constant)) continue;
            if (!sym->declaration || !sym->declaration->var_init) continue;
            if (!global_initializer_runs_at_runtime(sym)) continue;

            std::unordered_set<const Symbol*> calls;
            collect_calls(sym->declaration->var_init, calls);
            for (const Symbol* callee : calls) {
                mark_reachable(callee, facts);
            }
        }
    }
}

void Analyzer::mark_reachable(const Symbol* func_sym, AnalysisFacts& facts) {
    if (!func_sym) return;
    if (facts.reachable_functions.count(func_sym)) {
        return;
    }

    facts.reachable_functions.insert(func_sym);

    if (func_sym->kind != Symbol::Kind::Function || !func_sym->declaration || func_sym->declaration->is_external) {
        return;
    }

    [[maybe_unused]] auto callee_scope = scoped_instance(func_sym->instance_id);

    std::unordered_set<const Symbol*> calls;
    collect_calls(func_sym->declaration->body, calls);
    for (const auto& called_sym : calls) {
        mark_reachable(called_sym, facts);
    }

}

void Analyzer::collect_calls(ExprPtr expr, std::unordered_set<const Symbol*>& calls) {
    walk_pruned_expr(
        expr,
        [&](ExprPtr node) {
            if (!node) return;
            if (node->kind != Expr::Kind::Call || !node->operand ||
                node->operand->kind != Expr::Kind::Identifier) {
                return;
            }
            Symbol* sym = binding_for(node->operand);
            if (sym && sym->kind == Symbol::Kind::Function) {
                calls.insert(sym);
            }
        },
        [&](StmtPtr) {});
}

} // namespace vexel
