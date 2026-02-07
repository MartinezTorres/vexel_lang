#include "type_use_validator.h"
#include "common.h"
#include "expr_access.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

namespace {

bool type_is_concrete(const TypeUseContext* ctx, TypePtr type) {
    if (!type) return false;
    if (ctx && ctx->resolve_type) {
        type = ctx->resolve_type(type);
    }
    switch (type->kind) {
        case Type::Kind::TypeVar:
            return false;
        case Type::Kind::Array:
            return type_is_concrete(ctx, type->element_type);
        default:
            return true;
    }
}

std::string qualified_func_name(const Symbol* sym) {
    if (!sym) return "";
    return sym->name;
}

const Symbol* bound_callee(const TypeUseContext* ctx, int instance_id, ExprPtr call_expr) {
    if (!ctx || !ctx->binding || !call_expr || call_expr->kind != Expr::Kind::Call ||
        !call_expr->operand || call_expr->operand->kind != Expr::Kind::Identifier) {
        return nullptr;
    }
    return ctx->binding(instance_id, call_expr->operand);
}

template <typename ArgFn>
void for_each_call_value_arg(const Symbol* callee, ExprPtr call_expr, const ArgFn& fn) {
    if (!call_expr) return;
    size_t param_index = 0;
    for (const auto& arg : call_expr->args) {
        bool is_expr_param = callee && callee->kind == Symbol::Kind::Function && callee->declaration &&
                             param_index < callee->declaration->params.size() &&
                             callee->declaration->params[param_index].is_expression_param;
        if (!is_expr_param) {
            fn(arg);
        }
        ++param_index;
    }
}

template <typename StmtFn, typename ExprFn>
void visit_function_body(const ExprPtr& body, bool result_required, const StmtFn& on_stmt, const ExprFn& on_expr) {
    if (!body) return;
    if (body->kind == Expr::Kind::Block) {
        for (const auto& stmt : body->statements) {
            on_stmt(stmt);
        }
        on_expr(body->result_expr, result_required);
        return;
    }
    on_expr(body, result_required);
}

struct CallCollector {
    const TypeUseContext* ctx = nullptr;
    int instance_id = -1;
    bool return_required = false;
    std::unordered_set<const Symbol*> calls;

    void collect_expr(ExprPtr expr, bool value_required) {
        if (!expr) return;
        switch (expr->kind) {
            case Expr::Kind::Call: {
                if (value_required && expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                    const Symbol* sym = ctx && ctx->binding ? ctx->binding(instance_id, expr->operand) : nullptr;
                    if (sym) {
                        calls.insert(sym);
                    }
                }
                for (const auto& rec : expr->receivers) {
                    collect_expr(rec, true);
                }
                const Symbol* callee = bound_callee(ctx, instance_id, expr);
                for_each_call_value_arg(callee, expr, [&](ExprPtr arg) { collect_expr(arg, true); });
                break;
            }
            case Expr::Kind::Binary:
                collect_expr(expr->left, true);
                collect_expr(expr->right, true);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                collect_expr(expr->operand, true);
                break;
            case Expr::Kind::Index:
                collect_expr(expr->operand, true);
                if (!expr->args.empty()) {
                    collect_expr(expr->args[0], true);
                }
                break;
            case Expr::Kind::Member:
                collect_expr(expr->operand, true);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) {
                    collect_expr(elem, true);
                }
                break;
            case Expr::Kind::Block:
                for (const auto& stmt : expr->statements) {
                    collect_stmt(stmt);
                }
                collect_expr(expr->result_expr, value_required);
                break;
            case Expr::Kind::Conditional:
                if (ctx && ctx->constexpr_condition) {
                    auto cond = ctx->constexpr_condition(instance_id, expr->condition);
                    if (cond.has_value()) {
                        // Invariant: skip compile-time-dead branches to avoid requiring
                        // concrete types for values that will never be used.
                        collect_expr(cond.value() ? expr->true_expr : expr->false_expr, value_required);
                        break;
                    }
                }
                collect_expr(expr->condition, true);
                collect_expr(expr->true_expr, value_required);
                collect_expr(expr->false_expr, value_required);
                break;
            case Expr::Kind::Assignment:
                collect_expr(expr->left, true);
                collect_expr(expr->right, true);
                break;
            case Expr::Kind::Range:
                collect_expr(expr->left, true);
                collect_expr(expr->right, true);
                break;
            case Expr::Kind::Iteration:
                collect_expr(loop_subject(expr), true);
                collect_expr(loop_body(expr), false);
                break;
            case Expr::Kind::Repeat:
                collect_expr(loop_subject(expr), true);
                collect_expr(loop_body(expr), false);
                break;
            default:
                break;
        }
    }

