#include "typechecker.h"
#include "evaluator.h"
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
    validate_annotations(mod);

    // Two-pass checking: functions and types allow forward references, constants do not
    // Pass 1: Declare all functions and types (without checking bodies/initializers)
    for (auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            // Declare function signature only
            std::string func_name = stmt->func_name;
            if (!stmt->type_namespace.empty()) {
                func_name = stmt->type_namespace + "::" + stmt->func_name;
            }

            // Check if generic
            stmt->is_generic = is_generic_function(stmt);

            verify_no_shadowing(func_name, stmt->location);

            Symbol sym;
            sym.kind = Symbol::Kind::Function;
            sym.is_external = stmt->is_external;
            sym.is_exported = stmt->is_exported;
            sym.declaration = stmt;
            current_scope->define(func_name, sym);
        } else if (stmt->kind == Stmt::Kind::TypeDecl) {
            verify_no_shadowing(stmt->type_decl_name, stmt->location);

            Symbol sym;
            sym.kind = Symbol::Kind::Type;
            sym.declaration = stmt;
            current_scope->define(stmt->type_decl_name, sym);
        }
        // Note: Constants are NOT pre-declared - they must be defined in parse order
    }

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

void TypeChecker::check_import(StmtPtr stmt) {
    std::string resolved_path;
    if (!try_resolve_module_path(stmt->import_path, stmt->location.filename, resolved_path)) {
        throw CompileError("Import failed: cannot resolve module", stmt->location);
    }

    if (scope_loaded_modules[current_scope].count(resolved_path)) {
        return;
    }
    scope_loaded_modules[current_scope].insert(resolved_path);

    Module imported_mod = load_module_file(resolved_path);
    std::vector<StmtPtr> cloned_decls = clone_module_declarations(imported_mod.top_level);

    for (auto& decl : cloned_decls) {
        decl->scope_instance_id = current_scope->id;
        check_stmt(decl);

        std::string symbol_name;
        if (decl->kind == Stmt::Kind::FuncDecl) {
            symbol_name = decl->func_name;
            if (!decl->type_namespace.empty()) {
                symbol_name = decl->type_namespace + "::" + decl->func_name;
            }
        } else if (decl->kind == Stmt::Kind::VarDecl) {
            symbol_name = decl->var_name;
        } else if (decl->kind == Stmt::Kind::TypeDecl) {
            symbol_name = decl->type_decl_name;
        }

        if (!symbol_name.empty() && current_scope->symbols.count(symbol_name)) {
            current_scope->symbols[symbol_name].scope_instance_id = current_scope->id;
        }

        tag_scope_instances(decl, current_scope->id);

        if (current_module) {
            current_module->top_level.push_back(decl);
        }
    }
}

TypePtr TypeChecker::check_expr(ExprPtr expr) {
    if (!expr) return nullptr;

    switch (expr->kind) {
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
            expr->type = infer_literal_type(expr);
            return expr->type;

        case Expr::Kind::Identifier: {
            Symbol* sym = current_scope->lookup(expr->name);
            if (!sym) {
                throw CompileError("Undefined identifier: " + expr->name, expr->location);
            }
            if (expr->type) {
                // Type annotation provided
                return expr->type;
            }
            expr->type = sym->type;
            // Track scope instance for imported symbols
            expr->scope_instance_id = sym->scope_instance_id;
            expr->is_mutable_binding = sym->is_mutable;
            return expr->type;
        }

        case Expr::Kind::Binary:
            return check_binary(expr);

        case Expr::Kind::Unary:
            return check_unary(expr);

        case Expr::Kind::Call:
            return check_call(expr);

        case Expr::Kind::Index:
            return check_index(expr);

        case Expr::Kind::Member:
            return check_member(expr);

        case Expr::Kind::ArrayLiteral:
            return check_array_literal(expr);

        case Expr::Kind::TupleLiteral:
            return check_tuple_literal(expr);

        case Expr::Kind::Block:
            return check_block(expr);

        case Expr::Kind::Conditional:
            return check_conditional(expr);

        case Expr::Kind::Cast:
            return check_cast(expr);

        case Expr::Kind::Assignment:
            return check_assignment(expr);

        case Expr::Kind::Range:
            return check_range(expr);

        case Expr::Kind::Length:
            return check_length(expr);

        case Expr::Kind::Iteration:
            return check_iteration(expr);

        case Expr::Kind::Repeat:
            return check_repeat(expr);

        case Expr::Kind::Resource:
            return check_resource_expr(expr);

        case Expr::Kind::Process:
            return check_process_expr(expr);
    }

    return nullptr;
}

TypePtr TypeChecker::check_binary(ExprPtr expr) {
    if (expr->left && expr->left->kind == Expr::Kind::Iteration) {
        throw CompileError("Iteration expressions cannot be used inside larger expressions without parentheses", expr->left->location);
    }
    if (expr->right && expr->right->kind == Expr::Kind::Iteration) {
        throw CompileError("Iteration expressions cannot be used inside larger expressions without parentheses", expr->right->location);
    }

    TypePtr left_type = check_expr(expr->left);
    TypePtr right_type = check_expr(expr->right);

    if (expr->op == "&&" || expr->op == "||") {
        std::string context = expr->op == "&&" ? "Logical operator &&" : "Logical operator ||";
        require_boolean(left_type, expr->left->location, context);
        require_boolean(right_type, expr->right->location, context);
        expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
        return expr->type;
    }

    if (left_type && left_type->kind == Type::Kind::Named) {
        TypePtr overloaded = try_operator_overload(expr, expr->op, left_type);
        if (overloaded) {
            return overloaded;
        }
    }

    // Arithmetic operators
    if (expr->op == "+" || expr->op == "-" || expr->op == "*" || expr->op == "/") {
        TypePtr result = unify_types(left_type, right_type);
        expr->type = result;
        return result;
    }

    // Modulo and bitwise: unsigned only
    if (expr->op == "%" || expr->op == "&" || expr->op == "|" || expr->op == "^" ||
        expr->op == "<<" || expr->op == ">>") {
        require_unsigned_integer(left_type, expr->left ? expr->left->location : expr->location,
                                 "Operator " + expr->op);
        require_unsigned_integer(right_type, expr->right ? expr->right->location : expr->location,
                                 "Operator " + expr->op);

        if (expr->op == "<<" || expr->op == ">>") {
            expr->type = left_type;
            return expr->type;
        }

        expr->type = unify_types(left_type, right_type);
        return expr->type;
    }

    // Comparison operators
    if (expr->op == "==" || expr->op == "!=" || expr->op == "<" ||
        expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
        expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
        return expr->type;
    }

    // Logical operators
    if (expr->op == "&&" || expr->op == "||") {
        require_boolean(left_type, expr->left ? expr->left->location : expr->location,
                        "Logical operator " + expr->op);
        require_boolean(right_type, expr->right ? expr->right->location : expr->location,
                        "Logical operator " + expr->op);
        expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
        return expr->type;
    }

    return nullptr;
}

TypePtr TypeChecker::try_operator_overload(ExprPtr expr, const std::string& op, TypePtr left_type) {
    if (!left_type || left_type->kind != Type::Kind::Named) {
        return nullptr;
    }

    std::string func_name = left_type->type_name + "::" + op;
    Symbol* sym = current_scope->lookup(func_name);
    if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) {
        return nullptr;
    }

    if (sym->declaration->ref_params.size() != 1) {
        throw CompileError("Operator '" + op + "' on type " + left_type->type_name +
                           " must declare exactly one receiver parameter", sym->declaration->location);
    }

    size_t expected_args = 0;
    for (const auto& param : sym->declaration->params) {
        if (param.is_expression_param) {
            throw CompileError("Operator '" + op + "' on type " + left_type->type_name +
                               " cannot use expression parameters", sym->declaration->location);
        }
        expected_args++;
    }

    size_t provided_args = expr->right ? 1 : 0;
    if (provided_args != expected_args) {
        throw CompileError("Operator '" + op + "' on type " + left_type->type_name +
                           " expects " + std::to_string(expected_args) + " argument(s)", expr->location);
    }

    ExprPtr receiver_expr = expr->left;
    ExprPtr right_expr = expr->right;

    expr->kind = Expr::Kind::Call;
    expr->operand = Expr::make_identifier(op, expr->location);
    expr->receivers.clear();
    expr->receivers.push_back(receiver_expr);
    expr->args.clear();
    if (right_expr) {
        expr->args.push_back(right_expr);
    }
    expr->left = nullptr;
    expr->right = nullptr;

    return check_call(expr);
}

