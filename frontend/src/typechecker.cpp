#include "typechecker.h"
#include "evaluator.h"
#include "resolver.h"
#include "type_use_validator.h"
#include <algorithm>
#include <array>
#include <limits>
#include "constants.h"

namespace vexel {

TypeChecker::TypeChecker(const std::string& proj_root, bool allow_process_exprs, Resolver* resolver,
                         Bindings* bindings_in, Program* program_in)
    : resolver(resolver),
      bindings(bindings_in),
      program(program_in),
      global_scope(resolver ? resolver->instance_scope(current_instance_id) : nullptr),
      type_var_counter(0),
      loop_depth(0),
      project_root(proj_root),
      allow_process(allow_process_exprs) {}

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

unsigned long long TypeChecker::stmt_key(const Stmt* stmt) const {
    return (static_cast<unsigned long long>(static_cast<uint32_t>(current_instance_id)) << 32) ^
           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(stmt));
}

void TypeChecker::check_program(Program& program_in) {
    set_program(&program_in);
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
            require_boolean(cond_type, stmt->condition ? stmt->condition->location : stmt->location,
                            "Conditional statement");
            check_stmt(stmt->true_stmt);
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
            throw CompileError("Return type mismatch in function '" + stmt->func_name + "'",
                               stmt->location);
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
    sym->kind = stmt->is_mutable ? Symbol::Kind::Variable : Symbol::Kind::Constant;
    sym->type = type;
    sym->is_mutable = stmt->is_mutable;
    sym->declaration = stmt;
}

  void TypeChecker::validate_type_usage(const Module& mod, const AnalysisFacts& facts) {
    TypeUseContext ctx;
    ctx.resolve_type = [this](TypePtr type) { return resolve_type(type); };
    ctx.constexpr_condition = [this](int instance_id, ExprPtr expr) {
        int saved = current_instance_id;
        set_current_instance(instance_id);
        auto result = constexpr_condition(expr);
        set_current_instance(saved);
        return result;
    };
    ctx.binding = [this](int instance_id, ExprPtr expr) {
        return binding_for(instance_id, expr.get());
    };
    ::vexel::validate_type_usage(mod, facts, ctx);
}

}