    void collect_stmt(StmtPtr stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::VarDecl:
                collect_expr(stmt->var_init, true);
                break;
            case Stmt::Kind::Expr:
                collect_expr(stmt->expr, false);
                break;
            case Stmt::Kind::Return:
                collect_expr(stmt->return_expr, return_required);
                break;
            case Stmt::Kind::ConditionalStmt:
                if (ctx && ctx->constexpr_condition) {
                    auto cond = ctx->constexpr_condition(instance_id, stmt->condition);
                    if (cond.has_value()) {
                        if (cond.value()) {
                            collect_stmt(stmt->true_stmt);
                        }
                        break;
                    }
                }
                collect_expr(stmt->condition, true);
                collect_stmt(stmt->true_stmt);
                break;
            default:
                break;
        }
    }
};

struct TypeUseValidator {
    const TypeUseContext* ctx = nullptr;
    bool return_required = false;
    std::string func_name;
    int instance_id = -1;

    void validate_lvalue(ExprPtr expr) {
        if (!expr) return;
        switch (expr->kind) {
            case Expr::Kind::Identifier:
                return;
            case Expr::Kind::Member:
                validate_expr(expr->operand, true);
                return;
            case Expr::Kind::Index:
                validate_expr(expr->operand, true);
                if (!expr->args.empty()) {
                    validate_expr(expr->args[0], true);
                }
                return;
            default:
                validate_expr(expr, true);
                return;
        }
    }

    void validate_expr(ExprPtr expr, bool value_required) {
        if (!expr) return;
        bool allow_untyped = expr->kind == Expr::Kind::ArrayLiteral && expr->elements.empty();
        if (value_required && !expr->is_expr_param_ref && !allow_untyped) {
            if (!expr->type) {
                throw CompileError("Expression produces no value", expr->location);
            }
            if (!type_is_concrete(ctx, expr->type)) {
                if (const char* debug = std::getenv("VEXEL_DEBUG_TYPE_USE"); debug && *debug) {
                    std::cerr << "Type-use debug: kind=" << static_cast<int>(expr->kind)
                              << " type=" << (expr->type ? expr->type->to_string() : "<null>");
                    if (expr->kind == Expr::Kind::Identifier) {
                        std::cerr << " name=" << expr->name;
                    }
                    std::cerr << " at " << expr->location.filename << ":" << expr->location.line
                              << ":" << expr->location.column << "\n";
                }
                throw CompileError("Expression requires a concrete type", expr->location);
            }
        }

        switch (expr->kind) {
            case Expr::Kind::Binary:
                validate_expr(expr->left, true);
                validate_expr(expr->right, true);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                validate_expr(expr->operand, true);
                break;
            case Expr::Kind::Call:
                for (const auto& rec : expr->receivers) {
                    validate_expr(rec, true);
                }
                {
                    const Symbol* callee = bound_callee(ctx, instance_id, expr);
                    for_each_call_value_arg(callee, expr, [&](ExprPtr arg) { validate_expr(arg, true); });
                }
                break;
            case Expr::Kind::Index:
                validate_expr(expr->operand, true);
                if (!expr->args.empty()) {
                    validate_expr(expr->args[0], true);
                }
                break;
            case Expr::Kind::Member:
                validate_expr(expr->operand, true);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) {
                    validate_expr(elem, true);
                }
                break;
            case Expr::Kind::Block:
                for (const auto& stmt : expr->statements) {
                    validate_stmt(stmt);
                }
                validate_expr(expr->result_expr, value_required);
                break;
            case Expr::Kind::Conditional:
                if (ctx && ctx->constexpr_condition) {
                    auto cond = ctx->constexpr_condition(instance_id, expr->condition);
                    if (cond.has_value()) {
                        // Invariant: compile-time-dead branch is ignored by type-use validation.
                        validate_expr(cond.value() ? expr->true_expr : expr->false_expr, value_required);
                        break;
                    }
                }
                validate_expr(expr->condition, true);
                validate_expr(expr->true_expr, value_required);
                validate_expr(expr->false_expr, value_required);
                break;
            case Expr::Kind::Assignment:
                validate_lvalue(expr->left);
                validate_expr(expr->right, true);
                break;
            case Expr::Kind::Range:
                validate_expr(expr->left, true);
                validate_expr(expr->right, true);
                break;
            case Expr::Kind::Iteration:
                validate_expr(loop_subject(expr), true);
                validate_expr(loop_body(expr), false);
                break;
            case Expr::Kind::Repeat:
                validate_expr(loop_subject(expr), true);
                validate_expr(loop_body(expr), false);
                break;
            default:
                break;
        }
    }

    void validate_stmt(StmtPtr stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::VarDecl:
                if (!type_is_concrete(ctx, stmt->var_type)) {
                    throw CompileError("Variable '" + stmt->var_name + "' requires a concrete type", stmt->location);
                }
                validate_expr(stmt->var_init, true);
                break;
            case Stmt::Kind::Expr:
                validate_expr(stmt->expr, false);
                break;
            case Stmt::Kind::Return:
                validate_expr(stmt->return_expr, return_required);
                break;
            case Stmt::Kind::ConditionalStmt:
                if (ctx && ctx->constexpr_condition) {
                    auto cond = ctx->constexpr_condition(instance_id, stmt->condition);
                    if (cond.has_value()) {
                        if (cond.value()) {
                            validate_stmt(stmt->true_stmt);
                        }
                        break;
                    }
                }
                validate_expr(stmt->condition, true);
                validate_stmt(stmt->true_stmt);
                break;
            default:
                break;
        }
    }
};

} // namespace