bool TypeChecker::try_custom_iteration(ExprPtr expr, TypePtr iterable_type) {
    if (!iterable_type || iterable_type->kind != Type::Kind::Named) {
        return false;
    }

    std::string method_token = expr->is_sorted_iteration ? "@@" : "@";
    std::string method_name = iterable_type->type_name + "::" + method_token;

    Symbol* sym = current_scope->lookup(method_name);
    if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) {
        return false;
    }

    if (sym->declaration->ref_params.size() != 1) {
        throw CompileError("Iterator method " + method_name +
                           " must declare exactly one receiver parameter", sym->declaration->location);
    }

    size_t expr_param_count = 0;
    size_t value_param_count = 0;
    for (const auto& param : sym->declaration->params) {
        if (param.is_expression_param) {
            expr_param_count++;
        } else {
            value_param_count++;
        }
    }

    if (expr_param_count != 1 || value_param_count != 0) {
        throw CompileError("Iterator method " + method_name +
                           " must take exactly one expression parameter and no value parameters", sym->declaration->location);
    }

    // Type-check loop body with '_' bound in a temporary scope
    push_scope();
    loop_depth++;
    Symbol underscore;
    underscore.kind = Symbol::Kind::Variable;
    underscore.type = make_fresh_typevar();
    underscore.is_mutable = true;
    current_scope->define("_", underscore);
    check_expr(expr->right);
    loop_depth--;
    pop_scope();

    ExprPtr receiver_expr = expr->operand;
    ExprPtr body_expr = expr->right;

    expr->kind = Expr::Kind::Call;
    expr->operand = Expr::make_identifier(method_token, expr->location);
    expr->operand->scope_instance_id = sym->scope_instance_id;
    expr->receivers.clear();
    expr->receivers.push_back(receiver_expr);
    expr->args.clear();
    expr->args.push_back(body_expr);
    expr->left = nullptr;
    expr->right = nullptr;
    expr->is_sorted_iteration = false;

    expr->type = check_call(expr);
    return true;
}

void TypeChecker::register_tuple_type(const std::string& name, const std::vector<TypePtr>& elem_types) {
    if (!forced_tuple_types.count(name)) {
        forced_tuple_types[name] = elem_types;
    }
}

TypePtr TypeChecker::check_unary(ExprPtr expr) {
    TypePtr operand_type = check_expr(expr->operand);

    if (expr->op == "-") {
        expr->type = operand_type;
        return operand_type;
    }

    if (expr->op == "!") {
        expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
        return expr->type;
    }

    if (expr->op == "~") {
        if (operand_type && operand_type->kind == Type::Kind::Primitive &&
            !is_unsigned_int(operand_type->primitive)) {
            throw CompileError("Bitwise NOT requires unsigned integer", expr->location);
        }
        expr->type = operand_type;
        return operand_type;
    }

    return operand_type;
}

TypePtr TypeChecker::check_call(ExprPtr expr) {
    std::vector<TypePtr> receiver_types;
    if (!expr->receivers.empty()) {
        receiver_types.reserve(expr->receivers.size());
        bool multi_receiver = expr->receivers.size() > 1;
        for (const auto& rec : expr->receivers) {
            if (multi_receiver && rec && rec->kind != Expr::Kind::Identifier) {
                throw CompileError("Multi-receiver calls require identifier receivers", expr->location);
            }
            receiver_types.push_back(check_expr(rec));
        }
    }

    Symbol* sym = nullptr;
    std::string func_name;
    bool has_symbol = false;

    if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
        func_name = expr->operand->name;

        if (expr->receivers.size() == 1) {
            TypePtr receiver_type = receiver_types.empty() ? nullptr : receiver_types[0];
            if (receiver_type && receiver_type->kind == Type::Kind::Named) {
                if (expr->operand->name.find("::") == std::string::npos) {
                    func_name = receiver_type->type_name + "::" + expr->operand->name;
                    expr->operand->name = func_name;
                } else {
                    func_name = expr->operand->name;
                }
            }
        }

        sym = current_scope->lookup(func_name);
        if (!sym) {
            throw CompileError("Undefined function: " + func_name, expr->location);
        }

        expr->operand->scope_instance_id = sym->scope_instance_id;
        has_symbol = true;
    }

    for (size_t i = 0; i < expr->args.size(); i++) {
        bool skip = false;
        if (has_symbol && sym->kind == Symbol::Kind::Function && sym->declaration &&
            i < sym->declaration->params.size() && sym->declaration->params[i].is_expression_param) {
            skip = true;
        }

        if (!skip) {
            check_expr(expr->args[i]);
        }
    }

    if (!has_symbol) {
        expr->type = make_fresh_typevar();
        return expr->type;
    }

    if (sym->kind == Symbol::Kind::Type && sym->declaration) {
        for (size_t i = 0; i < expr->args.size() && i < sym->declaration->fields.size(); i++) {
            if (!sym->declaration->fields[i].type ||
                sym->declaration->fields[i].type->kind == Type::Kind::TypeVar) {
                sym->declaration->fields[i].type = expr->args[i]->type;
            }
        }

        expr->type = Type::make_named(expr->operand->name, expr->location);
        return expr->type;
    }

    if (sym->kind == Symbol::Kind::Function && sym->declaration) {
        size_t expected_receivers = sym->declaration->ref_params.size();
        size_t provided_receivers = expr->receivers.size();
        if (expected_receivers != provided_receivers) {
            if (expected_receivers == 0) {
                if (provided_receivers > 0) {
                    throw CompileError("Function '" + sym->declaration->func_name +
                                       "' does not accept receiver arguments", expr->location);
                }
            } else {
                throw CompileError(
                    "Function '" + sym->declaration->func_name + "' requires " +
                    std::to_string(expected_receivers) + " receiver(s)", expr->location);
            }
        }

        // Reconcile receiver parameter types with the provided receivers
        if (!sym->declaration->ref_params.empty()) {
            if (sym->declaration->ref_param_types.size() < sym->declaration->ref_params.size()) {
                sym->declaration->ref_param_types.resize(sym->declaration->ref_params.size(), nullptr);
            }
            for (size_t i = 0; i < sym->declaration->ref_params.size() && i < receiver_types.size(); ++i) {
                TypePtr recv_type = receiver_types[i];
                TypePtr& param_type = sym->declaration->ref_param_types[i];

                if (!param_type || param_type->kind == Type::Kind::TypeVar) {
                    param_type = recv_type;
                } else if (!types_compatible(recv_type, param_type)) {
                    throw CompileError(
                        "Receiver '" + sym->declaration->ref_params[i] + "' expects type " +
                        param_type->to_string(), expr->location);
                }
            }
        }

        bool is_generic_func = sym->declaration->is_generic;

        // Validate argument count (skip expression parameters)
        size_t expected_args = sym->declaration->params.size();
        if (expr->args.size() != expected_args) {
            throw CompileError(
                "Function '" + sym->declaration->func_name + "' expects " +
                std::to_string(expected_args) + " argument(s)", expr->location);
        }

        if (is_generic_func) {
            std::vector<TypePtr> arg_types;
            for (size_t i = 0; i < expr->args.size() && i < sym->declaration->params.size(); i++) {
                ExprPtr arg_expr = expr->args[i];
                TypePtr param_type = sym->declaration->params[i].type;
                if (param_type && param_type->kind != Type::Kind::TypeVar) {
                    if (!types_compatible(arg_expr->type, param_type) &&
                        !literal_assignable_to(param_type, arg_expr)) {
                        throw CompileError(
                            "Type mismatch for parameter '" + sym->declaration->params[i].name +
                            "' in call to '" + sym->declaration->func_name + "'", expr->location);
                    }
                }

                if (!sym->declaration->params[i].is_expression_param) {
                    arg_types.push_back(arg_expr->type);
                }
            }

            std::string mangled_name = get_or_create_instantiation(func_name, arg_types, sym->declaration);
            expr->operand->name = mangled_name;

            TypeSignature sig;
            sig.param_types = arg_types;
            auto func_it = instantiations.find(func_name);
            if (func_it != instantiations.end()) {
                auto inst_it = func_it->second.find(sig);
                if (inst_it != func_it->second.end()) {
                    expr->type = inst_it->second.declaration->return_type;
                    return expr->type;
                }
            }

            expr->type = make_fresh_typevar();
            return expr->type;
        }

        // Validate argument types against parameter types
        for (size_t i = 0; i < sym->declaration->params.size(); ++i) {
            auto& param = sym->declaration->params[i];
            ExprPtr arg_expr = expr->args[i];
            if (param.is_expression_param) {
                continue;
            }

            TypePtr param_type = param.type;

            if (!param_type || param_type->kind == Type::Kind::TypeVar) {
                // Infer generic parameter types from arguments
                if (!param.type) {
                    sym->declaration->params[i].type = arg_expr->type;
                } else if (param_type && param_type->kind == Type::Kind::TypeVar) {
                    sym->declaration->params[i].type = unify_types(param_type, arg_expr->type);
                }
                continue;
            }

            if (!types_compatible(arg_expr->type, param_type) &&
                !literal_assignable_to(param_type, arg_expr)) {
                throw CompileError(
                    "Type mismatch for parameter '" + param.name + "' in call to '" +
                    sym->declaration->func_name + "'", arg_expr->location);
            }
        }

        if (!sym->declaration->return_types.empty()) {
            std::string tuple_type_name = std::string(TUPLE_TYPE_PREFIX) + std::to_string(sym->declaration->return_types.size());
            for (const auto& rt : sym->declaration->return_types) {
                tuple_type_name += "_";
                if (rt) {
                    tuple_type_name += rt->to_string();
                } else {
                    tuple_type_name += "unknown";
                }
            }
            register_tuple_type(tuple_type_name, sym->declaration->return_types);
            expr->type = Type::make_named(tuple_type_name, expr->location);
            return expr->type;
        }

        if (sym->declaration->return_type) {
            expr->type = sym->declaration->return_type;
            return expr->type;
        }
    }

    expr->type = make_fresh_typevar();
    return expr->type;
}

