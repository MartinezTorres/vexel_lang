#include "analysis.h"
#include "evaluator.h"
#include "function_key.h"
#include "typechecker.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <tuple>

namespace vexel {

namespace {
bool is_addressable_lvalue(ExprPtr expr) {
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

bool is_mutable_lvalue(ExprPtr expr) {
    if (!expr) return false;
    switch (expr->kind) {
        case Expr::Kind::Identifier:
            return expr->is_mutable_binding;
        case Expr::Kind::Member:
        case Expr::Kind::Index:
            return is_mutable_lvalue(expr->operand);
        default:
            return false;
    }
}

bool receiver_is_mutable_arg(ExprPtr expr) {
    return is_addressable_lvalue(expr) && is_mutable_lvalue(expr);
}

std::string base_identifier(ExprPtr expr) {
    while (expr) {
        if (expr->kind == Expr::Kind::Identifier) {
            return expr->name;
        }
        if (expr->kind == Expr::Kind::Member || expr->kind == Expr::Kind::Index) {
            expr = expr->operand;
            continue;
        }
        break;
    }
    return "";
}

std::optional<std::tuple<std::string, int, bool>> base_identifier_info(ExprPtr expr) {
    while (expr) {
        if (expr->kind == Expr::Kind::Identifier) {
            return std::make_tuple(expr->name, expr->scope_instance_id, expr->is_mutable_binding);
        }
        if (expr->kind == Expr::Kind::Member || expr->kind == Expr::Kind::Index) {
            expr = expr->operand;
            continue;
        }
        break;
    }
    return std::nullopt;
}
} // namespace

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
    // Start from all exported functions
    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl && stmt->is_exported) {
            std::string func_name = stmt->func_name;
            if (!stmt->type_namespace.empty()) {
                func_name = stmt->type_namespace + "::" + stmt->func_name;
            }
            mark_reachable(func_name, stmt->scope_instance_id, mod, facts);
        }
    }

    // Also mark any global variable initializers that reference functions
    // BUT only if they weren't compile-time evaluated
    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::VarDecl && stmt->var_init) {
            bool evaluated_at_compile_time = false;
            if (type_checker) {
                CompileTimeEvaluator evaluator(type_checker);
                CTValue result;
                if (evaluator.try_evaluate(stmt->var_init, result)) {
                    evaluated_at_compile_time = true;
                }
            }

            if (!evaluated_at_compile_time) {
                std::unordered_set<std::string> calls;
                collect_calls(stmt->var_init, calls);
                for (const auto& func_key : calls) {
                    std::string func_name;
                    int scope_id = -1;
                    split_reachability_key(func_key, func_name, scope_id);
                    mark_reachable(func_name, scope_id, mod, facts);
                }
            }
        }
    }
}

void Analyzer::mark_reachable(const std::string& func_name, int scope_id,
                              const Module& mod, AnalysisFacts& facts) {
    std::string key = reachability_key(func_name, scope_id);
    if (facts.reachable_functions.count(key)) {
        return;
    }

    facts.reachable_functions.insert(key);

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl && !stmt->is_external) {
            std::string stmt_func_name = stmt->func_name;
            if (!stmt->type_namespace.empty()) {
                stmt_func_name = stmt->type_namespace + "::" + stmt->func_name;
            }

            if (stmt_func_name == func_name) {
                if (scope_id >= 0) {
                    if (stmt->scope_instance_id != scope_id) continue;
                } else if (stmt->scope_instance_id >= 0) {
                    continue;
                }
                if (stmt->body) {
                    std::unordered_set<std::string> calls;
                    collect_calls(stmt->body, calls);
                    for (const auto& called_key : calls) {
                        std::string called_name;
                        int called_scope = -1;
                        split_reachability_key(called_key, called_name, called_scope);
                        mark_reachable(called_name, called_scope, mod, facts);
                    }
                }
                break;
            }
        }
    }
}

