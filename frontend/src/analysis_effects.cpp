#include "analysis.h"

#include "typechecker.h"

#include <algorithm>

namespace vexel {

void Analyzer::analyze_effects(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.function_writes_global.clear();
    facts.function_is_pure.clear();

    const AnalysisRunSummary& summary = run_summary();
    Program* program = summary.program;
    if (!program) return;

    std::unordered_map<const Symbol*, StmtPtr> function_map;
    std::unordered_map<const Symbol*, std::unordered_set<const Symbol*>> function_calls;
    std::unordered_map<const Symbol*, bool> function_direct_writes_global;
    std::unordered_map<const Symbol*, bool> function_direct_impure;
    std::unordered_map<const Symbol*, bool> function_unknown_call;
    std::unordered_map<const Symbol*, bool> function_mutates_receiver;
    std::unordered_set<const Symbol*> external_functions;

    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (sym && sym->kind == Symbol::Kind::Function && sym->is_external) {
                external_functions.insert(sym);
            }
        }
    }

    for (const auto& entry : summary.reachable_function_decls) {
        const Symbol* sym = entry.first;
        function_map[sym] = entry.second;
        auto calls_it = summary.reachable_calls.find(sym);
        function_calls[sym] = (calls_it != summary.reachable_calls.end())
                                  ? calls_it->second
                                  : std::unordered_set<const Symbol*>{};
        function_direct_writes_global[sym] = false;
        function_direct_impure[sym] = false;
        function_unknown_call[sym] = false;

        bool mutates = false;
        auto mut_it = facts.receiver_mutates.find(sym);
        if (mut_it != facts.receiver_mutates.end()) {
            mutates = std::any_of(mut_it->second.begin(), mut_it->second.end(),
                                  [](bool v) { return v; });
        }
        function_mutates_receiver[sym] = mutates;
    }

    for (const auto& entry : function_map) {
        const Symbol* func_sym = entry.first;
        const StmtPtr& func = entry.second;
        if (is_foldable(func_sym)) {
            function_direct_writes_global[func_sym] = false;
            function_direct_impure[func_sym] = false;
            function_unknown_call[func_sym] = false;
            continue;
        }
        if (!func || !func->body) {
            function_direct_impure[func_sym] = true;
            function_unknown_call[func_sym] = true;
            continue;
        }

        bool direct_write = false;
        bool direct_impure = false;
        bool unknown_call = false;

        auto func_scope = scoped_instance(func_sym->instance_id);
        (void)func_scope;

        walk_pruned_expr(
            func->body,
            [&](ExprPtr expr) {
                if (!expr) return;
                if (expr->kind == Expr::Kind::Assignment) {
                    if (expr->creates_new_variable &&
                        expr->left && expr->left->kind == Expr::Kind::Identifier) {
                        return;
                    }
                    auto base = base_identifier_symbol(expr->left);
                    if (base && !(*base)->is_local &&
                        ((*base)->kind == Symbol::Kind::Variable || (*base)->kind == Symbol::Kind::Constant) &&
                        (*base)->is_mutable) {
                        direct_write = true;
                    }
                    return;
                }

                if (expr->kind == Expr::Kind::Call) {
                    if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
                        unknown_call = true;
                        direct_impure = true;
                        return;
                    }
                    Symbol* callee_sym = binding_for(expr->operand);
                    if (!callee_sym) {
                        unknown_call = true;
                        direct_impure = true;
                        return;
                    }

                    auto callee_it = facts.receiver_mutates.find(callee_sym);
                    for (size_t i = 0; i < expr->receivers.size(); i++) {
                        bool mut = true;
                        if (callee_it != facts.receiver_mutates.end() && i < callee_it->second.size()) {
                            mut = callee_it->second[i];
                        }
                        if (!mut) continue;
                        ExprPtr rec = expr->receivers[i];
                        if (!receiver_is_mutable_arg(rec)) {
                            continue;
                        }
                        auto base = base_identifier_symbol(rec);
                        if (base && !(*base)->is_local &&
                            ((*base)->kind == Symbol::Kind::Variable || (*base)->kind == Symbol::Kind::Constant)) {
                            direct_write = true;
                        }
                    }
                    return;
                }

                if (expr->kind == Expr::Kind::Process) {
                    direct_impure = true;
                }
            },
            [&](StmtPtr) {});

        function_direct_writes_global[func_sym] = direct_write;
        function_direct_impure[func_sym] = direct_impure;
        function_unknown_call[func_sym] = unknown_call;
    }

    for (const auto& entry : function_map) {
        const Symbol* func_sym = entry.first;
        facts.function_writes_global[func_sym] = function_direct_writes_global[func_sym] ||
                                                 function_unknown_call[func_sym];
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& entry : function_map) {
            const Symbol* func_sym = entry.first;
            bool writes = function_direct_writes_global[func_sym] || function_unknown_call[func_sym];
            if (!writes) {
                for (const auto& callee_sym : function_calls[func_sym]) {
                    if (external_functions.count(callee_sym) || !function_map.count(callee_sym)) {
                        writes = true;
                        break;
                    }
                    if (facts.function_writes_global[callee_sym]) {
                        writes = true;
                        break;
                    }
                }
            }
            if (writes != facts.function_writes_global[func_sym]) {
                facts.function_writes_global[func_sym] = writes;
                changed = true;
            }
        }
    }

    for (const auto& entry : function_map) {
        const Symbol* func_sym = entry.first;
        bool base = !facts.function_writes_global[func_sym] &&
                    !function_direct_impure[func_sym] &&
                    !function_mutates_receiver[func_sym];
        facts.function_is_pure[func_sym] = base;
    }

    changed = true;
    while (changed) {
        changed = false;
        for (const auto& entry : function_map) {
            const Symbol* func_sym = entry.first;
            bool base = !facts.function_writes_global[func_sym] &&
                        !function_direct_impure[func_sym] &&
                        !function_mutates_receiver[func_sym];
            bool pure = base;
            if (pure) {
                for (const auto& callee_sym : function_calls[func_sym]) {
                    if (external_functions.count(callee_sym) || !function_map.count(callee_sym)) {
                        pure = false;
                        break;
                    }
                    if (!facts.function_is_pure[callee_sym]) {
                        pure = false;
                        break;
                    }
                }
            }
            if (pure != facts.function_is_pure[func_sym]) {
                facts.function_is_pure[func_sym] = pure;
                changed = true;
            }
        }
    }
}

} // namespace vexel
