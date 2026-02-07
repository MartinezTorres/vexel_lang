#include "typechecker.h"
#include "evaluator.h"
#include "expr_access.h"
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
#include "path_utils.h"

namespace vexel {
namespace {

void assign_loop_symbol_stmt(StmtPtr stmt, TypePtr type, Bindings* bindings, int instance_id);

void assign_loop_symbol_expr(ExprPtr expr, TypePtr type, Bindings* bindings, int instance_id) {
    if (!expr) return;
    if (expr->kind == Expr::Kind::Identifier && expr->name == "_" && bindings) {
        if (Symbol* sym = bindings->lookup(instance_id, expr.get())) {
            sym->type = type;
        }
    }
    if (expr->kind == Expr::Kind::Iteration) {
        return; // inner loop has its own '_'
    }
    switch (expr->kind) {
        case Expr::Kind::Identifier:
            break;
        case Expr::Kind::Binary:
            assign_loop_symbol_expr(expr->left, type, bindings, instance_id);
            assign_loop_symbol_expr(expr->right, type, bindings, instance_id);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
            assign_loop_symbol_expr(expr->operand, type, bindings, instance_id);
            break;
        case Expr::Kind::Call:
            assign_loop_symbol_expr(expr->operand, type, bindings, instance_id);
            for (const auto& rec : expr->receivers) assign_loop_symbol_expr(rec, type, bindings, instance_id);
            for (const auto& arg : expr->args) assign_loop_symbol_expr(arg, type, bindings, instance_id);
            break;
        case Expr::Kind::Index:
            assign_loop_symbol_expr(expr->operand, type, bindings, instance_id);
            if (!expr->args.empty()) assign_loop_symbol_expr(expr->args[0], type, bindings, instance_id);
            break;
        case Expr::Kind::Member:
            assign_loop_symbol_expr(expr->operand, type, bindings, instance_id);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) assign_loop_symbol_expr(elem, type, bindings, instance_id);
            break;
        case Expr::Kind::Block:
            for (const auto& st : expr->statements) assign_loop_symbol_stmt(st, type, bindings, instance_id);
            assign_loop_symbol_expr(expr->result_expr, type, bindings, instance_id);
            break;
        case Expr::Kind::Conditional:
            assign_loop_symbol_expr(expr->condition, type, bindings, instance_id);
            assign_loop_symbol_expr(expr->true_expr, type, bindings, instance_id);
            assign_loop_symbol_expr(expr->false_expr, type, bindings, instance_id);
            break;
        case Expr::Kind::Assignment:
            assign_loop_symbol_expr(expr->left, type, bindings, instance_id);
            assign_loop_symbol_expr(expr->right, type, bindings, instance_id);
            break;
        case Expr::Kind::Range:
            assign_loop_symbol_expr(expr->left, type, bindings, instance_id);
            assign_loop_symbol_expr(expr->right, type, bindings, instance_id);
            break;
        case Expr::Kind::Repeat:
            assign_loop_symbol_expr(loop_subject(expr), type, bindings, instance_id);
            assign_loop_symbol_expr(loop_body(expr), type, bindings, instance_id);
            break;
        case Expr::Kind::Iteration:
        case Expr::Kind::Resource:
        case Expr::Kind::Process:
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
            break;
    }
}

void assign_loop_symbol_stmt(StmtPtr stmt, TypePtr type, Bindings* bindings, int instance_id) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::VarDecl:
            assign_loop_symbol_expr(stmt->var_init, type, bindings, instance_id);
            break;
        case Stmt::Kind::Expr:
            assign_loop_symbol_expr(stmt->expr, type, bindings, instance_id);
            break;
        case Stmt::Kind::Return:
            assign_loop_symbol_expr(stmt->return_expr, type, bindings, instance_id);
            break;
        case Stmt::Kind::ConditionalStmt:
            assign_loop_symbol_expr(stmt->condition, type, bindings, instance_id);
            assign_loop_symbol_stmt(stmt->true_stmt, type, bindings, instance_id);
            break;
        default:
            break;
    }
}

} // namespace

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
            Symbol* sym = lookup_binding(expr.get());
            if (!sym) {
                sym = lookup_global(expr->name);
                if (sym && bindings) {
                    bindings->bind(current_instance_id, expr.get(), sym);
                }
            }
            if (!sym) {
                throw CompileError("Internal error: unresolved identifier: " + expr->name, expr->location);
            }
            if (!sym->type && sym->declaration && sym->declaration->kind == Stmt::Kind::VarDecl) {
                if (sym->instance_id != current_instance_id) {
                    auto scope = scoped_instance(sym->instance_id);
                    (void)scope;
                    check_stmt(sym->declaration);
                } else {
                    check_stmt(sym->declaration);
                }
            }
            if (expr->type) {
                // Type annotation provided
                return expr->type;
            }
            expr->type = sym->type;
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

    return nullptr;
}