void Analyzer::collect_calls(ExprPtr expr, std::unordered_set<std::string>& calls) {
    if (!expr) return;

    switch (expr->kind) {
        case Expr::Kind::Call:
            if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                calls.insert(reachability_key(expr->operand->name, expr->operand->scope_instance_id));
            }
            for (const auto& rec : expr->receivers) {
                collect_calls(rec, calls);
            }
            for (const auto& arg : expr->args) {
                collect_calls(arg, calls);
            }
            collect_calls(expr->operand, calls);
            break;

        case Expr::Kind::Binary:
            collect_calls(expr->left, calls);
            collect_calls(expr->right, calls);
            break;

        case Expr::Kind::Unary:
            collect_calls(expr->operand, calls);
            break;

        case Expr::Kind::Index:
            collect_calls(expr->operand, calls);
            if (!expr->args.empty()) collect_calls(expr->args[0], calls);
            break;

        case Expr::Kind::Member:
            collect_calls(expr->operand, calls);
            break;

        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) {
                collect_calls(elem, calls);
            }
            break;

        case Expr::Kind::Block:
            for (const auto& stmt : expr->statements) {
                if (stmt->kind == Stmt::Kind::Expr && stmt->expr) {
                    collect_calls(stmt->expr, calls);
                } else if (stmt->kind == Stmt::Kind::Return && stmt->return_expr) {
                    collect_calls(stmt->return_expr, calls);
                } else if (stmt->kind == Stmt::Kind::VarDecl && stmt->var_init) {
                    collect_calls(stmt->var_init, calls);
                }
            }
            if (expr->result_expr) {
                collect_calls(expr->result_expr, calls);
            }
            break;

        case Expr::Kind::Conditional:
            collect_calls(expr->condition, calls);
            collect_calls(expr->true_expr, calls);
            if (expr->false_expr) collect_calls(expr->false_expr, calls);
            break;

        case Expr::Kind::Cast:
            collect_calls(expr->operand, calls);
            break;

        case Expr::Kind::Assignment:
            collect_calls(expr->left, calls);
            collect_calls(expr->right, calls);
            break;

        case Expr::Kind::Range:
            collect_calls(expr->left, calls);
            collect_calls(expr->right, calls);
            break;

        case Expr::Kind::Length:
            collect_calls(expr->operand, calls);
            break;

        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
            collect_calls(expr->left, calls);
            if (expr->right) collect_calls(expr->right, calls);
            break;

        default:
            break;
    }
}

void Analyzer::analyze_reentrancy(const Module& mod, AnalysisFacts& facts) {
    auto has_ann = [](const std::vector<Annotation>& anns, const std::string& name) {
        return std::any_of(anns.begin(), anns.end(), [&](const Annotation& a) { return a.name == name; });
    };

    auto qualified_name = [](const StmtPtr& stmt) {
        if (!stmt->type_namespace.empty()) {
            return stmt->type_namespace + "::" + stmt->func_name;
        }
        return stmt->func_name;
    };

    std::unordered_map<std::string, StmtPtr> function_map;
    std::unordered_set<std::string> external_reentrant;
    std::unordered_set<std::string> external_nonreentrant;

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind != Stmt::Kind::FuncDecl) continue;
        std::string func_name = qualified_name(stmt);
        std::string key = reachability_key(func_name, stmt->scope_instance_id);
        if (stmt->is_external) {
            bool is_reentrant = has_ann(stmt->annotations, "reentrant");
            bool is_nonreentrant = has_ann(stmt->annotations, "nonreentrant");
            if (is_reentrant && is_nonreentrant) {
                throw CompileError("Conflicting annotations: [[reentrant]] and [[nonreentrant]] on external function '" +
                                   stmt->func_name + "'", stmt->location);
            }
            if (is_reentrant) {
                external_reentrant.insert(key);
            } else {
                external_nonreentrant.insert(key);
            }
            continue;
        }
        if (!facts.reachable_functions.count(key)) continue;
        function_map[key] = stmt;
    }

    std::deque<std::pair<std::string, char>> work;

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind != Stmt::Kind::FuncDecl) continue;
        if (!stmt->is_exported) continue;
        std::string func_name = qualified_name(stmt);
        std::string key = reachability_key(func_name, stmt->scope_instance_id);
        if (!facts.reachable_functions.count(key)) continue;

        bool is_reentrant = has_ann(stmt->annotations, "reentrant");
        bool is_nonreentrant = has_ann(stmt->annotations, "nonreentrant");
        if (is_reentrant && is_nonreentrant) {
            throw CompileError("Conflicting annotations: [[reentrant]] and [[nonreentrant]] on entry function '" +
                               stmt->func_name + "'", stmt->location);
        }

        char ctx = (is_reentrant ? 'R' : 'N');
        if (facts.reentrancy_variants[key].insert(ctx).second) {
            work.emplace_back(key, ctx);
        }
    }

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::VarDecl && stmt->var_init) {
            bool evaluated_at_compile_time = false;
            if (type_checker) {
                CompileTimeEvaluator evaluator(type_checker);
                CTValue result;
                if (evaluator.try_evaluate(stmt->var_init, result)) {
                    evaluated_at_compile_time = true;
                }
            }
            if (evaluated_at_compile_time) continue;
            std::unordered_set<std::string> calls;
            collect_calls(stmt->var_init, calls);
            for (const auto& callee_key : calls) {
                if (function_map.count(callee_key) == 0) continue;
                if (facts.reentrancy_variants[callee_key].insert('N').second) {
                    work.emplace_back(callee_key, 'N');
                }
            }
        }
    }

    while (!work.empty()) {
        auto [func_key, ctx] = work.front();
        work.pop_front();
        auto it = function_map.find(func_key);
        if (it == function_map.end()) continue;
        StmtPtr func = it->second;
        if (!func || !func->body) continue;

        std::unordered_set<std::string> calls;
        collect_calls(func->body, calls);
        for (const auto& callee_key : calls) {
            auto callee_it = function_map.find(callee_key);
            if (callee_it == function_map.end()) {
                if (ctx == 'R' && external_nonreentrant.count(callee_key)) {
                    std::string callee_name;
                    int callee_scope = -1;
                    split_reachability_key(callee_key, callee_name, callee_scope);
                    if (callee_scope >= 0) {
                        callee_name += " (scope " + std::to_string(callee_scope) + ")";
                    }
                    throw CompileError("Reentrant path calls non-reentrant external function '" + callee_name + "'",
                                       func->location);
                }
                continue;
            }
            if (facts.reentrancy_variants[callee_key].insert(ctx).second) {
                work.emplace_back(callee_key, ctx);
            }
        }
    }

    for (const auto& entry : function_map) {
        const std::string& func_key = entry.first;
        if (facts.reentrancy_variants[func_key].empty()) {
            facts.reentrancy_variants[func_key].insert('N');
        }
    }
}