TypePtr TypeChecker::check_index(ExprPtr expr) {
    TypePtr arr_type = check_expr(expr->operand);
    check_expr(expr->args[0]);

    if (arr_type && arr_type->kind == Type::Kind::Array) {
        expr->type = arr_type->element_type;
        return expr->type;
    }

    if (arr_type && arr_type->kind == Type::Kind::Primitive &&
        arr_type->primitive == PrimitiveType::String) {
        expr->type = Type::make_primitive(PrimitiveType::U8, expr->location);
        return expr->type;
    }

    expr->type = make_fresh_typevar();
    return expr->type;
}

TypePtr TypeChecker::check_member(ExprPtr expr) {
    TypePtr obj_type = check_expr(expr->operand);

    // Look up the object's type definition if it's a named type
    if (obj_type && obj_type->kind == Type::Kind::Named) {
        // Special handling for tuple types (synthetic types starting with __Tuple)
        if (obj_type->type_name.find(TUPLE_TYPE_PREFIX) == 0 &&
            expr->name.size() >= 3 && expr->name.substr(0, 2) == std::string(MANGLED_PREFIX)) {
            std::string index_str = expr->name.substr(2);
            size_t field_index = std::stoull(index_str);

            auto tuple_it = forced_tuple_types.find(obj_type->type_name);
            if (tuple_it != forced_tuple_types.end()) {
                if (field_index >= tuple_it->second.size()) {
                    throw CompileError("Tuple field index out of bounds: " + expr->name, expr->location);
                }
                expr->type = tuple_it->second[field_index];
                return expr->type;
            }

            // Fallback: parse tuple type name (__TupleN_T1_T2_...) to derive field types
            std::string type_name = obj_type->type_name;
            size_t prefix_len = std::string(TUPLE_TYPE_PREFIX).length();
            size_t pos = type_name.find('_', prefix_len);
            if (pos == std::string::npos) {
                throw CompileError("Malformed tuple type name: " + type_name, expr->location);
            }

            std::vector<std::string> field_type_names;
            pos++;
            while (pos < type_name.length()) {
                size_t next_underscore = type_name.find('_', pos);
                if (next_underscore == std::string::npos) {
                    field_type_names.push_back(type_name.substr(pos));
                    break;
                }
                field_type_names.push_back(type_name.substr(pos, next_underscore - pos));
                pos = next_underscore + 1;
            }

            if (field_index >= field_type_names.size()) {
                throw CompileError("Tuple field index out of bounds: " + expr->name, expr->location);
            }

            std::string field_type_str = field_type_names[field_index];
            TypePtr field_type = parse_type_from_string(field_type_str, expr->location);
            expr->type = field_type;
            return expr->type;
        }

        Symbol* type_sym = current_scope->lookup(obj_type->type_name);
        if (type_sym && type_sym->kind == Symbol::Kind::Type && type_sym->declaration) {
            // Find the field in the type declaration
            for (const auto& field : type_sym->declaration->fields) {
                if (field.name == expr->name) {
                    expr->type = field.type;
                    return expr->type;
                }
            }
            throw CompileError("Type " + obj_type->type_name + " has no field: " + expr->name, expr->location);
        }
    }

    // Fallback to type variable if we can't determine field type
    expr->type = make_fresh_typevar();
    return expr->type;
}

TypePtr TypeChecker::check_array_literal(ExprPtr expr) {
    if (expr->elements.empty()) {
        TypePtr elem_type = make_fresh_typevar();
        ExprPtr size = Expr::make_int(0, expr->location);
        expr->type = Type::make_array(elem_type, size, expr->location);
        return expr->type;
    }

    TypePtr elem_type = nullptr;
    for (auto& elem : expr->elements) {
        TypePtr et = check_expr(elem);
        if (!elem_type) {
            elem_type = et;
        } else {
            elem_type = unify_types(elem_type, et);
        }
    }

    auto size = Expr::make_int(expr->elements.size(), expr->location);
    expr->type = Type::make_array(elem_type, size, expr->location);

    return expr->type;
}

TypePtr TypeChecker::check_tuple_literal(ExprPtr expr) {
    if (expr->elements.size() < 2) {
        throw CompileError("Tuple literal must have at least 2 elements", expr->location);
    }

    // Type check each element
    std::vector<TypePtr> element_types;
    for (auto& elem : expr->elements) {
        element_types.push_back(check_expr(elem));
    }

    // Generate anonymous tuple type name: __TupleN_T1_T2_...
    std::string type_name = std::string(TUPLE_TYPE_PREFIX) + std::to_string(expr->elements.size());
    for (auto& et : element_types) {
        type_name += "_";
        if (et) {
            type_name += et->to_string();
        } else {
            type_name += "unknown";
        }
    }

    register_tuple_type(type_name, element_types);

    // Create named type (the actual struct will be generated in codegen)
    expr->type = Type::make_named(type_name, expr->location);
    return expr->type;
}

TypePtr TypeChecker::check_block(ExprPtr expr) {
    push_scope();
    for (auto& stmt : expr->statements) {
        check_stmt(stmt);
    }
    TypePtr result_type = nullptr;
    if (expr->result_expr) {
        result_type = check_expr(expr->result_expr);
    }
    pop_scope();
    expr->type = result_type;
    return result_type;
}

TypePtr TypeChecker::check_conditional(ExprPtr expr) {
    TypePtr cond_type = check_expr(expr->condition);
    require_boolean(cond_type, expr->condition ? expr->condition->location : expr->location,
                    "Conditional expression");

    auto static_value = evaluate_static_condition(expr->condition);
    if (static_value.has_value()) {
        if (static_value.value()) {
            expr->type = check_expr(expr->true_expr);
        } else {
            expr->type = check_expr(expr->false_expr);
        }
        return expr->type;
    }

    TypePtr true_type = check_expr(expr->true_expr);
    TypePtr false_type = check_expr(expr->false_expr);

    if (types_equal(true_type, false_type)) {
        expr->type = true_type;
        return expr->type;
    }

    bool primitive_family_match =
        true_type && false_type &&
        true_type->kind == Type::Kind::Primitive &&
        false_type->kind == Type::Kind::Primitive &&
        types_in_same_family(true_type, false_type);

    if (primitive_family_match) {
        expr->type = unify_types(true_type, false_type);
        return expr->type;
    }

    std::string lhs = true_type ? true_type->to_string() : "<unknown>";
    std::string rhs = false_type ? false_type->to_string() : "<unknown>";
    throw CompileError("Conditional branches must have matching types at runtime (type mismatch: " + lhs + " vs " + rhs + ")",
                       expr->location);
}
std::optional<bool> TypeChecker::evaluate_static_condition(ExprPtr expr) {
    std::unordered_set<const Stmt*> visiting;
    auto helper = [&](auto&& self, ExprPtr node) -> std::optional<bool> {
        if (!node) return std::nullopt;
        switch (node->kind) {
            case Expr::Kind::IntLiteral:
                return node->uint_val != 0;
            case Expr::Kind::Identifier: {
                Symbol* sym = current_scope->lookup(node->name);
                if (!sym) return std::nullopt;
                if (sym->kind == Symbol::Kind::Constant && sym->declaration && sym->declaration->var_init) {
                    const Stmt* key = sym->declaration.get();
                    if (visiting.count(key)) return std::nullopt;
                    visiting.insert(key);
                    auto res = self(self, sym->declaration->var_init);
                    visiting.erase(key);
                    return res;
                }
                return std::nullopt;
            }
            default:
                return std::nullopt;
        }
    };
    return helper(helper, expr);
}