TypePtr TypeChecker::try_operator_overload(ExprPtr expr, const std::string& op, TypePtr left_type) {
    if (!left_type || left_type->kind != Type::Kind::Named) {
        return nullptr;
    }

    std::string func_name = left_type->type_name + "::" + op;
    Symbol* sym = lookup_global(func_name);
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
    if (bindings) {
        bindings->bind(current_instance_id, expr->operand.get(), sym);
    }
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

    Symbol* sym = lookup_global(method_name);
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

    TypePtr loop_type = make_fresh_typevar();
    assign_loop_symbol_expr(expr->right, loop_type, bindings, current_instance_id);
    loop_depth++;
    check_expr(expr->right);
    loop_depth--;

    ExprPtr receiver_expr = expr->operand;
    ExprPtr body_expr = expr->right;

    expr->kind = Expr::Kind::Call;
    expr->operand = Expr::make_identifier(method_token, expr->location);
    if (bindings) {
        bindings->bind(current_instance_id, expr->operand.get(), sym);
    }
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

        sym = lookup_binding(expr->operand.get());
        if (!sym || expr->operand->name != func_name) {
            sym = lookup_global(func_name);
        }
        if (!sym) {
            throw CompileError("Undefined function: " + func_name, expr->location);
        }
        if (bindings) {
            bindings->bind(current_instance_id, expr->operand.get(), sym);
        }
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
        if (bindings) {
            bindings->bind(current_instance_id, expr->type.get(), sym);
        }
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
                    if (param_type && param_type->kind == Type::Kind::TypeVar && recv_type) {
                        bind_typevar(param_type, recv_type);
                    }
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
            if (bindings) {
                Symbol* inst_sym = lookup_global(mangled_name);
                if (inst_sym) {
                    bindings->bind(current_instance_id, expr->operand.get(), inst_sym);
                }
            }

            TypeSignature sig;
            sig.param_types = arg_types;
            std::string lookup_key = func_name + "_inst" + std::to_string(current_instance_id);
            auto func_it = instantiations.find(lookup_key);
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
                    if (arg_expr->type) {
                        bind_typevar(param_type, arg_expr->type);
                    }
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

        // No declared return type: treat as void (no value)
        expr->type = nullptr;
        return expr->type;
    }

    throw CompileError("Cannot call non-function: " + func_name, expr->location);
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

        Symbol* type_sym = nullptr;
        if (bindings) {
            type_sym = bindings->lookup(current_instance_id, obj_type.get());
        }
        if (!type_sym) {
            type_sym = lookup_global(obj_type->type_name);
        }
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
    for (auto& stmt : expr->statements) {
        check_stmt(stmt);
    }
    TypePtr result_type = nullptr;
    if (expr->result_expr) {
        result_type = check_expr(expr->result_expr);
    }
    expr->type = result_type;
    return result_type;
}

