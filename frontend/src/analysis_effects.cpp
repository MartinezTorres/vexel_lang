#include "analysis.h"

#include "expr_access.h"
#include "typechecker.h"

#include <algorithm>
#include <functional>

namespace vexel {

void Analyzer::analyze_effects(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.function_writes_global.clear();
    facts.function_is_pure.clear();

    Program* program = type_checker ? type_checker->get_program() : nullptr;
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
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (sym->is_external) {
                external_functions.insert(sym);
                continue;
            }
            if (!facts.reachable_functions.count(sym)) {
                continue;
            }
            function_map[sym] = sym->declaration;
            function_calls[sym] = {};
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

        current_instance_id = func_sym->instance_id;

        std::function<void(ExprPtr)> visit_expr;
        std::function<void(StmtPtr)> visit_stmt;

        visit_expr = [&](ExprPtr expr) {
            if (!expr) return;
            switch (expr->kind) {
                case Expr::Kind::Assignment: {
                    if (expr->creates_new_variable &&
                        expr->left && expr->left->kind == Expr::Kind::Identifier) {
                        // local declaration
                    } else {
                        auto base = base_identifier_symbol(expr->left);
                        if (base && !(*base)->is_local &&
                            ((*base)->kind == Symbol::Kind::Variable || (*base)->kind == Symbol::Kind::Constant) &&
                            (*base)->is_mutable) {
                            direct_write = true;
                        }
                    }
                    visit_expr(expr->right);
                    break;
                }
                case Expr::Kind::Call: {
                    if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
                        unknown_call = true;
                        direct_impure = true;
                    } else {
                        Symbol* callee_sym = binding_for(expr->operand);
                        if (!callee_sym) {
                            unknown_call = true;
                            direct_impure = true;
                        } else {
                            function_calls[func_sym].insert(callee_sym);
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
                        }
                    }
                    for (const auto& rec : expr->receivers) visit_expr(rec);
                    for (const auto& arg : expr->args) visit_expr(arg);
                    visit_expr(expr->operand);
                    break;
                }
                case Expr::Kind::Process:
                    direct_impure = true;
                    break;
                case Expr::Kind::Binary:
                    visit_expr(expr->left);
                    visit_expr(expr->right);
                    break;
                case Expr::Kind::Unary:
                case Expr::Kind::Cast:
                case Expr::Kind::Length:
                    visit_expr(expr->operand);
                    break;
                case Expr::Kind::Index:
                    visit_expr(expr->operand);
                    if (!expr->args.empty()) visit_expr(expr->args[0]);
                    break;
                case Expr::Kind::Member:
                    visit_expr(expr->operand);
                    break;
                case Expr::Kind::ArrayLiteral:
                case Expr::Kind::TupleLiteral:
                    for (const auto& elem : expr->elements) visit_expr(elem);
                    break;
                case Expr::Kind::Block:
                    for (const auto& stmt : expr->statements) visit_stmt(stmt);
                    visit_expr(expr->result_expr);
                    break;
                case Expr::Kind::Conditional:
                    if (auto cond = constexpr_condition(expr->condition)) {
                        if (cond.value()) {
                            visit_expr(expr->true_expr);
                        } else {
                            visit_expr(expr->false_expr);
                        }
                    } else {
                        visit_expr(expr->condition);
                        visit_expr(expr->true_expr);
                        visit_expr(expr->false_expr);
                    }
                    break;
                case Expr::Kind::Range:
                    visit_expr(expr->left);
                    visit_expr(expr->right);
                    break;
                case Expr::Kind::Iteration:
                case Expr::Kind::Repeat:
                    visit_expr(loop_subject(expr));
                    visit_expr(loop_body(expr));
                    break;
                default:
                    break;
            }
        };

        visit_stmt = [&](StmtPtr stmt) {
            if (!stmt) return;
            switch (stmt->kind) {
                case Stmt::Kind::VarDecl:
                    visit_expr(stmt->var_init);
                    break;
                case Stmt::Kind::Expr:
                    visit_expr(stmt->expr);
                    break;
                case Stmt::Kind::Return:
                    visit_expr(stmt->return_expr);
                    break;
                case Stmt::Kind::ConditionalStmt:
                    if (auto cond = constexpr_condition(stmt->condition)) {
                        if (cond.value()) {
                            visit_stmt(stmt->true_stmt);
                        }
                    } else {
                        visit_expr(stmt->condition);
                        visit_stmt(stmt->true_stmt);
                    }
                    break;
                default:
                    break;
            }
        };

        visit_expr(func->body);

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