TypePtr TypeChecker::check_cast(ExprPtr expr) {
    TypePtr operand_type = check_expr(expr->operand);
    TypePtr target_type = expr->target_type;

    // Special-case: casting packed bool arrays to unsigned integers requires matching size
    if (target_type && target_type->kind == Type::Kind::Primitive &&
        is_unsigned_int(target_type->primitive) &&
        operand_type && operand_type->kind == Type::Kind::Array &&
        operand_type->element_type &&
        operand_type->element_type->kind == Type::Kind::Primitive &&
        operand_type->element_type->primitive == PrimitiveType::Bool) {

        uint64_t count = 0;
        if (operand_type->array_size && operand_type->array_size->kind == Expr::Kind::IntLiteral) {
            count = operand_type->array_size->uint_val;
        }
        if (count != static_cast<uint64_t>(type_bits(target_type->primitive))) {
            throw CompileError("Boolean array size mismatch for cast to #" +
                               primitive_name(target_type->primitive), expr->location);
        }
    }

    expr->type = target_type;
    return expr->type;
}

TypePtr TypeChecker::check_assignment(ExprPtr expr) {
    // Check if LHS is a new variable declaration (identifier that doesn't exist yet)
    if (expr->left->kind == Expr::Kind::Identifier &&
        !current_scope->lookup(expr->left->name)) {
        // This is a local variable declaration with initialization

        // Verify no shadowing (underscore is allowed to shadow)
        if (expr->left->name != "_") {
            verify_no_shadowing(expr->left->name, expr->location);
        }

        // Check if RHS is a function reference (not allowed)
        if (expr->right->kind == Expr::Kind::Identifier) {
            Symbol* rhs_sym = current_scope->lookup(expr->right->name);
            if (rhs_sym && rhs_sym->kind == Symbol::Kind::Function) {
                throw CompileError("Cannot assign function to variable (no function types): " + expr->right->name, expr->location);
            }
        }

        TypePtr rhs_type = check_expr(expr->right);
        TypePtr var_type = expr->left->type ? expr->left->type : rhs_type;
        if (expr->right->kind == Expr::Kind::Cast && expr->left->type) {
            rhs_type = var_type;
        }

        if (expr->left->type &&
            var_type && var_type->kind == Type::Kind::Array &&
            expr->right->kind == Expr::Kind::ArrayLiteral) {
            bool compatible = true;
            for (auto& el : expr->right->elements) {
                if (!types_compatible(el->type, var_type->element_type) &&
                    !literal_assignable_to(var_type->element_type, el)) {
                    compatible = false;
                    break;
                }
            }
            if (compatible) {
                rhs_type = var_type;
            }
        }

        if (expr->left->type && !types_compatible(rhs_type, var_type)) {
            if (expr->right->kind == Expr::Kind::Cast) {
                expr->right->type = var_type;
            } else if (literal_assignable_to(var_type, expr->right)) {
                expr->right->type = var_type;
            } else {
                throw CompileError("Type mismatch in variable initialization", expr->location);
            }
        }

        // Define the new variable in current scope
        Symbol sym;
        sym.kind = Symbol::Kind::Variable;
        sym.type = var_type;
        sym.is_mutable = true;
        sym.is_external = false;
        sym.is_exported = false;
        sym.declaration = nullptr;
        current_scope->define(expr->left->name, sym);

        // Mark that this assignment creates a new variable
        expr->left->type = nullptr;
        expr->creates_new_variable = true;

        expr->type = var_type;
        return var_type;
    }

    // Regular assignment to existing variable
    // If a prior declaration left a type annotation on the identifier node, clear it now
    if (expr->left->kind == Expr::Kind::Identifier && expr->left->type) {
        expr->left->type = nullptr;
    }

    // Check if trying to assign to an immutable constant
    if (expr->left->kind == Expr::Kind::Identifier) {
        Symbol* sym = current_scope->lookup(expr->left->name);
        if (sym && !sym->is_mutable) {
            std::string name = expr->left->name;
            if (name == "_") {
                throw CompileError("Cannot assign to read-only loop variable '_'", expr->location);
            }
            throw CompileError("Cannot assign to immutable constant: " + name, expr->location);
        }
    }

    // Check if RHS is a function reference (not allowed)
    if (expr->right->kind == Expr::Kind::Identifier) {
        Symbol* rhs_sym = current_scope->lookup(expr->right->name);
        if (rhs_sym && rhs_sym->kind == Symbol::Kind::Function) {
            throw CompileError("Cannot assign function to variable (no function types): " + expr->right->name, expr->location);
        }
    }

    TypePtr lhs_type = check_expr(expr->left);
    TypePtr rhs_type = check_expr(expr->right);

    if (expr->left->kind == Expr::Kind::TupleLiteral && expr->right->kind != Expr::Kind::TupleLiteral) {
        throw CompileError("Arity mismatch in multi-assignment", expr->location);
    }

    if (!types_compatible(rhs_type, lhs_type)) {
        if (literal_assignable_to(lhs_type, expr->right)) {
            expr->type = lhs_type;
            return lhs_type;
        }
        throw CompileError("Type mismatch in assignment", expr->location);
    }

    // This is assignment to existing variable
    expr->creates_new_variable = false;

    expr->type = lhs_type;
    return lhs_type;
}

TypePtr TypeChecker::check_range(ExprPtr expr) {
    TypePtr start_type = check_expr(expr->left);
    TypePtr end_type = check_expr(expr->right);

    // Check if bounds are equal (would create empty array)
    if (expr->left->kind == Expr::Kind::IntLiteral &&
        expr->right->kind == Expr::Kind::IntLiteral) {
        if (expr->left->uint_val == expr->right->uint_val) {
            throw CompileError("Range with equal bounds (a..a) would produce empty array", expr->location);
        }
    }

    auto fold_const = [&](ExprPtr e, uint64_t& out) -> bool {
        if (e->kind == Expr::Kind::IntLiteral) {
            out = e->uint_val;
            return true;
        }
        return false;
    };
    uint64_t start_val = 0, end_val = 0;
    if (!fold_const(expr->left, start_val) || !fold_const(expr->right, end_val)) {
        throw CompileError("Range bounds must be compile-time constants", expr->location);
    }

    TypePtr elem_type = unify_types(start_type, end_type);
    uint64_t count = (start_val <= end_val) ? (end_val - start_val) : (start_val - end_val);
    ExprPtr size = Expr::make_int(count, expr->location);
    expr->type = Type::make_array(elem_type, size, expr->location);
    return expr->type;
}

TypePtr TypeChecker::check_length(ExprPtr expr) {
    check_expr(expr->operand);
    expr->type = Type::make_primitive(PrimitiveType::I32, expr->location);
    return expr->type;
}

TypePtr TypeChecker::check_iteration(ExprPtr expr) {
    if (expr->operand->kind == Expr::Kind::Assignment) {
        throw CompileError("Iteration expressions cannot be used inside larger expressions without parentheses", expr->operand->location);
    }

    TypePtr iterable_type = check_expr(expr->operand);

    if (try_custom_iteration(expr, iterable_type)) {
        return expr->type;
    }

    if (!iterable_type || iterable_type->kind != Type::Kind::Array) {
        if (iterable_type && iterable_type->kind == Type::Kind::Named) {
            std::string type_name = iterable_type->type_name;
            std::string method = expr->is_sorted_iteration ? "@@" : "@";
            throw CompileError("Type " + type_name + " is not iterable (missing &(self)#" +
                               type_name + "::" + method + "($loop))", expr->operand->location);
        }
        throw CompileError("Expression is not iterable (expected array, range, or custom @/@@" +
                           std::string(" iterator)"), expr->operand->location);
    }

    if (expr->is_sorted_iteration && !iterable_type->element_type) {
        throw CompileError("Cannot sort iteration over array with unknown element type", expr->location);
    }

    push_scope();
    loop_depth++;
    Symbol underscore;
    underscore.kind = Symbol::Kind::Variable;
    underscore.type = iterable_type && iterable_type->element_type
        ? iterable_type->element_type
        : make_fresh_typevar();
    underscore.is_mutable = false;
    current_scope->define("_", underscore);

    check_expr(expr->right);
    loop_depth--;
    pop_scope();

    expr->type = nullptr;
    return nullptr;
}

TypePtr TypeChecker::check_repeat(ExprPtr expr) {
    TypePtr cond_type = check_expr(expr->condition);
    require_boolean(cond_type, expr->condition ? expr->condition->location : expr->location,
                    "Repeat loop");
    loop_depth++;
    check_expr(expr->right);
    loop_depth--;
    expr->type = nullptr;
    return nullptr;
}

