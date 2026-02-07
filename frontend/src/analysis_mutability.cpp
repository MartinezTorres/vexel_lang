#include "analysis.h"

#include "evaluator.h"
#include "expr_access.h"
#include "typechecker.h"

#include <algorithm>
#include <functional>

namespace vexel {

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

} // namespace vexel