void Analyzer::analyze_mutability(const Module& mod, AnalysisFacts& facts) {
    facts.var_mutability.clear();
    facts.receiver_mutates.clear();
    std::unordered_map<std::string, StmtPtr> function_map;
    std::unordered_map<const Stmt*, StmtPtr> var_decl_map;
    std::unordered_map<const Stmt*, bool> var_written;

    auto qualified_name = [](const StmtPtr& stmt) {
        if (!stmt->type_namespace.empty()) {
            return stmt->type_namespace + "::" + stmt->func_name;
        }
        return stmt->func_name;
    };

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            function_map[qualified_name(stmt)] = stmt;
            if (!stmt->ref_params.empty()) {
                std::vector<bool> mut(stmt->ref_params.size(), false);
                if (stmt->is_external || !stmt->body) {
                    std::fill(mut.begin(), mut.end(), true);
                }
                facts.receiver_mutates[qualified_name(stmt)] = mut;
            }
        } else if (stmt->kind == Stmt::Kind::VarDecl) {
            var_decl_map[stmt.get()] = stmt;
            var_written[stmt.get()] = false;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& entry : function_map) {
            const std::string& func_name = entry.first;
            const StmtPtr& func = entry.second;
            if (func->is_external || !func->body || func->ref_params.empty()) continue;

            std::vector<bool> updated = facts.receiver_mutates[func_name];
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
                        std::string base = base_identifier(expr->left);
                        auto it = receiver_index.find(base);
                        if (it != receiver_index.end()) {
                            updated[it->second] = true;
                        }
                        visit_expr(expr->right);
                        break;
                    }
                    case Expr::Kind::Call: {
                        if (!expr->receivers.empty()) {
                            std::string callee;
                            if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                                callee = expr->operand->name;
                            }
                            auto callee_it = facts.receiver_mutates.find(callee);
                            for (size_t i = 0; i < expr->receivers.size(); i++) {
                                ExprPtr rec_expr = expr->receivers[i];
                                std::string rec_name = base_identifier(rec_expr);
                                if (rec_name.empty()) continue;
                                auto rec_it = receiver_index.find(rec_name);
                                if (rec_it == receiver_index.end()) continue;
                                bool mut = true;
                                if (callee_it != facts.receiver_mutates.end() && i < callee_it->second.size()) {
                                    mut = callee_it->second[i];
                                }
                                if (mut) {
                                    updated[rec_it->second] = true;
                                }
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
                        visit_expr(expr->condition);
                        visit_expr(expr->true_expr);
                        visit_expr(expr->false_expr);
                        break;
                    case Expr::Kind::Range:
                    case Expr::Kind::Iteration:
                    case Expr::Kind::Repeat:
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
                        visit_expr(stmt->condition);
                        visit_stmt(stmt->true_stmt);
                        break;
                    default:
                        break;
                }
            };

            visit_expr(func->body);

            if (updated != facts.receiver_mutates[func_name]) {
                facts.receiver_mutates[func_name] = updated;
                changed = true;
            }
        }
    }

    struct ScopeFrame {
        std::unordered_map<std::string, const Stmt*> vars;
    };
    std::vector<ScopeFrame> scopes;
    scopes.emplace_back();

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::VarDecl) {
            std::string key = reachability_key(stmt->var_name, stmt->scope_instance_id);
            scopes.back().vars[key] = stmt.get();
        }
    }

    auto resolve_var = [&](const std::string& name, int scope_id) -> const Stmt* {
        std::string key = reachability_key(name, scope_id);
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->vars.find(key);
            if (found != it->vars.end()) {
                return found->second;
            }
        }
        return nullptr;
    };

    auto mark_written = [&](const std::string& name, int scope_id) {
        const Stmt* decl = resolve_var(name, scope_id);
        if (decl) {
            var_written[decl] = true;
        }
    };

    std::function<void(ExprPtr)> visit_expr;
    std::function<void(StmtPtr)> visit_stmt;

    visit_expr = [&](ExprPtr expr) {
        if (!expr) return;
        switch (expr->kind) {
            case Expr::Kind::Assignment: {
                auto info = base_identifier_info(expr->left);
                if (info) {
                    mark_written(std::get<0>(*info), std::get<1>(*info));
                }
                visit_expr(expr->right);
                break;
            }
            case Expr::Kind::Call: {
                if (!expr->receivers.empty()) {
                    std::string callee;
                    if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                        callee = expr->operand->name;
                    }
                    auto callee_it = facts.receiver_mutates.find(callee);
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
                        auto info = base_identifier_info(rec_expr);
                        if (info) {
                            mark_written(std::get<0>(*info), std::get<1>(*info));
                        }
                    }
                }
                for (const auto& rec : expr->receivers) {
                    visit_expr(rec);
                }
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
                scopes.emplace_back();
                for (const auto& stmt : expr->statements) visit_stmt(stmt);
                visit_expr(expr->result_expr);
                scopes.pop_back();
                break;
            case Expr::Kind::Conditional:
                visit_expr(expr->condition);
                visit_expr(expr->true_expr);
                visit_expr(expr->false_expr);
                break;
            case Expr::Kind::Range:
            case Expr::Kind::Iteration:
            case Expr::Kind::Repeat:
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
            case Stmt::Kind::VarDecl:
                scopes.back().vars[reachability_key(stmt->var_name, stmt->scope_instance_id)] = stmt.get();
                var_decl_map[stmt.get()] = stmt;
                var_written.emplace(stmt.get(), false);
                visit_expr(stmt->var_init);
                break;
            case Stmt::Kind::Expr:
                visit_expr(stmt->expr);
                break;
            case Stmt::Kind::Return:
                visit_expr(stmt->return_expr);
                break;
            case Stmt::Kind::ConditionalStmt:
                visit_expr(stmt->condition);
                visit_stmt(stmt->true_stmt);
                break;
            default:
                break;
        }
    };

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl && stmt->body) {
            scopes.emplace_back();
            for (const auto& param : stmt->params) {
                scopes.back().vars[param.name] = nullptr;
            }
            for (const auto& ref : stmt->ref_params) {
                scopes.back().vars[ref] = nullptr;
            }
            visit_expr(stmt->body);
            scopes.pop_back();
        } else if (stmt->kind == Stmt::Kind::Expr) {
            visit_expr(stmt->expr);
        }
    }

    for (const auto& entry : var_decl_map) {
        const Stmt* key = entry.first;
        StmtPtr decl = entry.second;
        bool written = var_written[key];
        bool effective_mutable = decl->is_mutable && written;
        if (effective_mutable) {
            facts.var_mutability[key] = VarMutability::Mutable;
        } else {
            bool constexpr_init = false;
            if (decl->var_init) {
                if (decl->var_type && decl->var_type->kind == Type::Kind::Array &&
                    (decl->var_init->kind == Expr::Kind::ArrayLiteral || decl->var_init->kind == Expr::Kind::Range)) {
                    constexpr_init = true;
                } else if (type_checker) {
                    CompileTimeEvaluator evaluator(type_checker);
                    CTValue result;
                    if (evaluator.try_evaluate(decl->var_init, result)) {
                        constexpr_init = true;
                    }
                }
            }
            if (constexpr_init) {
                facts.var_mutability[key] = VarMutability::Constexpr;
            } else {
                facts.var_mutability[key] = VarMutability::NonMutableRuntime;
            }
        }
    }
}