TypePtr TypeChecker::check_resource_expr(ExprPtr expr) {
    std::string resolved;
    bool found = try_resolve_resource_path(expr->resource_path, expr->location.filename, resolved);
    const std::string tuple_name = std::string(TUPLE_TYPE_PREFIX) + "2_#s_#s";
    auto register_resource_tuple = [&](const SourceLocation& loc) {
        std::vector<TypePtr> elem_types = {
            Type::make_primitive(PrimitiveType::String, loc),
            Type::make_primitive(PrimitiveType::String, loc)
        };
        register_tuple_type(tuple_name, elem_types);
    };
    auto make_empty_directory_result = [&](const SourceLocation& loc) -> TypePtr {
        register_resource_tuple(loc);
        expr->kind = Expr::Kind::ArrayLiteral;
        expr->elements.clear();
        ExprPtr size_expr = Expr::make_int(0, loc);
        expr->type = Type::make_array(Type::make_named(tuple_name, loc), size_expr, loc);
        return expr->type;
    };

    std::filesystem::path path;
    if (found) {
        path = std::filesystem::path(resolved);
    } else {
        std::string logical = join_import_path(expr->resource_path);
        if (!project_root.empty()) {
            path = std::filesystem::path(project_root) / logical;
        } else {
            path = logical;
        }
    }

    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            if (entry.is_regular_file()) {
                entries.push_back(entry);
            }
        }
        register_resource_tuple(expr->location);
        if (entries.empty()) {
            return make_empty_directory_result(expr->location);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.path().filename().string() < b.path().filename().string();
        });

        std::vector<ExprPtr> elements;
        elements.reserve(entries.size());
        for (const auto& entry : entries) {
            std::ifstream file(entry.path(), std::ios::binary);
            if (!file) {
                throw CompileError("Cannot open resource file: " + entry.path().string(), expr->location);
            }
            std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            ExprPtr literal = Expr::make_string(data, expr->location);
            literal->type = Type::make_primitive(PrimitiveType::String, expr->location);
            ExprPtr name_literal = Expr::make_string(entry.path().filename().string(), expr->location);
            name_literal->type = Type::make_primitive(PrimitiveType::String, expr->location);
            ExprPtr record = Expr::make_tuple({name_literal, literal}, expr->location);
            elements.push_back(record);
        }

        ExprPtr array_literal = Expr::make_array(elements, expr->location);
        *expr = *array_literal;
        return check_array_literal(expr);
    }

    if (std::filesystem::is_regular_file(path, ec)) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw CompileError("Cannot open resource: " + path.string(), expr->location);
        }
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        ExprPtr literal = Expr::make_string(data, expr->location);
        *expr = *literal;
        return check_expr(expr);
    }

    return make_empty_directory_result(expr->location);
}

static std::string run_process_command(const std::string& command, const SourceLocation& loc) {
    std::array<char, 256> buffer{};
    std::string result;
    // Intentional: process expressions are executed via the host shell. Callers are responsible for
    // trusting or sanitizing the Vexel source that supplies the command string.
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw CompileError("Failed to execute command: " + command, loc);
    }
    try {
        while (fgets(buffer.data(), buffer.size(), pipe)) {
            result += buffer.data();
        }
        int rc = pclose(pipe);
        if (rc == -1) {
            throw CompileError("Failed to close command: " + command, loc);
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    return result;
}

TypePtr TypeChecker::check_process_expr(ExprPtr expr) {
    if (!allow_process) {
        throw CompileError("Process expressions are disabled (enable with --allow-process)", expr->location);
    }
    std::string output = run_process_command(expr->process_command, expr->location);
    ExprPtr literal = Expr::make_string(output, expr->location);
    literal->type = Type::make_primitive(PrimitiveType::String, expr->location);
    *expr = *literal;
    return literal->type;
}

bool TypeChecker::types_equal(TypePtr a, TypePtr b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case Type::Kind::Primitive:
            return a->primitive == b->primitive;
        case Type::Kind::Array:
            // Arrays are equal if element types match AND sizes match
            if (!types_equal(a->element_type, b->element_type)) {
                return false;
            }
            // Check array sizes if both are known
            if (a->array_size && b->array_size &&
                a->array_size->kind == Expr::Kind::IntLiteral &&
                b->array_size->kind == Expr::Kind::IntLiteral) {
                return a->array_size->uint_val == b->array_size->uint_val;
            }
            return true; // Unknown sizes are considered equal
        case Type::Kind::Named:
            return a->type_name == b->type_name;
        case Type::Kind::TypeVar:
            return a->var_name == b->var_name;
    }
    return false;
}

bool TypeChecker::types_compatible(TypePtr a, TypePtr b) {
    if (types_equal(a, b)) return true;
    if (!a || !b) return true; // Type variable
    if ((a && a->kind == Type::Kind::TypeVar) || (b && b->kind == Type::Kind::TypeVar)) {
        return true;
    }

    // Array compatibility - check element types and sizes
    if (a->kind == Type::Kind::Array && b->kind == Type::Kind::Array) {
        // Element types must be compatible
        if (!types_compatible(a->element_type, b->element_type)) {
            return false;
        }

        // Strict array size checking
        if (a->array_size && b->array_size) {
            // Both sizes known - must match exactly
            if (a->array_size->kind == Expr::Kind::IntLiteral &&
                b->array_size->kind == Expr::Kind::IntLiteral) {
                if (a->array_size->uint_val != b->array_size->uint_val) {
                    return false;
                }
            }
        }

        return true;
    }

    // Enhanced type promotion for primitives
    if (a->kind == Type::Kind::Primitive && b->kind == Type::Kind::Primitive) {
        // Same type family check
        if (types_in_same_family(a, b)) {
            return type_bits(a->primitive) <= type_bits(b->primitive);
        }

        // No cross-category promotions (need explicit cast)
        return false;
    }

    return false;
}

TypePtr TypeChecker::unify_types(TypePtr a, TypePtr b) {
    if (!a) return b;
    if (!b) return a;
    if (types_equal(a, b)) return a;

    // Enhanced type unification respecting type families
    if (a->kind == Type::Kind::Primitive && b->kind == Type::Kind::Primitive) {
        // Same family - promote to larger
        if (types_in_same_family(a, b)) {
            if (type_bits(a->primitive) <= type_bits(b->primitive)) {
                return b;
            }
            return a;
        }

        // Different families - no automatic unification (return first type)
        return a;
    }

    return a;
}

TypePtr TypeChecker::infer_literal_type(ExprPtr expr) {
    switch (expr->kind) {
        case Expr::Kind::IntLiteral: {
            uint64_t raw = expr->uint_val;
            if (expr->raw_literal == "true" || expr->raw_literal == "false") {
                return Type::make_primitive(PrimitiveType::Bool, expr->location);
            }

            if (expr->literal_is_unsigned) {
                if (raw <= 0xFFull) return Type::make_primitive(PrimitiveType::U8, expr->location);
                if (raw <= 0xFFFFull) return Type::make_primitive(PrimitiveType::U16, expr->location);
                if (raw <= 0xFFFFFFFFull) return Type::make_primitive(PrimitiveType::U32, expr->location);
                return Type::make_primitive(PrimitiveType::U64, expr->location);
            }

            int64_t val = static_cast<int64_t>(raw);
            if (val >= -128 && val <= 127) return Type::make_primitive(PrimitiveType::I8, expr->location);
            if (val >= -32768 && val <= 32767) return Type::make_primitive(PrimitiveType::I16, expr->location);
            if (val >= -2147483648LL && val <= 2147483647LL) return Type::make_primitive(PrimitiveType::I32, expr->location);
            return Type::make_primitive(PrimitiveType::I64, expr->location);
        }
        case Expr::Kind::FloatLiteral:
            return Type::make_primitive(PrimitiveType::F64, expr->location);
        case Expr::Kind::StringLiteral:
            return Type::make_primitive(PrimitiveType::String, expr->location);
        case Expr::Kind::CharLiteral:
            return Type::make_primitive(PrimitiveType::U8, expr->location);
        default:
            return nullptr;
    }
}

bool TypeChecker::literal_assignable_to(TypePtr target, ExprPtr expr) {
    if (!target || target->kind != Type::Kind::Primitive || !expr) return false;

    // Do not implicitly widen boolean-typed literals to non-boolean targets
    if (expr->type && expr->type->kind == Type::Kind::Primitive &&
        expr->type->primitive == PrimitiveType::Bool &&
        target->primitive != PrimitiveType::Bool) {
        return false;
    }

    auto fits_signed = [&](int64_t minv, int64_t maxv) {
        int64_t v = static_cast<int64_t>(expr->uint_val);
        if (expr->literal_is_unsigned && expr->uint_val > static_cast<uint64_t>(INT64_MAX)) {
            return false;
        }
        return v >= minv && v <= maxv;
    };
    auto fits_unsigned = [&](uint64_t maxv) {
        if (!expr->literal_is_unsigned) {
            int64_t v = static_cast<int64_t>(expr->uint_val);
            if (v < 0) return false;
            return static_cast<uint64_t>(v) <= maxv;
        }
        return expr->uint_val <= maxv;
    };

    // Treat character literals like unsigned bytes
    Expr::Kind kind = expr->kind;
    if (kind == Expr::Kind::CharLiteral) {
        kind = Expr::Kind::IntLiteral;
    }

    if (kind == Expr::Kind::IntLiteral) {
        switch (target->primitive) {
            case PrimitiveType::Bool:
                return fits_unsigned(1);
            case PrimitiveType::I8:
                return fits_signed(-128, 127);
            case PrimitiveType::I16:
                return fits_signed(-32768, 32767);
            case PrimitiveType::I32:
                return fits_signed(-2147483648LL, 2147483647LL);
            case PrimitiveType::I64:
                // If unsigned literal exceeds max int64, it does not fit
                if (expr->literal_is_unsigned && expr->uint_val > static_cast<uint64_t>(INT64_MAX)) {
                    return false;
                }
                return true;
            case PrimitiveType::U8:
                return fits_unsigned(0xFFull);
            case PrimitiveType::U16:
                return fits_unsigned(0xFFFFull);
            case PrimitiveType::U32:
                return fits_unsigned(0xFFFFFFFFull);
            case PrimitiveType::U64:
                return !expr->literal_is_unsigned
                    ? fits_unsigned(std::numeric_limits<uint64_t>::max())
                    : true;
            case PrimitiveType::F32:
            case PrimitiveType::F64:
                return true; // Integer literals can widen to floats
            case PrimitiveType::String:
                return false;
        }
    }

    if (kind == Expr::Kind::FloatLiteral) {
        if (target->primitive == PrimitiveType::F32 ||
            target->primitive == PrimitiveType::F64) {
            return true;
        }
    }

    return false;
}

