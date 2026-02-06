#include "analysis.h"
#include "ast_walk.h"
#include "evaluator.h"
#include "expr_access.h"
#include "optimizer.h"
#include "typechecker.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <tuple>

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

void Analyzer::analyze_reentrancy(const Module& /*mod*/, AnalysisFacts& facts) {
    Program* program = type_checker ? type_checker->get_program() : nullptr;
    if (!program) return;

    auto has_ann = [](const std::vector<Annotation>& anns, const std::string& name) {
        return std::any_of(anns.begin(), anns.end(), [&](const Annotation& a) { return a.name == name; });
    };

    std::unordered_map<const Symbol*, StmtPtr> function_map;
    std::unordered_set<const Symbol*> external_reentrant;
    std::unordered_set<const Symbol*> external_nonreentrant;

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (sym->is_external) {
                bool is_reentrant = has_ann(sym->declaration->annotations, "reentrant");
                bool is_nonreentrant = has_ann(sym->declaration->annotations, "nonreentrant");
                if (is_reentrant && is_nonreentrant) {
                    throw CompileError("Conflicting annotations: [[reentrant]] and [[nonreentrant]] on external function '" +
                                       sym->name + "'", sym->declaration->location);
                }
                if (is_reentrant) {
                    external_reentrant.insert(sym);
                } else {
                    external_nonreentrant.insert(sym);
                }
                continue;
            }
            if (!facts.reachable_functions.count(sym)) continue;
            function_map[sym] = sym->declaration;
        }
    }

    std::deque<std::pair<const Symbol*, char>> work;

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (!sym->is_exported) continue;
            if (!facts.reachable_functions.count(sym)) continue;

            bool is_reentrant = has_ann(sym->declaration->annotations, "reentrant");
            bool is_nonreentrant = has_ann(sym->declaration->annotations, "nonreentrant");
            if (is_reentrant && is_nonreentrant) {
                throw CompileError("Conflicting annotations: [[reentrant]] and [[nonreentrant]] on entry function '" +
                                   sym->name + "'", sym->declaration->location);
            }

            char ctx = (is_reentrant ? 'R' : 'N');
            if (facts.reentrancy_variants[sym].insert(ctx).second) {
                work.emplace_back(sym, ctx);
            }
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
            if (evaluated_at_compile_time) continue;
            std::unordered_set<const Symbol*> calls;
            collect_calls(sym->declaration->var_init, calls);
            for (const auto& callee_sym : calls) {
                if (!callee_sym) continue;
                if (facts.reentrancy_variants[callee_sym].insert('N').second) {
                    work.emplace_back(callee_sym, 'N');
                }
            }
        }
    }

    while (!work.empty()) {
        auto [func_sym, ctx] = work.front();
        work.pop_front();
        auto it = function_map.find(func_sym);
        if (it == function_map.end()) {
            if (ctx == 'R' && external_nonreentrant.count(func_sym)) {
                throw CompileError("Reentrant path calls non-reentrant external function '" + func_sym->name + "'",
                                   func_sym->declaration ? func_sym->declaration->location : SourceLocation());
            }
            continue;
        }
        StmtPtr func = it->second;
        if (!func || !func->body) continue;
        if (is_foldable(func_sym)) continue;

        int saved_instance = current_instance_id;
        current_instance_id = func_sym->instance_id;

        std::unordered_set<const Symbol*> calls;
        collect_calls(func->body, calls);
        for (const auto& callee_sym : calls) {
            if (!callee_sym) continue;
            if (ctx == 'R' && external_nonreentrant.count(callee_sym)) {
                throw CompileError("Reentrant path calls non-reentrant external function '" + callee_sym->name + "'",
                                   func->location);
            }
            if (facts.reentrancy_variants[callee_sym].insert(ctx).second) {
                work.emplace_back(callee_sym, ctx);
            }
        }

        current_instance_id = saved_instance;
    }

    for (const auto& entry : function_map) {
        const Symbol* func_sym = entry.first;
        if (facts.reentrancy_variants[func_sym].empty()) {
            facts.reentrancy_variants[func_sym].insert('N');
        }
    }
}