void validate_type_usage(const Module& mod, const AnalysisFacts& facts, const TypeUseContext& ctx) {
    // Invariants:
    // - This pass runs after Analyzer, so reachability/used-globals are known.
    // - Only *used* values must have concrete types; unused chains are allowed.
    // - Compile-time-dead branches are ignored (via ctx.constexpr_condition).
    // - Expression-parameter arguments are treated as opaque and skipped.
    std::unordered_map<const Symbol*, StmtPtr> functions;
    for (const auto& sym : facts.reachable_functions) {
        if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) continue;
        functions[sym] = sym->declaration;
    }

    std::unordered_map<const Symbol*, std::unordered_set<const Symbol*>> calls_always;
    std::unordered_map<const Symbol*, std::unordered_set<const Symbol*>> calls_if_return;
    std::unordered_set<const Symbol*> return_required;

    for (const auto& pair : functions) {
        const Symbol* sym = pair.first;
        const StmtPtr& func = pair.second;
        CallCollector collect_false;
        collect_false.ctx = &ctx;
        collect_false.instance_id = sym->instance_id;
        collect_false.return_required = false;
        visit_function_body(
            func->body,
            false,
            [&](StmtPtr stmt) { collect_false.collect_stmt(stmt); },
            [&](ExprPtr expr, bool required) { collect_false.collect_expr(expr, required); });
        CallCollector collect_true;
        collect_true.ctx = &ctx;
        collect_true.instance_id = sym->instance_id;
        collect_true.return_required = true;
        visit_function_body(
            func->body,
            true,
            [&](StmtPtr stmt) { collect_true.collect_stmt(stmt); },
            [&](ExprPtr expr, bool required) { collect_true.collect_expr(expr, required); });

        calls_always[sym] = collect_false.calls;
        std::unordered_set<const Symbol*> diff;
        for (const auto& callee : collect_true.calls) {
            if (!collect_false.calls.count(callee)) {
                diff.insert(callee);
            }
        }
        calls_if_return[sym] = std::move(diff);

        for (const auto& callee : collect_false.calls) {
            return_required.insert(callee);
        }
    }

    for (const auto& sym : facts.used_global_vars) {
        if (!sym || !sym->declaration) continue;
        // Invariant: used globals must have concrete types, and their initializers
        // are treated as value-required.
        CallCollector collector;
        collector.ctx = &ctx;
        collector.instance_id = sym->instance_id;
        collector.return_required = true;
        collector.collect_expr(sym->declaration->var_init, true);
        for (const auto& callee : collector.calls) {
            return_required.insert(callee);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        // Invariant: if a function's return is required, then any callee
        // reachable only in the "return value used" context also becomes required.
        for (const auto& key : std::vector<const Symbol*>(return_required.begin(), return_required.end())) {
            auto it = calls_if_return.find(key);
            if (it == calls_if_return.end()) {
                continue;
            }
            for (const auto& callee : it->second) {
                if (return_required.insert(callee).second) {
                    changed = true;
                }
            }
        }
    }

    for (const auto& stmt : mod.top_level) {
        if (stmt->kind != Stmt::Kind::TypeDecl) continue;
        if (!facts.used_type_names.count(stmt->type_decl_name)) {
            continue;
        }
        for (const auto& field : stmt->fields) {
            if (!type_is_concrete(&ctx, field.type)) {
                throw CompileError("Field '" + field.name + "' requires a concrete type", field.location);
            }
        }
    }

    for (const auto& pair : functions) {
        const Symbol* sym = pair.first;
        const StmtPtr& func = pair.second;
        if (func->is_generic) {
            continue;
        }

        bool ret_required = return_required.count(sym) > 0;
        for (const auto& param : func->params) {
            if (param.is_expression_param) {
                continue;
            }
            if (!type_is_concrete(&ctx, param.type)) {
                throw CompileError("Parameter '" + param.name + "' in function '" +
                                   qualified_func_name(sym) + "' requires a concrete type",
                                   param.location);
            }
        }

        if (func->ref_param_types.size() < func->ref_params.size()) {
            func->ref_param_types.resize(func->ref_params.size(), nullptr);
        }
        for (size_t i = 0; i < func->ref_params.size(); ++i) {
            TypePtr ref_type = func->ref_param_types[i];
            if (!ref_type && !func->type_namespace.empty() && i == 0) {
                ref_type = Type::make_named(func->type_namespace, func->location);
            }
            if (!type_is_concrete(&ctx, ref_type)) {
                throw CompileError("Receiver '" + func->ref_params[i] + "' in function '" +
                                   qualified_func_name(sym) + "' requires a concrete type",
                                   func->location);
            }
        }

        if (!func->return_types.empty()) {
            for (const auto& rt : func->return_types) {
                if (!type_is_concrete(&ctx, rt)) {
                    throw CompileError("Return type in function '" + qualified_func_name(sym) +
                                       "' requires a concrete type", func->location);
                }
            }
        } else if (ret_required) {
            if (!type_is_concrete(&ctx, func->return_type)) {
                throw CompileError("Return value of function '" + qualified_func_name(sym) +
                                   "' is used but its return type is unresolved",
                                   func->location);
            }
        }

        if (!func->body) {
            continue;
        }
        TypeUseValidator validator;
        validator.ctx = &ctx;
        validator.return_required = ret_required;
        validator.func_name = qualified_func_name(sym);
        validator.instance_id = sym->instance_id;
        visit_function_body(
            func->body,
            ret_required,
            [&](StmtPtr stmt) { validator.validate_stmt(stmt); },
            [&](ExprPtr expr, bool required) { validator.validate_expr(expr, required); });
    }

    for (const auto& sym : facts.used_global_vars) {
        if (!sym || !sym->declaration) continue;
        if (!type_is_concrete(&ctx, sym->declaration->var_type)) {
            throw CompileError("Global '" + sym->name + "' requires a concrete type", sym->declaration->location);
        }
        TypeUseValidator validator;
        validator.ctx = &ctx;
        validator.return_required = true;
        validator.instance_id = sym->instance_id;
        validator.validate_expr(sym->declaration->var_init, true);
    }
}

} // namespace vexel