TypePtr TypeChecker::make_fresh_typevar() {
    return Type::make_typevar("T" + std::to_string(type_var_counter++), SourceLocation());
}

void TypeChecker::verify_no_shadowing(const std::string& name, const SourceLocation& loc) {
    if (name == "_") return; // Underscore can shadow

    Symbol* existing = current_scope->lookup(name);
    if (existing) {
        throw CompileError("Name shadows existing definition: " + name, loc);
    }
}

TypePtr TypeChecker::parse_type_from_string(const std::string& type_str, const SourceLocation& loc) {
    // Parse primitive types
    if (type_str == "i8") return Type::make_primitive(PrimitiveType::I8, loc);
    if (type_str == "i16") return Type::make_primitive(PrimitiveType::I16, loc);
    if (type_str == "i32") return Type::make_primitive(PrimitiveType::I32, loc);
    if (type_str == "i64") return Type::make_primitive(PrimitiveType::I64, loc);
    if (type_str == "u8") return Type::make_primitive(PrimitiveType::U8, loc);
    if (type_str == "u16") return Type::make_primitive(PrimitiveType::U16, loc);
    if (type_str == "u32") return Type::make_primitive(PrimitiveType::U32, loc);
    if (type_str == "u64") return Type::make_primitive(PrimitiveType::U64, loc);
    if (type_str == "f32") return Type::make_primitive(PrimitiveType::F32, loc);
    if (type_str == "f64") return Type::make_primitive(PrimitiveType::F64, loc);
    if (type_str == "b") return Type::make_primitive(PrimitiveType::Bool, loc);
    if (type_str == "s") return Type::make_primitive(PrimitiveType::String, loc);

    // For now, return type variable for complex types
    // TODO: Handle array types, named types, etc.
    return Type::make_named(type_str, loc);
}

TypeChecker::TypeFamily TypeChecker::get_type_family(TypePtr type) {
    if (!type || type->kind != Type::Kind::Primitive) return TypeFamily::Other;

    if (is_signed_int(type->primitive)) return TypeFamily::Signed;
    if (is_unsigned_int(type->primitive)) return TypeFamily::Unsigned;
    if (is_float(type->primitive)) return TypeFamily::Float;
    return TypeFamily::Other;
}

bool TypeChecker::types_in_same_family(TypePtr a, TypePtr b) {
    return get_type_family(a) == get_type_family(b) &&
           get_type_family(a) != TypeFamily::Other;
}

bool TypeChecker::is_generic_function(StmtPtr func) {
    if (!func || func->kind != Stmt::Kind::FuncDecl) return false;

    bool has_untyped_param = false;
    for (const auto& param : func->params) {
        if ((!param.type || param.type->kind == Type::Kind::TypeVar) && !param.is_expression_param) {
            has_untyped_param = true;
            break;
        }
    }

    bool has_typevar_return = false;
    if (!func->return_types.empty()) {
        for (const auto& rt : func->return_types) {
            if (!rt || rt->kind == Type::Kind::TypeVar) {
                has_typevar_return = true;
                break;
            }
        }
    } else if (func->return_type && func->return_type->kind == Type::Kind::TypeVar) {
        has_typevar_return = true;
    }

    return has_untyped_param || has_typevar_return;
}

bool TypeChecker::try_resolve_relative_path(const std::string& relative, const std::string& current_file, std::string& out_path) {
    std::filesystem::path rel_path(relative);

    if (!project_root.empty()) {
        std::filesystem::path full = std::filesystem::path(project_root) / rel_path;
        if (std::filesystem::exists(full)) {
            out_path = full.string();
            return true;
        }
    }

    if (!current_file.empty()) {
        std::filesystem::path current_dir = std::filesystem::path(current_file).parent_path();
        if (!current_dir.empty()) {
            std::filesystem::path full = current_dir / rel_path;
            if (std::filesystem::exists(full)) {
                out_path = full.string();
                return true;
            }
        }
    }

    return false;
}

bool TypeChecker::try_resolve_module_path(const std::vector<std::string>& import_path, const std::string& current_file, std::string& out_path) {
    std::string relative = join_import_path(import_path) + ".vx";
    return try_resolve_relative_path(relative, current_file, out_path);
}

bool TypeChecker::try_resolve_resource_path(const std::vector<std::string>& import_path, const std::string& current_file, std::string& out_path) {
    std::string relative = join_import_path(import_path);
    return try_resolve_relative_path(relative, current_file, out_path);
}

std::string TypeChecker::join_import_path(const std::vector<std::string>& import_path) {
    std::string path;
    for (size_t i = 0; i < import_path.size(); ++i) {
        if (i > 0) path += "/";
        path += import_path[i];
    }
    return path;
}

Module TypeChecker::load_module_file(const std::string& path) {
    // Read file
    std::ifstream file(path);
    if (!file) {
        throw CompileError("Cannot open file: " + path, SourceLocation());
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Lex and parse
    Lexer lexer(source, path);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse_module(path, path);
}

static bool ann_is(const Annotation& ann, const std::string& name) {
    return ann.name == name;
}

void TypeChecker::warn_annotation(const Annotation& ann, const std::string& msg) {
    std::cerr << "Warning: " << msg;
    if (!ann.location.filename.empty()) {
        std::cerr << " at " << ann.location.filename << ":" << ann.location.line << ":" << ann.location.column;
    }
    std::cerr << " [[" << ann.name << "]]" << std::endl;
}

void TypeChecker::validate_annotations(const Module& mod) {
    for (const auto& stmt : mod.top_level) {
        validate_stmt_annotations(stmt);
    }
}

void TypeChecker::validate_stmt_annotations(StmtPtr stmt) {
    if (!stmt) return;

    auto warn_if = [&](const Annotation& ann, bool condition, const std::string& msg) {
        if (condition) warn_annotation(ann, msg);
    };

    for (const auto& ann : stmt->annotations) {
        bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                          ann_is(ann, "nonbanked") || ann_is(ann, "inline") || ann_is(ann, "noinline");
        if (!recognized) continue;
        switch (stmt->kind) {
            case Stmt::Kind::FuncDecl:
                // All recognized annotations allowed on functions
                break;
            case Stmt::Kind::VarDecl:
                warn_if(ann, ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                 ann_is(ann, "inline") || ann_is(ann, "noinline"),
                        "[[" + ann.name + "]] is only meaningful on functions");
                break;
            default:
                warn_if(ann, true, "[[" + ann.name + "]] is only supported on functions or globals");
                break;
        }
    }

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl: {
            for (const auto& param : stmt->params) {
                for (const auto& ann : param.annotations) {
                    bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                      ann_is(ann, "nonbanked") || ann_is(ann, "inline") || ann_is(ann, "noinline");
                    if (recognized) {
                        warn_annotation(ann, "[[" + ann.name + "]] is not used on parameters");
                    }
                }
            }
            if (stmt->body) {
                validate_expr_annotations(stmt->body);
            }
            break;
        }
        case Stmt::Kind::VarDecl:
            validate_expr_annotations(stmt->var_init);
            break;
        case Stmt::Kind::TypeDecl:
            for (const auto& field : stmt->fields) {
                for (const auto& ann : field.annotations) {
                    bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                      ann_is(ann, "nonbanked") || ann_is(ann, "inline") || ann_is(ann, "noinline");
                    if (recognized) {
                        warn_annotation(ann, "[[" + ann.name + "]] is not used on struct fields");
                    }
                }
            }
            break;
        case Stmt::Kind::Import:
            break;
        case Stmt::Kind::Expr:
            validate_expr_annotations(stmt->expr);
            break;
        case Stmt::Kind::Return:
            validate_expr_annotations(stmt->return_expr);
            break;
        case Stmt::Kind::ConditionalStmt:
            validate_expr_annotations(stmt->condition);
            validate_stmt_annotations(stmt->true_stmt);
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}

void TypeChecker::validate_expr_annotations(ExprPtr expr) {
    if (!expr) return;

    auto warn_all = [&](const Annotation& ann) {
        if (ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
            ann_is(ann, "nonbanked") || ann_is(ann, "inline") || ann_is(ann, "noinline")) {
            warn_annotation(ann, "[[" + ann.name + "]] is not used on expressions");
        }
    };
    for (const auto& ann : expr->annotations) {
        warn_all(ann);
    }

    validate_expr_annotations(expr->left);
    validate_expr_annotations(expr->right);
    validate_expr_annotations(expr->operand);
    validate_expr_annotations(expr->condition);
    validate_expr_annotations(expr->true_expr);
    validate_expr_annotations(expr->false_expr);
    for (const auto& arg : expr->args) validate_expr_annotations(arg);
    for (const auto& elem : expr->elements) validate_expr_annotations(elem);
    for (const auto& st : expr->statements) validate_stmt_annotations(st);
    validate_expr_annotations(expr->result_expr);
}

StmtPtr TypeChecker::clone_stmt_deep(StmtPtr stmt) {
    if (!stmt) return nullptr;

    // Use existing clone_stmt for expressions, add full statement cloning
    auto cloned = std::make_shared<Stmt>();
    cloned->kind = stmt->kind;
    cloned->location = stmt->location;
    cloned->annotations = stmt->annotations;

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            cloned->func_name = stmt->func_name;
            cloned->params = stmt->params;
            cloned->ref_params = stmt->ref_params;
            cloned->ref_param_types = stmt->ref_param_types;
            cloned->return_type = stmt->return_type;
            cloned->body = clone_expr(stmt->body);
            cloned->is_external = stmt->is_external;
            cloned->is_exported = stmt->is_exported;
            cloned->type_namespace = stmt->type_namespace;
            cloned->return_types = stmt->return_types;
            cloned->is_generic = stmt->is_generic;
            break;
        case Stmt::Kind::TypeDecl:
            cloned->type_decl_name = stmt->type_decl_name;
            cloned->fields = stmt->fields;
            break;
        case Stmt::Kind::VarDecl:
            cloned->var_name = stmt->var_name;
            cloned->var_type = stmt->var_type;
            cloned->var_init = clone_expr(stmt->var_init);
            cloned->is_mutable = stmt->is_mutable;
            break;
        case Stmt::Kind::Import:
            cloned->import_path = stmt->import_path;
            break;
        case Stmt::Kind::Expr:
            cloned->expr = clone_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            cloned->return_expr = clone_expr(stmt->return_expr);
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
        case Stmt::Kind::ConditionalStmt:
            cloned->condition = clone_expr(stmt->condition);
            cloned->true_stmt = clone_stmt_deep(stmt->true_stmt);
            break;
    }

    return cloned;
}