void Analyzer::analyze_ref_variants(const Module& mod, AnalysisFacts& facts) {
    facts.ref_variants.clear();
    std::unordered_map<std::string, StmtPtr> function_map;

    auto qualified_name = [](const StmtPtr& stmt) {
        if (!stmt->type_namespace.empty()) {
            return stmt->type_namespace + "::" + stmt->func_name;
        }
        return stmt->func_name;
    };

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            function_map[qualified_name(stmt)] = stmt;
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
        const std::string& func_name = expr->operand->name;
        auto fit = function_map.find(func_name);
        if (fit == function_map.end()) return;
        size_t ref_count = fit->second->ref_params.size();
        if (ref_count == 0) return;
        std::string key = ref_variant_key(expr, ref_count);
        facts.ref_variants[func_name].insert(key);
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
                visit_expr(expr->condition);
                visit_expr(expr->true_expr);
                visit_expr(expr->false_expr);
                break;
            case Expr::Kind::Range:
            case Expr::Kind::Iteration:
            case Expr::Kind::Repeat:
                visit_expr(expr->left);
                visit_expr(expr->right);
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
                visit_expr(stmt->condition);
                visit_stmt(stmt->true_stmt);
                break;
            default:
                break;
        }
    };

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            std::string func_name = qualified_name(stmt);
            std::string key = reachability_key(func_name, stmt->scope_instance_id);
            if (!facts.reachable_functions.count(key)) {
                continue;
            }
            if (stmt->body) visit_expr(stmt->body);
        } else if (stmt->kind == Stmt::Kind::VarDecl && stmt->var_init) {
            bool evaluated_at_compile_time = false;
            if (type_checker) {
                CompileTimeEvaluator evaluator(type_checker);
                CTValue result;
                if (evaluator.try_evaluate(stmt->var_init, result)) {
                    evaluated_at_compile_time = true;
                }
            }
            if (!evaluated_at_compile_time) {
                visit_expr(stmt->var_init);
            }
        }
    }
}

