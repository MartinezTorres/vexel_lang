#include "typechecker.h"
#include "cte_engine.h"
#include "expr_access.h"
#include "resolver.h"
#include "type_use_validator.h"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include "constants.h"

namespace vexel {

TypeChecker::TypeChecker(const std::string& proj_root, bool allow_process_exprs, Resolver* resolver,
                         Bindings* bindings_in, Program* program_in)
    : resolver(resolver),
      bindings(bindings_in),
      program(program_in),
      global_scope(nullptr),
      type_var_counter(0),
      loop_depth(0),
      project_root(proj_root),
      allow_process(allow_process_exprs),
      cte_engine(std::make_unique<CTEEngine>(this)) {}

TypeChecker::~TypeChecker() = default;

void TypeChecker::set_resolver(Resolver* resolver_in) {
    resolver = resolver_in;
    global_scope = resolver ? resolver->instance_scope(current_instance_id) : nullptr;
}

void TypeChecker::set_bindings(Bindings* bindings_in) {
    bindings = bindings_in;
}

void TypeChecker::set_program(Program* program_in) {
    program = program_in;
}

Symbol* TypeChecker::binding_for(int instance_id, const void* node) const {
    if (!bindings) return nullptr;
    return bindings->lookup(instance_id, node);
}

void TypeChecker::set_current_instance(int instance_id) {
    current_instance_id = instance_id;
    global_scope = resolver ? resolver->instance_scope(current_instance_id) : nullptr;
    forget_all_constexpr_values();
}

Symbol* TypeChecker::lookup_global(const std::string& name) const {
    if (resolver) {
        return resolver->lookup_in_instance(current_instance_id, name);
    }
    if (!global_scope) return nullptr;
    return global_scope->lookup(name);
}

Symbol* TypeChecker::lookup_binding(const void* node) const {
    if (!bindings) return nullptr;
    return bindings->lookup(current_instance_id, node);
}

bool TypeChecker::try_evaluate_constexpr(ExprPtr expr, CTValue& out) {
    if (!cte_engine) {
        cte_engine = std::make_unique<CTEEngine>(this);
    }
    const auto seed_values = known_constexpr_values;
    return cte_engine->try_evaluate(current_instance_id, expr, out, seed_values);
}

CTEQueryResult TypeChecker::query_constexpr(ExprPtr expr) {
    if (!cte_engine) {
        cte_engine = std::make_unique<CTEEngine>(this);
    }
    const auto seed_values = known_constexpr_values;
    return cte_engine->query(current_instance_id, expr, seed_values);
}

void TypeChecker::remember_constexpr_value(Symbol* sym, const CTValue& value) {
    if (!sym || !sym->is_local) return;
    known_constexpr_values[sym] = value;
}

void TypeChecker::forget_constexpr_value(Symbol* sym) {
    if (!sym || !sym->is_local) return;
    known_constexpr_values.erase(sym);
}

void TypeChecker::forget_all_constexpr_values() {
    known_constexpr_values.clear();
}

unsigned long long TypeChecker::stmt_key(const Stmt* stmt) const {
    return (static_cast<unsigned long long>(static_cast<uint32_t>(current_instance_id)) << 32) ^
           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stmt));
}

unsigned long long TypeChecker::expr_key(int instance_id, const Expr* expr) const {
    return (static_cast<unsigned long long>(static_cast<uint32_t>(instance_id)) << 32) ^
           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(expr));
}

unsigned long long TypeChecker::expr_key(const Expr* expr) const {
    return expr_key(current_instance_id, expr);
}

void TypeChecker::check_program(Program& program_in) {
    set_program(&program_in);
    checked_statements.clear();
    constexpr_condition_cache.clear();
    forget_all_constexpr_values();
    for (const auto& instance : program_in.instances) {
        set_current_instance(instance.id);
        check_module(program_in.modules[static_cast<size_t>(instance.module_id)].module);
    }
}

void TypeChecker::check_module(Module& mod) {
    // Pass: Type-check all statements in order (constants, functions, variables)
    // This enforces parse-order initialization for constants. Iterate by index
    // because imports/generic instantiations can append new statements.
    for (size_t i = 0; i < mod.top_level.size(); ++i) {
        StmtPtr stmt = mod.top_level[i];
        check_stmt(stmt);
    }
    validate_invariants(mod);
}