std::vector<StmtPtr> TypeChecker::clone_module_declarations(const std::vector<StmtPtr>& decls) {
    std::vector<StmtPtr> cloned;
    for (const auto& stmt : decls) {
        // Don't clone Import statements
        if (stmt->kind != Stmt::Kind::Import) {
            cloned.push_back(clone_stmt_deep(stmt));
        }
    }
    return cloned;
}

// TypeSignature helper implementations
bool TypeSignature::types_equal_static(TypePtr a, TypePtr b) {
    if (!a && !b) return true;
    if (!a || !b) return false;

    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case Type::Kind::Primitive:
            return a->primitive == b->primitive;
        case Type::Kind::Array:
            return types_equal_static(a->element_type, b->element_type);
        case Type::Kind::Named:
            return a->type_name == b->type_name;
        case Type::Kind::TypeVar:
            return a->var_name == b->var_name;
    }
    return false;
}

size_t TypeSignatureHash::type_hash(TypePtr t) {
    if (!t) return 0;

    size_t hash = static_cast<size_t>(t->kind);

    switch (t->kind) {
        case Type::Kind::Primitive:
            hash ^= static_cast<size_t>(t->primitive) << 8;
            break;
        case Type::Kind::Array:
            hash ^= type_hash(t->element_type) << 4;
            break;
        case Type::Kind::Named:
            hash ^= std::hash<std::string>{}(t->type_name);
            break;
        case Type::Kind::TypeVar:
            hash ^= std::hash<std::string>{}(t->var_name);
            break;
    }

    return hash;
}

// Generic monomorphization implementation
std::string TypeChecker::get_or_create_instantiation(const std::string& func_name,
                                                     const std::vector<TypePtr>& arg_types,
                                                     StmtPtr generic_func) {
    // Create type signature
    TypeSignature sig;
    sig.param_types = arg_types;

    // For imported functions with scope_instance_id, make lookups scope-specific
    // by including scope_instance_id in the function name key
    int scope_id = generic_func->scope_instance_id;
    std::string lookup_key = func_name;
    if (scope_id >= 0) {
        lookup_key += "_scope" + std::to_string(scope_id);
    }

    // Check if instantiation already exists
    auto func_it = instantiations.find(lookup_key);
    if (func_it != instantiations.end()) {
        auto inst_it = func_it->second.find(sig);
        if (inst_it != func_it->second.end()) {
            return inst_it->second.mangled_name;
        }
    }

    // Create new instantiation
    StmtPtr cloned = clone_function(generic_func);
    substitute_types(cloned, arg_types);

    // Generate mangled name
    std::string mangled = mangle_generic_name(func_name, arg_types);
    cloned->func_name = mangled;

    // Inherit scope_instance_id from the original function
    cloned->scope_instance_id = generic_func->scope_instance_id;

    // Mark as non-generic since types have been substituted
    cloned->is_generic = false;

    // Type check the instantiation immediately to infer return type
    check_func_decl(cloned);

    // Store instantiation
    GenericInstantiation inst;
    inst.mangled_name = mangled;
    inst.declaration = cloned;

    instantiations[lookup_key][sig] = inst;
    pending_instantiations.push_back(cloned);

    return mangled;
}

std::string TypeChecker::mangle_generic_name(const std::string& base_name,
                                              const std::vector<TypePtr>& types) {
    std::string result = base_name + "_G";

    for (const auto& type : types) {
        if (type->kind == Type::Kind::Primitive) {
            result += "_" + primitive_name(type->primitive);
        } else if (type->kind == Type::Kind::Named) {
            result += "_" + type->type_name;
        } else if (type->kind == Type::Kind::Array) {
            result += "_array";
        }
    }

    return result;
}

StmtPtr TypeChecker::clone_function(StmtPtr func) {
    auto cloned = std::make_shared<Stmt>();
    cloned->kind = func->kind;
    cloned->location = func->location;
    cloned->annotations = func->annotations;
    cloned->func_name = func->func_name;
    cloned->is_external = func->is_external;
    cloned->is_exported = func->is_exported;
    cloned->is_generic = func->is_generic;
    cloned->type_namespace = func->type_namespace;

    // Clone parameters
    for (const auto& param : func->params) {
        cloned->params.push_back(Parameter(param.name, param.type, param.is_expression_param, param.location, param.annotations));
    }
    cloned->ref_params = func->ref_params;
    cloned->ref_param_types = func->ref_param_types;

    // Clone return type
    cloned->return_type = func->return_type; // Will be substituted later

    // Clone body
    cloned->body = func->body ? clone_expr(func->body) : nullptr;

    return cloned;
}

ExprPtr TypeChecker::clone_expr(ExprPtr expr) {
    if (!expr) return nullptr;

    auto cloned = std::make_shared<Expr>();
    cloned->kind = expr->kind;
    cloned->location = expr->location;
    cloned->annotations = expr->annotations;
    // Don't copy type - let type-checker infer it fresh for the instantiation
    cloned->type = nullptr;

    // Clone literals
    cloned->uint_val = expr->uint_val;
    cloned->float_val = expr->float_val;
    cloned->string_val = expr->string_val;
    cloned->resource_path = expr->resource_path;

    // Clone identifier info
    cloned->name = expr->name;
    cloned->is_expr_param_ref = expr->is_expr_param_ref;
    cloned->creates_new_variable = expr->creates_new_variable;
    cloned->is_mutable_binding = expr->is_mutable_binding;

    // Clone operator
    cloned->op = expr->op;

    // Clone sub-expressions
    cloned->left = clone_expr(expr->left);
    cloned->right = clone_expr(expr->right);
    cloned->operand = clone_expr(expr->operand);
    cloned->condition = clone_expr(expr->condition);
    cloned->true_expr = clone_expr(expr->true_expr);
    cloned->false_expr = clone_expr(expr->false_expr);
    cloned->result_expr = clone_expr(expr->result_expr);
    cloned->target_type = expr->target_type;

    // Clone arguments
    for (const auto& arg : expr->args) {
        cloned->args.push_back(clone_expr(arg));
    }

    // Clone elements
    for (const auto& elem : expr->elements) {
        cloned->elements.push_back(clone_expr(elem));
    }

    // Clone receivers
    for (const auto& rec : expr->receivers) {
        cloned->receivers.push_back(clone_expr(rec));
    }

    // Clone statements (for blocks) - deep cloning
    for (const auto& stmt : expr->statements) {
        cloned->statements.push_back(clone_stmt(stmt));
    }

    return cloned;
}