void Analyzer::analyze_effects(const Module& mod, AnalysisFacts& facts) {
    facts.function_writes_global.clear();
    facts.function_is_pure.clear();

    std::unordered_map<std::string, StmtPtr> function_map;
    std::unordered_map<std::string, std::unordered_set<std::string>> function_calls;
    std::unordered_map<std::string, bool> function_direct_writes_global;
    std::unordered_map<std::string, bool> function_direct_impure;
    std::unordered_map<std::string, bool> function_unknown_call;
    std::unordered_map<std::string, bool> function_mutates_receiver;
    std::unordered_set<std::string> external_functions;

    auto qualified_name = [](const StmtPtr& stmt) {
        if (!stmt->type_namespace.empty()) {
            return stmt->type_namespace + "::" + stmt->func_name;
        }
        return stmt->func_name;
    };

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind != Stmt::Kind::FuncDecl) continue;
        std::string func_name = qualified_name(stmt);
        std::string func_key = reachability_key(func_name, stmt->scope_instance_id);
        if (stmt->is_external) {
            external_functions.insert(func_key);
            continue;
        }
        if (!facts.reachable_functions.count(func_key)) {
            continue;
        }
        function_map[func_key] = stmt;
        function_calls[func_key] = {};
        function_direct_writes_global[func_key] = false;
        function_direct_impure[func_key] = false;
        function_unknown_call[func_key] = false;

        bool mutates = false;
        auto mut_it = facts.receiver_mutates.find(func_name);
        if (mut_it != facts.receiver_mutates.end()) {
            mutates = std::any_of(mut_it->second.begin(), mut_it->second.end(),
                                  [](bool v) { return v; });
        }
        function_mutates_receiver[func_key] = mutates;
    }

    for (const auto& entry : function_map) {
        const std::string& func_key = entry.first;
        const StmtPtr& func = entry.second;
        if (!func->body) {
            function_direct_impure[func_key] = true;
            function_unknown_call[func_key] = true;
            continue;
        }

        struct ScopeFrame {
            std::unordered_set<std::string> vars;
        };
        std::vector<ScopeFrame> scopes;
        scopes.emplace_back();
        for (const auto& param : func->params) {
            scopes.back().vars.insert(param.name);
        }
        for (const auto& ref : func->ref_params) {
            scopes.back().vars.insert(ref);
        }

        auto is_local = [&](const std::string& name) {
            for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                if (it->vars.count(name)) {
                    return true;
                }
            }
            return false;
        };

        bool direct_write = false;
        bool direct_impure = false;
        bool unknown_call = false;

        std::function<void(ExprPtr)> visit_expr;
        std::function<void(StmtPtr)> visit_stmt;

        visit_expr = [&](ExprPtr expr) {
            if (!expr) return;
            switch (expr->kind) {
                case Expr::Kind::Assignment: {
                    if (expr->creates_new_variable &&
                        expr->left && expr->left->kind == Expr::Kind::Identifier) {
                        scopes.back().vars.insert(expr->left->name);
                    } else {
                        auto base = base_identifier_info(expr->left);
                        if (base) {
                            const std::string& name = std::get<0>(*base);
                            bool is_mut = std::get<2>(*base);
                            if (is_mut && !is_local(name)) {
                                direct_write = true;
                            }
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
                        std::string callee_key = reachability_key(expr->operand->name, expr->operand->scope_instance_id);
                        function_calls[func_key].insert(callee_key);

                        auto callee_it = facts.receiver_mutates.find(expr->operand->name);
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
                            auto base = base_identifier_info(rec);
                            if (base) {
                                const std::string& name = std::get<0>(*base);
                                if (!is_local(name)) {
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
                    scopes.emplace_back();
                    for (const auto& stmt : expr->statements) visit_stmt(stmt);
                    visit_expr(expr->result_expr);
                    scopes.pop_back();
                    break;
                case Expr::Kind::Conditional:
                    visit_expr(expr->condition);
                    visit_expr(expr->true_expr);
                    visit_expr(expr->false_expr);
                    break;
                case Expr::Kind::Range:
                case Expr::Kind::Iteration:
                case Expr::Kind::Repeat:
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
                case Stmt::Kind::VarDecl:
                    scopes.back().vars.insert(stmt->var_name);
                    visit_expr(stmt->var_init);
                    break;
                case Stmt::Kind::Expr:
                    visit_expr(stmt->expr);
                    break;
                case Stmt::Kind::Return:
                    visit_expr(stmt->return_expr);
                    break;
                case Stmt::Kind::ConditionalStmt:
                    visit_expr(stmt->condition);
                    visit_stmt(stmt->true_stmt);
                    break;
                default:
                    break;
            }
        };

        visit_expr(func->body);

        function_direct_writes_global[func_key] = direct_write;
        function_direct_impure[func_key] = direct_impure;
        function_unknown_call[func_key] = unknown_call;
    }

    for (const auto& entry : function_map) {
        const std::string& func_key = entry.first;
        facts.function_writes_global[func_key] = function_direct_writes_global[func_key] ||
                                                 function_unknown_call[func_key];
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& entry : function_map) {
            const std::string& func_key = entry.first;
            bool writes = function_direct_writes_global[func_key] || function_unknown_call[func_key];
            if (!writes) {
                for (const auto& callee_key : function_calls[func_key]) {
                    if (external_functions.count(callee_key) || !function_map.count(callee_key)) {
                        writes = true;
                        break;
                    }
                    if (facts.function_writes_global[callee_key]) {
                        writes = true;
                        break;
                    }
                }
            }
            if (writes != facts.function_writes_global[func_key]) {
                facts.function_writes_global[func_key] = writes;
                changed = true;
            }
        }
    }

    for (const auto& entry : function_map) {
        const std::string& func_key = entry.first;
        bool base = !facts.function_writes_global[func_key] &&
                    !function_direct_impure[func_key] &&
                    !function_mutates_receiver[func_key];
        facts.function_is_pure[func_key] = base;
    }

    changed = true;
    while (changed) {
        changed = false;
        for (const auto& entry : function_map) {
            const std::string& func_key = entry.first;
            bool base = !facts.function_writes_global[func_key] &&
                        !function_direct_impure[func_key] &&
                        !function_mutates_receiver[func_key];
            bool pure = base;
            if (pure) {
                for (const auto& callee_key : function_calls[func_key]) {
                    if (external_functions.count(callee_key) || !function_map.count(callee_key)) {
                        pure = false;
                        break;
                    }
                    if (!facts.function_is_pure[callee_key]) {
                        pure = false;
                        break;
                    }
                }
            }
            if (pure != facts.function_is_pure[func_key]) {
                facts.function_is_pure[func_key] = pure;
                changed = true;
            }
        }
    }
}

void Analyzer::analyze_usage(const Module& mod, AnalysisFacts& facts) {
    facts.used_global_vars.clear();
    facts.used_type_names.clear();

    std::unordered_map<std::string, StmtPtr> global_vars;
    std::unordered_map<std::string, StmtPtr> type_decls;

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::VarDecl) {
            std::string key = reachability_key(stmt->var_name, stmt->scope_instance_id);
            global_vars[key] = stmt;
        } else if (stmt->kind == Stmt::Kind::TypeDecl) {
            type_decls[stmt->type_decl_name] = stmt;
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

    std::function<void(ExprPtr)> visit_expr_types;
    std::function<void(StmtPtr)> visit_stmt_types;

    visit_expr_types = [&](ExprPtr expr) {
        if (!expr) return;
        mark_type(expr->type);
        switch (expr->kind) {
            case Expr::Kind::Binary:
                visit_expr_types(expr->left);
                visit_expr_types(expr->right);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                visit_expr_types(expr->operand);
                break;
            case Expr::Kind::Call:
                visit_expr_types(expr->operand);
                for (const auto& rec : expr->receivers) visit_expr_types(rec);
                for (const auto& arg : expr->args) visit_expr_types(arg);
                break;
            case Expr::Kind::Index:
                visit_expr_types(expr->operand);
                if (!expr->args.empty()) visit_expr_types(expr->args[0]);
                break;
            case Expr::Kind::Member:
                visit_expr_types(expr->operand);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) visit_expr_types(elem);
                break;
            case Expr::Kind::Block:
                for (const auto& stmt : expr->statements) visit_stmt_types(stmt);
                visit_expr_types(expr->result_expr);
                break;
            case Expr::Kind::Conditional:
                visit_expr_types(expr->condition);
                visit_expr_types(expr->true_expr);
                visit_expr_types(expr->false_expr);
                break;
            case Expr::Kind::Assignment:
                visit_expr_types(expr->left);
                visit_expr_types(expr->right);
                break;
            case Expr::Kind::Range:
            case Expr::Kind::Iteration:
            case Expr::Kind::Repeat:
                visit_expr_types(expr->left);
                visit_expr_types(expr->right);
                break;
            default:
                break;
        }
    };

    visit_stmt_types = [&](StmtPtr stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::VarDecl:
                mark_type(stmt->var_type);
                visit_expr_types(stmt->var_init);
                break;
            case Stmt::Kind::Expr:
                visit_expr_types(stmt->expr);
                break;
            case Stmt::Kind::Return:
                visit_expr_types(stmt->return_expr);
                break;
            case Stmt::Kind::ConditionalStmt:
                visit_expr_types(stmt->condition);
                visit_stmt_types(stmt->true_stmt);
                break;
            default:
                break;
        }
    };

    struct ScopeFrame {
        std::unordered_set<std::string> vars;
    };

    auto is_local = [&](const std::string& name, const std::vector<ScopeFrame>& scopes) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            if (it->vars.count(name)) return true;
        }
        return false;
    };

    std::deque<StmtPtr> global_worklist;

    auto mark_global = [&](const std::string& name, int scope_id) {
        std::string key = reachability_key(name, scope_id);
        auto it = global_vars.find(key);
        if (it == global_vars.end()) return;
        if (facts.used_global_vars.insert(it->second.get()).second) {
            global_worklist.push_back(it->second);
        }
    };

    std::function<void(ExprPtr, std::vector<ScopeFrame>&)> visit_expr_globals;
    std::function<void(StmtPtr, std::vector<ScopeFrame>&)> visit_stmt_globals;

    visit_expr_globals = [&](ExprPtr expr, std::vector<ScopeFrame>& scopes) {
        if (!expr) return;
        switch (expr->kind) {
            case Expr::Kind::Identifier:
                if (!is_local(expr->name, scopes)) {
                    mark_global(expr->name, expr->scope_instance_id);
                }
                break;
            case Expr::Kind::Assignment:
                if (expr->creates_new_variable &&
                    expr->left && expr->left->kind == Expr::Kind::Identifier) {
                    scopes.back().vars.insert(expr->left->name);
                }
                visit_expr_globals(expr->left, scopes);
                visit_expr_globals(expr->right, scopes);
                break;
            case Expr::Kind::Binary:
                visit_expr_globals(expr->left, scopes);
                visit_expr_globals(expr->right, scopes);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                visit_expr_globals(expr->operand, scopes);
                break;
            case Expr::Kind::Call:
                visit_expr_globals(expr->operand, scopes);
                for (const auto& rec : expr->receivers) visit_expr_globals(rec, scopes);
                for (const auto& arg : expr->args) visit_expr_globals(arg, scopes);
                break;
            case Expr::Kind::Index:
                visit_expr_globals(expr->operand, scopes);
                if (!expr->args.empty()) visit_expr_globals(expr->args[0], scopes);
                break;
            case Expr::Kind::Member:
                visit_expr_globals(expr->operand, scopes);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) visit_expr_globals(elem, scopes);
                break;
            case Expr::Kind::Block:
                scopes.emplace_back();
                for (const auto& stmt : expr->statements) visit_stmt_globals(stmt, scopes);
                visit_expr_globals(expr->result_expr, scopes);
                scopes.pop_back();
                break;
            case Expr::Kind::Conditional:
                visit_expr_globals(expr->condition, scopes);
                visit_expr_globals(expr->true_expr, scopes);
                visit_expr_globals(expr->false_expr, scopes);
                break;
            case Expr::Kind::Range:
            case Expr::Kind::Iteration:
            case Expr::Kind::Repeat:
                visit_expr_globals(expr->left, scopes);
                visit_expr_globals(expr->right, scopes);
                break;
            default:
                break;
        }
    };

    visit_stmt_globals = [&](StmtPtr stmt, std::vector<ScopeFrame>& scopes) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::VarDecl:
                scopes.back().vars.insert(stmt->var_name);
                visit_expr_globals(stmt->var_init, scopes);
                break;
            case Stmt::Kind::Expr:
                visit_expr_globals(stmt->expr, scopes);
                break;
            case Stmt::Kind::Return:
                visit_expr_globals(stmt->return_expr, scopes);
                break;
            case Stmt::Kind::ConditionalStmt:
                visit_expr_globals(stmt->condition, scopes);
                visit_stmt_globals(stmt->true_stmt, scopes);
                break;
            default:
                break;
        }
    };

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            std::string func_name = stmt->func_name;
            if (!stmt->type_namespace.empty()) {
                func_name = stmt->type_namespace + "::" + stmt->func_name;
                add_type_name(stmt->type_namespace);
            }
            std::string key = reachability_key(func_name, stmt->scope_instance_id);
            if (!facts.reachable_functions.count(key)) {
                continue;
            }

            mark_type(stmt->return_type);
            for (const auto& t : stmt->return_types) {
                mark_type(t);
            }
            for (const auto& param : stmt->params) {
                mark_type(param.type);
            }
            for (const auto& ref_type : stmt->ref_param_types) {
                mark_type(ref_type);
            }

            if (stmt->body) {
                visit_expr_types(stmt->body);
                std::vector<ScopeFrame> scopes;
                scopes.emplace_back();
                for (const auto& param : stmt->params) {
                    scopes.back().vars.insert(param.name);
                }
                for (const auto& ref : stmt->ref_params) {
                    scopes.back().vars.insert(ref);
                }
                visit_expr_globals(stmt->body, scopes);
            }
        } else if (stmt->kind == Stmt::Kind::Expr) {
            visit_expr_types(stmt->expr);
            std::vector<ScopeFrame> scopes;
            scopes.emplace_back();
            visit_expr_globals(stmt->expr, scopes);
        }
    }

    while (!global_worklist.empty()) {
        StmtPtr global = global_worklist.front();
        global_worklist.pop_front();
        if (!global) continue;
        mark_type(global->var_type);
        visit_expr_types(global->var_init);
        std::vector<ScopeFrame> scopes;
        scopes.emplace_back();
        visit_expr_globals(global->var_init, scopes);
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