void TypeChecker::check_stmt(StmtPtr stmt) {
    if (!stmt) return;
    if (checked_statements.count(stmt_key(stmt.get()))) {
        return;
    }
    checked_statements.insert(stmt_key(stmt.get()));

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            check_func_decl(stmt);
            break;
        case Stmt::Kind::TypeDecl:
            check_type_decl(stmt);
            break;
        case Stmt::Kind::VarDecl:
            check_var_decl(stmt);
            break;
        case Stmt::Kind::Import:
            break;
        case Stmt::Kind::Expr:
            if (stmt->expr) check_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            if (stmt->return_expr) check_expr(stmt->return_expr);
            break;
        case Stmt::Kind::Break:
            if (loop_depth == 0) {
                throw CompileError("Break statement outside of loop", stmt->location);
            }
            break;
        case Stmt::Kind::Continue:
            if (loop_depth == 0) {
                throw CompileError("Continue statement outside of loop", stmt->location);
            }
            break;
        case Stmt::Kind::ConditionalStmt: {
            TypePtr cond_type = stmt->condition ? check_expr(stmt->condition) : nullptr;
            require_boolean_expr(stmt->condition, cond_type,
                                 stmt->condition ? stmt->condition->location : stmt->location,
                                 "Conditional statement");
            auto cond = constexpr_condition(stmt->condition);
            if (stmt->condition && cond.has_value()) {
                constexpr_condition_cache[expr_key(stmt->condition.get())] = cond.value();
            } else if (stmt->condition) {
                constexpr_condition_cache.erase(expr_key(stmt->condition.get()));
            }
            if (cond.has_value()) {
                if (cond.value()) {
                    check_stmt(stmt->true_stmt);
                }
            } else {
                auto saved_constexpr_values = known_constexpr_values;
                check_stmt(stmt->true_stmt);
                known_constexpr_values = std::move(saved_constexpr_values);
            }
            break;
        }
        default:
            break;
    }
}

void TypeChecker::check_func_decl(StmtPtr stmt) {
    // Build the function name (qualified if it's a method)
    std::string func_name = stmt->func_name;
    if (!stmt->type_namespace.empty()) {
        func_name = stmt->type_namespace + "::" + stmt->func_name;
    }

    // Check if function is generic (has parameters without types)
    if (!stmt->is_instantiation) {
        stmt->is_generic = is_generic_function(stmt);
    } else {
        stmt->is_generic = false;
    }

    if (stmt->is_generic && (stmt->is_exported || stmt->is_external)) {
        throw CompileError("Generic functions cannot be exported or external", stmt->location);
    }

    Symbol* func_sym = lookup_binding(stmt.get());
    if (!func_sym) {
        throw CompileError("Internal error: unresolved function '" + func_name + "'", stmt->location);
    }

    // Validate external function signatures (primitives only)
    if (stmt->is_external) {
        for (const auto& param : stmt->params) {
            if (param.type && !is_primitive_type(param.type)) {
                throw CompileError("External functions can only use primitive types (found " +
                                 param.type->to_string() + " in parameter " + param.name + ")", stmt->location);
            }
        }
        if (stmt->return_type && !is_primitive_type(stmt->return_type)) {
            throw CompileError("External functions can only use primitive types in return type (found " +
                             stmt->return_type->to_string() + ")", stmt->location);
        }
    }

    // Skip type-checking body for generic functions - only type-check instantiations
    if (stmt->is_generic) {
        return;
    }

    if (!stmt->is_external && stmt->body) {
        struct ConstexprScopeGuard {
            TypeChecker& tc;
            std::unordered_map<const Symbol*, CTValue> saved;
            explicit ConstexprScopeGuard(TypeChecker& checker)
                : tc(checker), saved(checker.known_constexpr_values) {
                tc.known_constexpr_values.clear();
            }
            ~ConstexprScopeGuard() {
                tc.known_constexpr_values = std::move(saved);
            }
        } constexpr_guard(*this);
        if (stmt->ref_param_types.size() < stmt->ref_params.size()) {
            stmt->ref_param_types.resize(stmt->ref_params.size(), nullptr);
        }
        for (size_t i = 0; i < stmt->ref_params.size(); ++i) {
            Symbol* rsym = lookup_binding(&stmt->ref_params[i]);
            if (!rsym) {
                throw CompileError("Internal error: unresolved receiver '" + stmt->ref_params[i] + "'", stmt->location);
            }
            if (!stmt->type_namespace.empty() && i == 0) {
                rsym->type = Type::make_named(stmt->type_namespace, stmt->location);
                if (bindings) {
                    Symbol* type_sym = lookup_global(stmt->type_namespace);
                    if (type_sym) {
                        bindings->bind(current_instance_id, rsym->type.get(), type_sym);
                    }
                }
            } else if (!rsym->type) {
                rsym->type = make_fresh_typevar();
            }
            rsym->is_mutable = true;
            stmt->ref_param_types[i] = rsym->type;
        }

        for (auto& param : stmt->params) {
            if (!param.type) {
                param.type = make_fresh_typevar();
            }
            Symbol* psym = lookup_binding(&param);
            if (!psym) {
                throw CompileError("Internal error: unresolved parameter '" + param.name + "'", param.location);
            }
            psym->type = param.type;
            psym->is_mutable = false;
        }

        TypePtr body_type = check_expr(stmt->body);

        if (!stmt->return_types.empty()) {
            if (!stmt->return_type) {
                stmt->return_type = stmt->return_types[0];
            }
        } else if (!stmt->return_type) {
            stmt->return_type = body_type;
        } else if (!types_compatible(body_type, stmt->return_type)) {
            ExprPtr return_expr = stmt->body;
            if (return_expr && return_expr->kind == Expr::Kind::Block && return_expr->result_expr) {
                return_expr = return_expr->result_expr;
            }
            if (return_expr && literal_assignable_to(stmt->return_type, return_expr)) {
                return_expr->type = stmt->return_type;
                if (stmt->body) {
                    stmt->body->type = stmt->return_type;
                }
            } else {
                throw CompileError("Return type mismatch in function '" + stmt->func_name + "'",
                                   stmt->location);
            }
        }
    }
}