TypePtr TypeChecker::check_conditional(ExprPtr expr) {
    TypePtr cond_type = check_expr(expr->condition);
    require_boolean(cond_type, expr->condition ? expr->condition->location : expr->location,
                    "Conditional expression");

    // Invariant: compile-time known condition short-circuits type requirements
    // for the dead branch. The type-use validator mirrors this by skipping
    // constexpr-dead branches.
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
                Symbol* sym = nullptr;
                if (bindings) {
                    sym = bindings->lookup(current_instance_id, node.get());
                }
                if (!sym) {
                    sym = lookup_global(node->name);
                }
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

std::optional<bool> TypeChecker::constexpr_condition(ExprPtr expr) {
    return evaluate_static_condition(expr);
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
    bool creates_new_variable = bindings && bindings->is_new_variable(current_instance_id, expr.get());
    if (creates_new_variable) {
        if (expr->left->kind != Expr::Kind::Identifier) {
            throw CompileError("Internal error: invalid declaration assignment", expr->location);
        }

        // Check if RHS is a function reference (not allowed)
        if (expr->right->kind == Expr::Kind::Identifier) {
            Symbol* rhs_sym = lookup_binding(expr->right.get());
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

        Symbol* lhs_sym = lookup_binding(expr->left.get());
        if (!lhs_sym) {
            throw CompileError("Internal error: unresolved declaration target", expr->location);
        }
        lhs_sym->kind = Symbol::Kind::Variable;
        lhs_sym->type = var_type;
        lhs_sym->is_mutable = true;

        // Invariant: declaration-site LHS is not a typed value expression.
        expr->left->type = nullptr;
        expr->creates_new_variable = true;

        expr->type = var_type;
        return var_type;
    }

    if (expr->left->kind == Expr::Kind::Identifier && expr->left->type) {
        expr->left->type = nullptr;
    }

    if (expr->left->kind == Expr::Kind::Identifier) {
        Symbol* sym = lookup_binding(expr->left.get());
        if (!sym) {
            sym = lookup_global(expr->left->name);
            if (sym && bindings) {
                bindings->bind(current_instance_id, expr->left.get(), sym);
            }
        }
        if (!sym) {
            throw CompileError("Internal error: unresolved assignment target", expr->location);
        }
        if (!sym->is_mutable) {
            std::string name = expr->left->name;
            if (name == "_") {
                throw CompileError("Cannot assign to read-only loop variable '_'", expr->location);
            }
            throw CompileError("Cannot assign to immutable constant: " + name, expr->location);
        }
    }

    if (expr->right->kind == Expr::Kind::Identifier) {
        Symbol* rhs_sym = lookup_binding(expr->right.get());
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

    TypePtr loop_type = iterable_type && iterable_type->element_type
        ? iterable_type->element_type
        : make_fresh_typevar();
    assign_loop_symbol_expr(expr->right, loop_type, bindings, current_instance_id);
    loop_depth++;
    check_expr(expr->right);
    loop_depth--;

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
    bool found = try_resolve_resource_path(expr->resource_path, expr->location.filename, project_root, resolved);
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

TypePtr TypeChecker::resolve_type(TypePtr type) {
    if (!type) return nullptr;
    if (type->kind == Type::Kind::TypeVar) {
        auto it = type_var_bindings.find(type->var_name);
        if (it != type_var_bindings.end()) {
            return resolve_type(it->second);
        }
    }
    if (type->kind == Type::Kind::Array && type->element_type) {
        TypePtr elem = resolve_type(type->element_type);
        if (elem != type->element_type) {
            TypePtr cloned = std::make_shared<Type>(*type);
            cloned->element_type = elem;
            return cloned;
        }
    }
    return type;
}

TypePtr TypeChecker::bind_typevar(TypePtr var, TypePtr target) {
    if (!var || var->kind != Type::Kind::TypeVar) return target;
    if (!target) return target;
    type_var_bindings[var->var_name] = target;
    return target;
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

}