void Analyzer::analyze_mutability(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.var_mutability.clear();
    facts.receiver_mutates.clear();

    Program* program = type_checker ? type_checker->get_program() : nullptr;
    if (!program) return;

    std::unordered_map<const Symbol*, StmtPtr> function_map;
    std::unordered_map<const Symbol*, bool> global_written;

    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym) continue;
            if (sym->kind == Symbol::Kind::Function && sym->declaration) {
                function_map[sym] = sym->declaration;
                if (!sym->declaration->ref_params.empty()) {
                    std::vector<bool> mut(sym->declaration->ref_params.size(), false);
                    if (sym->is_external || !sym->declaration->body) {
                        std::fill(mut.begin(), mut.end(), true);
                    }
                    facts.receiver_mutates[sym] = mut;
                }
            } else if ((sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant) && !sym->is_local) {
                global_written[sym] = false;
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& entry : function_map) {
            const Symbol* func_sym = entry.first;
            const StmtPtr& func = entry.second;
            if (!func || func->is_external || !func->body || func->ref_params.empty()) continue;

            current_instance_id = func_sym->instance_id;

            std::vector<bool> updated = facts.receiver_mutates[func_sym];
            std::unordered_map<std::string, size_t> receiver_index;
            receiver_index.reserve(func->ref_params.size());
            for (size_t i = 0; i < func->ref_params.size(); i++) {
                receiver_index[func->ref_params[i]] = i;
            }

            std::function<void(ExprPtr)> visit_expr;
            std::function<void(StmtPtr)> visit_stmt;

            visit_expr = [&](ExprPtr expr) {
                if (!expr) return;
                switch (expr->kind) {
                    case Expr::Kind::Assignment: {
                        auto base = base_identifier_symbol(expr->left);
                        if (base) {
                            auto it = receiver_index.find((*base)->name);
                            if (it != receiver_index.end()) {
                                updated[it->second] = true;
                            }
                        }
                        visit_expr(expr->right);
                        break;
                    }
                    case Expr::Kind::Call: {
                        Symbol* callee_sym = nullptr;
                        if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                            callee_sym = binding_for(expr->operand);
                        }
                        auto callee_it = callee_sym ? facts.receiver_mutates.find(callee_sym) : facts.receiver_mutates.end();
                        for (size_t i = 0; i < expr->receivers.size(); i++) {
                            ExprPtr rec_expr = expr->receivers[i];
                            auto base = base_identifier_symbol(rec_expr);
                            if (!base) continue;
                            auto rec_it = receiver_index.find((*base)->name);
                            if (rec_it == receiver_index.end()) continue;
                            bool mut = true;
                            if (callee_it != facts.receiver_mutates.end() && i < callee_it->second.size()) {
                                mut = callee_it->second[i];
                            }
                            if (mut) {
                                updated[rec_it->second] = true;
                            }
                        }
                        for (const auto& rec : expr->receivers) {
                            visit_expr(rec);
                        }
                        for (const auto& arg : expr->args) {
                            visit_expr(arg);
                        }
                        visit_expr(expr->operand);
                        break;
                    }
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
                    case Stmt::Kind::Expr:
                        visit_expr(stmt->expr);
                        break;
                    case Stmt::Kind::Return:
                        visit_expr(stmt->return_expr);
                        break;
                    case Stmt::Kind::VarDecl:
                        visit_expr(stmt->var_init);
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

            if (updated != facts.receiver_mutates[func_sym]) {
                facts.receiver_mutates[func_sym] = updated;
                changed = true;
            }
        }
    }

    for (const auto& entry : function_map) {
        const Symbol* func_sym = entry.first;
        const StmtPtr& func = entry.second;
        if (!func || !func->body) continue;
        if (!facts.reachable_functions.count(func_sym)) continue;

        current_instance_id = func_sym->instance_id;

        std::function<void(ExprPtr)> visit_expr;
        std::function<void(StmtPtr)> visit_stmt;

        visit_expr = [&](ExprPtr expr) {
            if (!expr) return;
            switch (expr->kind) {
                case Expr::Kind::Assignment: {
                    auto base = base_identifier_symbol(expr->left);
                    if (base && !(*base)->is_local &&
                        ((*base)->kind == Symbol::Kind::Variable || (*base)->kind == Symbol::Kind::Constant)) {
                        global_written[*base] = true;
                    }
                    visit_expr(expr->right);
                    break;
                }
                case Expr::Kind::Call: {
                    Symbol* callee_sym = nullptr;
                    if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                        callee_sym = binding_for(expr->operand);
                    }
                    auto callee_it = callee_sym ? facts.receiver_mutates.find(callee_sym) : facts.receiver_mutates.end();
                    for (size_t i = 0; i < expr->receivers.size(); i++) {
                        bool mut = true;
                        if (callee_it != facts.receiver_mutates.end() && i < callee_it->second.size()) {
                            mut = callee_it->second[i];
                        }
                        if (!mut) continue;
                        ExprPtr rec_expr = expr->receivers[i];
                        if (!rec_expr) continue;
                        if (!is_addressable_lvalue(rec_expr) || !is_mutable_lvalue(rec_expr)) {
                            continue;
                        }
                        auto base = base_identifier_symbol(rec_expr);
                        if (base && !(*base)->is_local &&
                            ((*base)->kind == Symbol::Kind::Variable || (*base)->kind == Symbol::Kind::Constant)) {
                            global_written[*base] = true;
                        }
                    }
                    for (const auto& rec : expr->receivers) visit_expr(rec);
                    for (const auto& arg : expr->args) visit_expr(arg);
                    visit_expr(expr->operand);
                    break;
                }
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
                case Stmt::Kind::Expr:
                    visit_expr(stmt->expr);
                    break;
                case Stmt::Kind::Return:
                    visit_expr(stmt->return_expr);
                    break;
                case Stmt::Kind::VarDecl:
                    visit_expr(stmt->var_init);
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
    }

    for (const auto& entry : global_written) {
        const Symbol* sym = entry.first;
        bool written = entry.second;
        if (!sym || !sym->declaration) continue;
        bool effective_mutable = sym->is_mutable && written;
        if (effective_mutable) {
            facts.var_mutability[sym] = VarMutability::Mutable;
        } else {
            bool constexpr_init = false;
            if (sym->declaration->var_init) {
                if (sym->declaration->var_type && sym->declaration->var_type->kind == Type::Kind::Array &&
                    (sym->declaration->var_init->kind == Expr::Kind::ArrayLiteral ||
                     sym->declaration->var_init->kind == Expr::Kind::Range)) {
                    constexpr_init = true;
                } else if (type_checker) {
                    CompileTimeEvaluator evaluator(type_checker);
                    CTValue result;
                    if (evaluator.try_evaluate(sym->declaration->var_init, result)) {
                        constexpr_init = true;
                    }
                }
            }
            if (constexpr_init) {
                facts.var_mutability[sym] = VarMutability::Constexpr;
            } else {
                facts.var_mutability[sym] = VarMutability::NonMutableRuntime;
            }
        }
    }
}
void Analyzer::analyze_ref_variants(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.ref_variants.clear();

    Program* program = type_checker ? type_checker->get_program() : nullptr;
    if (!program) return;

    std::unordered_map<const Symbol*, StmtPtr> function_map;
    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) continue;
            function_map[sym] = sym->declaration;
        }
    }

    auto ref_variant_key = [&](const ExprPtr& call, size_t ref_count) {
        std::string key;
        key.reserve(ref_count);
        for (size_t i = 0; i < ref_count; i++) {
            bool is_mut = false;
            if (call && i < call->receivers.size()) {
                is_mut = receiver_is_mutable_arg(call->receivers[i]);
            }
            key.push_back(is_mut ? 'M' : 'N');
        }
        return key;
    };

    auto record_call = [&](ExprPtr expr) {
        if (!expr || expr->kind != Expr::Kind::Call) return;
        if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) return;
        Symbol* callee = binding_for(expr->operand);
        if (!callee) return;
        auto fit = function_map.find(callee);
        if (fit == function_map.end()) return;
        size_t ref_count = fit->second->ref_params.size();
        if (ref_count == 0) return;
        std::string key = ref_variant_key(expr, ref_count);
        facts.ref_variants[callee].insert(key);
    };

    std::function<void(ExprPtr)> visit_expr;
    std::function<void(StmtPtr)> visit_stmt;

    visit_expr = [&](ExprPtr expr) {
        if (!expr) return;
        switch (expr->kind) {
            case Expr::Kind::Call:
                record_call(expr);
                for (const auto& rec : expr->receivers) visit_expr(rec);
                for (const auto& arg : expr->args) visit_expr(arg);
                visit_expr(expr->operand);
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
                    } else if (expr->false_expr) {
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
            case Expr::Kind::Assignment:
                visit_expr(expr->left);
                visit_expr(expr->right);
                break;
            default:
                break;
        }
    };

    visit_stmt = [&](StmtPtr stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::Expr:
                visit_expr(stmt->expr);
                break;
            case Stmt::Kind::Return:
                visit_expr(stmt->return_expr);
                break;
            case Stmt::Kind::VarDecl:
                visit_expr(stmt->var_init);
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

    for (const auto& func_sym : facts.reachable_functions) {
        if (!func_sym || !func_sym->declaration) continue;
        if (is_foldable(func_sym)) continue;
        current_instance_id = func_sym->instance_id;
        if (func_sym->declaration->body) {
            visit_expr(func_sym->declaration->body);
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
                visit_expr(sym->declaration->var_init);
            }
        }
    }
}
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

    // Seed used globals: exported globals and globals referenced from reachable functions.
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
        // mark types from function signatures
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

    // Propagate used globals through initializers.
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
