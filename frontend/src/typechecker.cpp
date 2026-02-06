#include "typechecker.h"
#include "evaluator.h"
#include "resolver.h"
#include "type_use_validator.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iostream>
#include "lexer.h"
#include "parser.h"
#include "constants.h"

namespace vexel {

TypeChecker::TypeChecker(const std::string& proj_root, bool allow_process_exprs)
    : current_scope(nullptr),
      type_var_counter(0),
      scope_counter(0),
      loop_depth(0),
      project_root(proj_root),
      allow_process(allow_process_exprs),
      current_module(nullptr) {
    push_scope();
}

void TypeChecker::push_scope() {
    scopes.push_back(std::make_unique<Scope>(current_scope, scope_counter++));
    current_scope = scopes.back().get();
}

void TypeChecker::pop_scope() {
    if (current_scope->parent) {
        current_scope = current_scope->parent;
    }
}

void TypeChecker::check_module(Module& mod) {
    current_module = &mod;
    Resolver resolver(this);
    resolver.predeclare(mod);

    // Pass 2: Type-check all statements in order (constants, functions, variables)
    // This enforces parse-order initialization for constants. Iterate by index
    // because imports/generic instantiations can append new statements.
    for (size_t i = 0; i < mod.top_level.size(); ++i) {
        StmtPtr stmt = mod.top_level[i];
        check_stmt(stmt);
    }

    // Process pending generic instantiations
    while (!pending_instantiations.empty()) {
        std::vector<StmtPtr> batch;
        batch.swap(pending_instantiations);

        for (auto& inst : batch) {
            // Type check the instantiation
            check_func_decl(inst);

            // Add to module
            mod.top_level.push_back(inst);
        }
    }
}

void TypeChecker::check_stmt(StmtPtr stmt) {
    if (!stmt) return;
    if (checked_statements.count(stmt.get())) {
        return;
    }
    checked_statements.insert(stmt.get());

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
            check_import(stmt);
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
    // Don't recalculate if already explicitly set to false (for instantiations)
    bool was_set_to_non_generic = !stmt->is_generic;
    stmt->is_generic = is_generic_function(stmt);
    // If it was explicitly marked as non-generic (e.g., instantiation), keep it that way
    if (was_set_to_non_generic && stmt->is_generic) {
        stmt->is_generic = false;
    }

    if (stmt->is_generic && (stmt->is_exported || stmt->is_external)) {
        throw CompileError("Generic functions cannot be exported or external", stmt->location);
    }

    // Check if already declared (from pass 1)
    Symbol* existing = current_scope->lookup(func_name);
    if (!existing || existing->kind != Symbol::Kind::Function) {
        // Not declared yet - this must be a generic instantiation
        verify_no_shadowing(func_name, stmt->location);

        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.is_external = stmt->is_external;
        sym.is_exported = stmt->is_exported;
        sym.declaration = stmt;
        current_scope->define(func_name, sym);
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
        push_scope();

        if (stmt->ref_param_types.size() < stmt->ref_params.size()) {
            stmt->ref_param_types.resize(stmt->ref_params.size(), nullptr);
        }
        // Add receiver parameters (mutable) if this is a reference-taking function
        for (size_t i = 0; i < stmt->ref_params.size(); ++i) {
            const std::string& ref_param = stmt->ref_params[i];
            Symbol rsym;
            rsym.kind = Symbol::Kind::Variable;
            // If this is a method, infer receiver type from type namespace
            if (!stmt->type_namespace.empty() && i == 0) {
                rsym.type = Type::make_named(stmt->type_namespace, stmt->location);
            } else {
                rsym.type = make_fresh_typevar();  // Type will be inferred from usage
            }
            rsym.is_mutable = true;
            current_scope->define(ref_param, rsym);
            stmt->ref_param_types[i] = rsym.type;
        }

        // Add regular parameters (immutable)
        for (auto& param : stmt->params) {
            if (!param.type) {
                param.type = make_fresh_typevar();
            }
            Symbol psym;
            psym.kind = Symbol::Kind::Variable;
            psym.type = param.type;
            psym.is_mutable = false;
            current_scope->define(param.name, psym);
        }

        TypePtr body_type = check_expr(stmt->body);

        // Handle tuple return types
        if (!stmt->return_types.empty()) {
            // Generate anonymous tuple type: #__Tuple2_T1_T2(__0:T1, __1:T2)
            // For now, just use the first return type (simplified)
            // Full implementation would create the tuple type
            if (!stmt->return_type) {
                stmt->return_type = stmt->return_types[0];  // Placeholder
            }
        } else if (!stmt->return_type) {
            stmt->return_type = body_type;
        } else {
            if (!types_compatible(body_type, stmt->return_type)) {
                throw CompileError("Return type mismatch in function '" + stmt->func_name + "'",
                                   stmt->location);
            }
        }

        pop_scope();
    }
}

void TypeChecker::check_type_decl(StmtPtr stmt) {
    // Check if already declared (from pass 1)
    Symbol* existing = current_scope->lookup(stmt->type_decl_name);
    bool already_declared = (existing && existing->kind == Symbol::Kind::Type);

    if (!already_declared) {
        verify_no_shadowing(stmt->type_decl_name, stmt->location);

        Symbol sym;
        sym.kind = Symbol::Kind::Type;
        sym.declaration = stmt;
        current_scope->define(stmt->type_decl_name, sym);
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
    // Verify no shadowing
    verify_no_shadowing(stmt->var_name, stmt->location);

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

    // Define the variable/constant
    Symbol sym;
    sym.kind = stmt->is_mutable ? Symbol::Kind::Variable : Symbol::Kind::Constant;
    sym.type = type;
    sym.is_mutable = stmt->is_mutable;
    sym.declaration = stmt;
    current_scope->define(stmt->var_name, sym);
}

  void TypeChecker::validate_type_usage(const Module& mod, const AnalysisFacts& facts) {
    TypeUseContext ctx;
    ctx.resolve_type = [this](TypePtr type) { return resolve_type(type); };
    ctx.constexpr_condition = [this](ExprPtr expr) { return constexpr_condition(expr); };
    ::vexel::validate_type_usage(mod, facts, ctx);
}

}