void TypeChecker::check_type_decl(StmtPtr stmt) {
    if (!lookup_binding(stmt.get())) {
        throw CompileError("Internal error: unresolved type '" + stmt->type_decl_name + "'", stmt->location);
    }

    // Check fields for type inference
    for (auto& field : stmt->fields) {
        if (!field.type) {
            field.type = make_fresh_typevar();
        }
    }

    // Check for recursive type (type contains itself as a field)
    check_recursive_type(stmt->type_decl_name, stmt, stmt->location);
}

void TypeChecker::check_var_decl(StmtPtr stmt) {
    TypePtr type = stmt->var_type;
    bool constexpr_init = false;
    if (stmt->var_init) {
        TypePtr init_type = check_expr(stmt->var_init);
        if (!type) {
            type = init_type;
            stmt->var_type = type;
        } else if (type->kind == Type::Kind::Array &&
                   stmt->var_init->kind == Expr::Kind::ArrayLiteral) {
            bool compatible = true;
            for (auto& el : stmt->var_init->elements) {
                if (!types_compatible(el->type, type->element_type) &&
                    !literal_assignable_to(type->element_type, el)) {
                    compatible = false;
                    break;
                }
            }
            if (compatible) {
                stmt->var_init->type = type;
            } else {
                throw CompileError("Type mismatch in variable initialization", stmt->location);
            }
        } else if (stmt->var_init->kind == Expr::Kind::Cast) {
            // Allow explicit casts to satisfy the annotated type
            stmt->var_init->type = type;
        } else if (!types_compatible(init_type, type)) {
            if (literal_assignable_to(type, stmt->var_init)) {
                stmt->var_init->type = type;
            } else {
                throw CompileError("Type mismatch in variable initialization", stmt->location);
            }
        }
    } else if (!type) {
        throw CompileError("Variable must have type annotation or initializer", stmt->location);
    }

    // Validate the type
    validate_type(type, stmt->location);

    Symbol* sym = lookup_binding(stmt.get());
    if (!sym) {
        throw CompileError("Internal error: unresolved variable '" + stmt->var_name + "'", stmt->location);
    }

    bool inferred_mutable = stmt->is_mutable;
    if (!sym->is_local && !inferred_mutable) {
        if (stmt->var_init) {
            CTValue result;
            constexpr_init = try_evaluate_constexpr(stmt->var_init, result);
        }
        inferred_mutable = !constexpr_init;
        stmt->is_mutable = inferred_mutable;
    }

    if (!sym->is_local && stmt->is_exported) {
        if (!stmt->var_init) {
            throw CompileError("Exported global '" + stmt->var_name +
                               "' must have a compile-time initializer",
                               stmt->location);
        }
        if (inferred_mutable || !constexpr_init) {
            throw CompileError("Exported global '" + stmt->var_name +
                               "' must be immutable and compile-time constant",
                               stmt->location);
        }
    }

    sym->kind = inferred_mutable ? Symbol::Kind::Variable : Symbol::Kind::Constant;
    sym->type = type;
    sym->is_mutable = inferred_mutable;
    sym->declaration = stmt;

    if (sym->is_local) {
        if (!stmt->var_init) {
            forget_constexpr_value(sym);
        } else {
            CTValue value;
            if (try_evaluate_constexpr(stmt->var_init, value)) {
                remember_constexpr_value(sym, value);
            } else {
                forget_constexpr_value(sym);
            }
        }
    }
}