StmtPtr TypeChecker::clone_stmt(StmtPtr stmt) {
    if (!stmt) return nullptr;

    auto cloned = std::make_shared<Stmt>();
    cloned->kind = stmt->kind;
    cloned->location = stmt->location;
    cloned->annotations = stmt->annotations;

    // Clone based on statement type
    switch (stmt->kind) {
        case Stmt::Kind::Expr:
        case Stmt::Kind::Return:
            cloned->expr = clone_expr(stmt->expr);
            cloned->return_expr = clone_expr(stmt->return_expr);
            break;

        case Stmt::Kind::VarDecl:
            cloned->var_name = stmt->var_name;
            cloned->var_type = stmt->var_type;
            cloned->var_init = clone_expr(stmt->var_init);
            cloned->is_mutable = stmt->is_mutable;
            break;

        case Stmt::Kind::ConditionalStmt:
            cloned->condition = clone_expr(stmt->condition);
            cloned->true_stmt = clone_stmt(stmt->true_stmt);
            break;

        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            // No additional data to clone
            break;

        default:
            // Other statement types shouldn't appear in function bodies
            break;
    }

    return cloned;
}

void TypeChecker::substitute_types(StmtPtr func, const std::vector<TypePtr>& concrete_types) {
    // Build type substitution map
    std::unordered_map<std::string, TypePtr> type_map;

    for (size_t i = 0; i < func->params.size() && i < concrete_types.size(); i++) {
        func->params[i].type = concrete_types[i];
        // If there was a type variable, map it
        // This is simplified - full implementation would track type variable names
    }

    // Substitute types in body
    if (func->body) {
        substitute_types_in_expr(func->body, type_map);
    }
}

void TypeChecker::substitute_types_in_expr(ExprPtr expr, const std::unordered_map<std::string, TypePtr>& type_map) {
    if (!expr) return;

    // Recursively substitute in sub-expressions
    substitute_types_in_expr(expr->left, type_map);
    substitute_types_in_expr(expr->right, type_map);
    substitute_types_in_expr(expr->operand, type_map);
    substitute_types_in_expr(expr->condition, type_map);
    substitute_types_in_expr(expr->true_expr, type_map);
    substitute_types_in_expr(expr->false_expr, type_map);
    substitute_types_in_expr(expr->result_expr, type_map);

    for (auto& arg : expr->args) {
        substitute_types_in_expr(arg, type_map);
    }

    for (auto& elem : expr->elements) {
        substitute_types_in_expr(elem, type_map);
    }
}

void TypeChecker::rename_identifiers(StmtPtr stmt, const std::unordered_map<std::string, std::string>& name_map) {
    if (!stmt) return;

    if (stmt->kind == Stmt::Kind::FuncDecl) {
        // Don't rename function body variables - only references to module-level items
        if (stmt->body) {
            rename_identifiers_in_expr(stmt->body, name_map);
        }
    } else if (stmt->kind == Stmt::Kind::VarDecl) {
        if (stmt->var_init) {
            rename_identifiers_in_expr(stmt->var_init, name_map);
        }
    } else if (stmt->kind == Stmt::Kind::Expr) {
        rename_identifiers_in_expr(stmt->expr, name_map);
    } else if (stmt->kind == Stmt::Kind::Return) {
        rename_identifiers_in_expr(stmt->return_expr, name_map);
    } else if (stmt->kind == Stmt::Kind::ConditionalStmt) {
        rename_identifiers_in_expr(stmt->condition, name_map);
        rename_identifiers(stmt->true_stmt, name_map);
    }
}

void TypeChecker::rename_identifiers_in_expr(ExprPtr expr, const std::unordered_map<std::string, std::string>& name_map) {
    if (!expr) return;

    // Rename identifier if it's in the map
    if (expr->kind == Expr::Kind::Identifier) {
        auto it = name_map.find(expr->name);
        if (it != name_map.end()) {
            expr->name = it->second;
        }
    }

    // Recursively rename in sub-expressions (only fields that exist in Expr)
    rename_identifiers_in_expr(expr->left, name_map);
    rename_identifiers_in_expr(expr->right, name_map);
    rename_identifiers_in_expr(expr->operand, name_map);
    rename_identifiers_in_expr(expr->condition, name_map);
    rename_identifiers_in_expr(expr->true_expr, name_map);
    rename_identifiers_in_expr(expr->false_expr, name_map);
    rename_identifiers_in_expr(expr->result_expr, name_map);

    for (auto& arg : expr->args) {
        rename_identifiers_in_expr(arg, name_map);
    }

    for (auto& elem : expr->elements) {
        rename_identifiers_in_expr(elem, name_map);
    }

    for (auto& s : expr->statements) {
        rename_identifiers(s, name_map);
    }
}

void TypeChecker::tag_scope_instances(StmtPtr stmt, int instance_id) {
    if (!stmt) return;

    if (stmt->kind == Stmt::Kind::FuncDecl || stmt->kind == Stmt::Kind::VarDecl) {
        // Collect module-level symbols
        std::unordered_set<std::string> module_symbols;
        for (const auto& pair : current_scope->symbols) {
            if (pair.second.declaration && pair.second.declaration->scope_instance_id == instance_id) {
                module_symbols.insert(pair.first);
            }
        }

        if (stmt->kind == Stmt::Kind::FuncDecl && stmt->body) {
            tag_scope_instances_in_expr(stmt->body, instance_id, module_symbols);
        } else if (stmt->kind == Stmt::Kind::VarDecl && stmt->var_init) {
            tag_scope_instances_in_expr(stmt->var_init, instance_id, module_symbols);
        }
    }
}

void TypeChecker::tag_scope_instances_in_expr(ExprPtr expr, int instance_id, const std::unordered_set<std::string>& module_symbols) {
    if (!expr) return;

    // Tag identifier if it's a module symbol
    if (expr->kind == Expr::Kind::Identifier && module_symbols.count(expr->name)) {
        expr->scope_instance_id = instance_id;
    }

    // Recursively tag sub-expressions
    tag_scope_instances_in_expr(expr->left, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->right, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->operand, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->condition, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->true_expr, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->false_expr, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->result_expr, instance_id, module_symbols);

    for (auto& arg : expr->args) {
        tag_scope_instances_in_expr(arg, instance_id, module_symbols);
    }

    for (auto& elem : expr->elements) {
        tag_scope_instances_in_expr(elem, instance_id, module_symbols);
    }

    for (auto& s : expr->statements) {
        tag_scope_instances(s, instance_id);
    }
}

void TypeChecker::validate_type(TypePtr type, const SourceLocation& loc) {
    if (!type) return;

    switch (type->kind) {
        case Type::Kind::Array: {
            // Recursively validate element type
            validate_type(type->element_type, loc);
            if (type->array_size) {
                CompileTimeEvaluator evaluator(this);
                CTValue size_val;
                if (!evaluator.try_evaluate(type->array_size, size_val)) {
                    throw CompileError("Array size must be a compile-time constant", loc);
                }
            }
            break;
        }
        case Type::Kind::Named: {
            // Check for recursive types
            Symbol* type_sym = current_scope->lookup(type->type_name);
            if (type_sym && type_sym->kind == Symbol::Kind::Type && type_sym->declaration) {
                check_recursive_type(type->type_name, type_sym->declaration, loc);
            }
            break;
        }
        default:
            break;
    }
}

void TypeChecker::check_recursive_type(const std::string& type_name, StmtPtr type_decl, const SourceLocation& loc) {
    // Check if any field has the same type as the parent (direct recursion)
    for (const auto& field : type_decl->fields) {
        if (field.type && field.type->kind == Type::Kind::Named &&
            field.type->type_name == type_name) {
            throw CompileError("Recursive types are not allowed (type " + type_name +
                             " contains field of its own type)", loc);
        }
    }
}

bool TypeChecker::is_primitive_type(TypePtr type) {
    if (!type) return false;
    return type->kind == Type::Kind::Primitive;
}

void TypeChecker::require_boolean(TypePtr type, const SourceLocation& loc, const std::string& context) {
    if (!type || type->kind != Type::Kind::Primitive || type->primitive != PrimitiveType::Bool) {
        throw CompileError(context + " requires a boolean expression", loc);
    }
}

void TypeChecker::require_unsigned_integer(TypePtr type, const SourceLocation& loc, const std::string& context) {
    if (!type || type->kind != Type::Kind::Primitive || !is_unsigned_int(type->primitive)) {
        throw CompileError(context + " requires unsigned integer operands", loc);
    }
}

}