void TypeChecker::validate_invariants(const Module& mod) {
    std::function<void(ExprPtr)> validate_expr;
    std::function<void(StmtPtr)> validate_stmt;

    validate_expr = [&](ExprPtr expr) -> void {
        if (!expr) return;

        bool untyped_ok = false;
        if (expr->kind == Expr::Kind::Iteration || expr->kind == Expr::Kind::Repeat) {
            untyped_ok = true;
        } else if (expr->kind == Expr::Kind::Block) {
            if (!expr->result_expr || !expr->result_expr->type) {
                untyped_ok = true;
            }
        } else if (expr->kind == Expr::Kind::Call && !expr->type) {
            // Void calls are permitted in statement position; type-use validation
            // will reject them if their value is consumed.
            untyped_ok = true;
        } else if (expr->kind == Expr::Kind::Assignment && !expr->type) {
            // Assignment expressions can be used as statements even when the RHS is void.
            untyped_ok = true;
        }

        if (!expr->type && !untyped_ok) {
            throw CompileError("Internal error: missing type after type checking", expr->location);
        }
        if (expr->type && untyped_ok) {
            throw CompileError("Internal error: unexpected type on statement expression", expr->location);
        }

        switch (expr->kind) {
            case Expr::Kind::Binary:
                validate_expr(expr->left);
                validate_expr(expr->right);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                validate_expr(expr->operand);
                break;
            case Expr::Kind::Call:
                {
                    if (expr->operand && expr->operand->kind != Expr::Kind::Identifier) {
                        validate_expr(expr->operand);
                    }
                    for (const auto& rec : expr->receivers) validate_expr(rec);
                    Symbol* call_sym = nullptr;
                    if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                        call_sym = lookup_binding(expr->operand.get());
                        if (!call_sym) {
                            call_sym = lookup_global(expr->operand->name);
                        }
                    }
                    for (size_t i = 0; i < expr->args.size(); ++i) {
                        bool skip_arg = false;
                        if (call_sym && call_sym->kind == Symbol::Kind::Function && call_sym->declaration) {
                            if (i < call_sym->declaration->params.size() &&
                                call_sym->declaration->params[i].is_expression_param) {
                                skip_arg = true;
                            }
                        }
                        if (!skip_arg) {
                            validate_expr(expr->args[i]);
                        }
                    }
                    break;
                }
            case Expr::Kind::Index:
                validate_expr(expr->operand);
                if (!expr->args.empty()) validate_expr(expr->args[0]);
                break;
            case Expr::Kind::Member:
                validate_expr(expr->operand);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) validate_expr(elem);
                break;
            case Expr::Kind::Block:
                for (const auto& st : expr->statements) {
                    if (!st) continue;
                    validate_stmt(st);
                }
                if (expr->result_expr) validate_expr(expr->result_expr);
                break;
            case Expr::Kind::Conditional:
                validate_expr(expr->condition);
                if (expr->condition) {
                    auto it = constexpr_condition_cache.find(expr_key(expr->condition.get()));
                    if (it != constexpr_condition_cache.end()) {
                        if (it->second) {
                            validate_expr(expr->true_expr);
                        } else {
                            validate_expr(expr->false_expr);
                        }
                        break;
                    }
                }
                {
                    auto static_value = constexpr_condition(expr->condition);
                    if (static_value.has_value()) {
                        if (static_value.value()) {
                            validate_expr(expr->true_expr);
                        } else {
                            validate_expr(expr->false_expr);
                        }
                    } else {
                        validate_expr(expr->true_expr);
                        validate_expr(expr->false_expr);
                    }
                }
                break;
            case Expr::Kind::Assignment:
                if (expr->left && expr->left->kind != Expr::Kind::Identifier) {
                    validate_expr(expr->left);
                }
                validate_expr(expr->right);
                break;
            case Expr::Kind::Range:
                validate_expr(expr->left);
                validate_expr(expr->right);
                break;
            case Expr::Kind::Iteration:
                validate_expr(loop_subject(expr));
                validate_expr(loop_body(expr));
                break;
            case Expr::Kind::Repeat:
                validate_expr(loop_subject(expr));
                validate_expr(loop_body(expr));
                break;
            case Expr::Kind::Resource:
            case Expr::Kind::Process:
            case Expr::Kind::Identifier:
            case Expr::Kind::IntLiteral:
            case Expr::Kind::FloatLiteral:
            case Expr::Kind::StringLiteral:
            case Expr::Kind::CharLiteral:
                break;
        }
    };

    validate_stmt = [&](StmtPtr stmt) -> void {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::VarDecl:
                if (!stmt->var_type) {
                    throw CompileError("Internal error: variable '" + stmt->var_name +
                                       "' has no type after type checking", stmt->location);
                }
                if (stmt->var_init) {
                    validate_expr(stmt->var_init);
                    if (!stmt->var_init->type) {
                        throw CompileError("Internal error: variable '" + stmt->var_name +
                                           "' initializer has no type", stmt->location);
                    }
                }
                break;
            case Stmt::Kind::FuncDecl: {
                if (stmt->is_generic && !stmt->is_instantiation) {
                    return;
                }
                if (!stmt->is_external && !stmt->body) {
                    throw CompileError("Internal error: missing function body for '" +
                                       stmt->func_name + "'", stmt->location);
                }
                if (stmt->type_namespace.empty() && !stmt->ref_params.empty()) {
                    // Ref params are allowed on free functions, but must be typed.
                }
                if (stmt->ref_param_types.size() < stmt->ref_params.size()) {
                    throw CompileError("Internal error: receiver types missing for '" +
                                       stmt->func_name + "'", stmt->location);
                }
                for (size_t i = 0; i < stmt->ref_params.size(); ++i) {
                    if (!stmt->ref_param_types[i]) {
                        throw CompileError("Internal error: receiver '" + stmt->ref_params[i] +
                                           "' has no type after type checking", stmt->location);
                    }
                }
                for (const auto& param : stmt->params) {
                    if (param.is_expression_param) continue;
                    if (!param.type) {
                        throw CompileError("Internal error: parameter '" + param.name +
                                           "' has no type after type checking", param.location);
                    }
                }
                if (stmt->is_external) {
                    for (const auto& param : stmt->params) {
                        if (param.is_expression_param) continue;
                        if (!param.type) {
                            throw CompileError("External function parameter '" + param.name +
                                               "' must have a type", param.location);
                        }
                    }
                }
                for (const auto& rt : stmt->return_types) {
                    if (!rt) {
                        throw CompileError("Internal error: tuple return type missing in '" +
                                           stmt->func_name + "'", stmt->location);
                    }
                }
                if (stmt->body) {
                    validate_expr(stmt->body);
                }
                break;
            }
            case Stmt::Kind::TypeDecl:
                for (const auto& field : stmt->fields) {
                    if (!field.type) {
                        throw CompileError("Internal error: field '" + field.name +
                                           "' missing type in '" + stmt->type_decl_name + "'",
                                           field.location);
                    }
                }
                break;
            case Stmt::Kind::Expr:
                validate_expr(stmt->expr);
                break;
            case Stmt::Kind::Return:
                if (stmt->return_expr) {
                    validate_expr(stmt->return_expr);
                }
                break;
            case Stmt::Kind::ConditionalStmt:
                validate_expr(stmt->condition);
                if (stmt->condition) {
                    auto it = constexpr_condition_cache.find(expr_key(stmt->condition.get()));
                    if (it != constexpr_condition_cache.end()) {
                        if (it->second) {
                            validate_stmt(stmt->true_stmt);
                        }
                        break;
                    }
                }
                validate_stmt(stmt->true_stmt);
                break;
            case Stmt::Kind::Import:
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue:
                break;
        }
    };

    for (const auto& stmt : mod.top_level) {
        validate_stmt(stmt);
    }
}

void TypeChecker::validate_type_usage(const Module& mod, const AnalysisFacts& facts) {
    TypeUseContext ctx;
    ctx.resolve_type = [this](TypePtr type) { return resolve_type(type); };
    ctx.constexpr_condition = [this](int instance_id, ExprPtr expr) {
        if (expr) {
            auto it = constexpr_condition_cache.find(expr_key(instance_id, expr.get()));
            if (it != constexpr_condition_cache.end()) {
                return std::optional<bool>(it->second);
            }
        }
        auto scope = scoped_instance(instance_id);
        (void)scope;
        auto result = constexpr_condition(expr);
        return result;
    };
    ctx.binding = [this](int instance_id, ExprPtr expr) {
        return binding_for(instance_id, expr.get());
    };
    ::vexel::validate_type_usage(mod, facts, ctx);
}

}
