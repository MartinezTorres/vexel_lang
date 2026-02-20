#include "typechecker.h"
#include "expr_access.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>

#include "constants.h"
#include "lexer.h"
#include "parser.h"
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

bool is_untyped_integer_primitive(const TypePtr& type) {
    return type &&
           type->kind == Type::Kind::Primitive &&
           (type->primitive == PrimitiveType::Int || type->primitive == PrimitiveType::UInt) &&
           type->integer_bits == 0;
}

void apply_context_type_recursive(const ExprPtr& expr, const TypePtr& target) {
    if (!expr || !target) return;
    if (expr->kind == Expr::Kind::Block) {
        apply_context_type_recursive(expr->result_expr, target);
        expr->type = target;
        return;
    }

    if ((expr->type && is_untyped_integer_primitive(expr->type)) ||
        expr->kind == Expr::Kind::IntLiteral) {
        expr->type = target;
    }

    switch (expr->kind) {
        case Expr::Kind::Binary:
            apply_context_type_recursive(expr->left, target);
            apply_context_type_recursive(expr->right, target);
            break;
        case Expr::Kind::Unary:
            apply_context_type_recursive(expr->operand, target);
            break;
        case Expr::Kind::Conditional:
            apply_context_type_recursive(expr->true_expr, target);
            apply_context_type_recursive(expr->false_expr, target);
            break;
        case Expr::Kind::Assignment:
            apply_context_type_recursive(expr->right, target);
            break;
        case Expr::Kind::Cast:
            apply_context_type_recursive(expr->operand, target);
            break;
        default:
            break;
    }
}

uint64_t min_unsigned_bits(uint64_t value) {
    if (value == 0) return 1;
    uint64_t bits = 0;
    while (value > 0) {
        value >>= 1;
        bits++;
    }
    return bits;
}

uint64_t min_signed_bits(int64_t value) {
    for (uint64_t bits = 1; bits <= 64; ++bits) {
        if (bits == 64) {
            return uint64_t(64);
        }
        const int64_t min_v = -(int64_t(1) << (bits - 1));
        const int64_t max_v = (int64_t(1) << (bits - 1)) - 1;
        if (value >= min_v && value <= max_v) {
            return bits;
        }
    }
    return uint64_t(64);
}

uint64_t normalize_inferred_int_bits(uint64_t bits) {
    if (bits <= 8) return 8;
    if (bits <= 16) return 16;
    if (bits <= 32) return 32;
    return 64;
}

} // namespace

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
    require_boolean_expr(expr->condition, cond_type,
                         expr->condition ? expr->condition->location : expr->location,
                         "Conditional expression");

    // Invariant: compile-time known condition short-circuits type requirements
    // for the dead branch. The type-use validator mirrors this by skipping
    // constexpr-dead branches.
    auto static_value = constexpr_condition(expr->condition);
    if (expr->condition && static_value.has_value()) {
        constexpr_condition_cache[expr_key(expr->condition.get())] = static_value.value();
    } else if (expr->condition) {
        constexpr_condition_cache.erase(expr_key(expr->condition.get()));
    }
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

    auto concretize_branch = [&](ExprPtr branch_expr, TypePtr& branch_type, TypePtr target_type) -> bool {
        if (!is_untyped_integer_primitive(branch_type)) return false;
        if (!target_type || target_type->kind != Type::Kind::Primitive) return false;
        if (target_type->primitive == PrimitiveType::Bool) return false;
        if (!literal_assignable_to(target_type, branch_expr)) return false;
        apply_context_type_recursive(branch_expr, target_type);
        branch_type = target_type;
        return true;
    };

    if (concretize_branch(expr->true_expr, true_type, false_type)) {
        expr->type = false_type;
        return expr->type;
    }
    if (concretize_branch(expr->false_expr, false_type, true_type)) {
        expr->type = true_type;
        return expr->type;
    }

    if (types_equal(true_type, false_type)) {
        expr->type = true_type;
        return expr->type;
    }

    TypePtr merged_type = unify_types(true_type, false_type);
    if (merged_type) {
        expr->type = merged_type;
        return expr->type;
    }

    std::string lhs = true_type ? true_type->to_string() : "<unknown>";
    std::string rhs = false_type ? false_type->to_string() : "<unknown>";
    throw CompileError("Conditional branches must have matching types at runtime (type mismatch: " + lhs + " vs " + rhs + ")",
                       expr->location);
}

std::optional<bool> TypeChecker::constexpr_condition(ExprPtr expr) {
    if (!expr) return std::nullopt;
    CTValue value;
    if (!try_evaluate_constexpr(expr, value)) {
        return std::nullopt;
    }
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value) != 0;
    }
    if (std::holds_alternative<uint64_t>(value)) {
        return std::get<uint64_t>(value) != 0;
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value) != 0.0;
    }
    return std::nullopt;
}

TypePtr TypeChecker::check_cast(ExprPtr expr) {
    TypePtr operand_type = check_expr(expr->operand);
    TypePtr target_type = validate_type(expr->target_type, expr->location);
    expr->target_type = target_type;
    if (operand_type &&
        operand_type->kind == Type::Kind::Primitive &&
        (operand_type->primitive == PrimitiveType::Int || operand_type->primitive == PrimitiveType::UInt) &&
        operand_type->integer_bits == 0 &&
        literal_assignable_to(target_type, expr->operand)) {
        apply_context_type_recursive(expr->operand, target_type);
        operand_type = target_type;
    }

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
        if (count != static_cast<uint64_t>(type_bits(target_type->primitive, target_type->integer_bits))) {
            throw CompileError("Boolean array size mismatch for cast to #" +
                               primitive_name(target_type->primitive, target_type->integer_bits), expr->location);
        }
    }

    expr->type = target_type;
    return expr->type;
}

TypePtr TypeChecker::check_assignment(ExprPtr expr) {
    expr->declared_var_type = nullptr;
    bool creates_new_variable = bindings && bindings->is_new_variable(current_instance_id, expr.get());
    if (creates_new_variable) {
        if (expr->left->kind != Expr::Kind::Identifier) {
            throw CompileError("Internal error: invalid declaration assignment", expr->location);
        }
        TypePtr explicit_decl_type = expr->left->type
            ? validate_type(expr->left->type, expr->left->location)
            : nullptr;
        expr->left->type = explicit_decl_type;
        if (type_strictness >= 1 && !explicit_decl_type) {
            throw CompileError(
                "Type strictness level 1 requires explicit type annotation for variable '" +
                    expr->left->name + "'",
                expr->location);
        }

        // Check if RHS is a function reference (not allowed)
        if (expr->right->kind == Expr::Kind::Identifier) {
            Symbol* rhs_sym = lookup_binding(expr->right.get());
            if (rhs_sym && rhs_sym->kind == Symbol::Kind::Function) {
                throw CompileError("Cannot assign function to variable (no function types): " + expr->right->name, expr->location);
            }
        }

        TypePtr rhs_type = check_expr(expr->right);
        TypePtr rhs_inferred_type = rhs_type;
        TypePtr var_type = expr->left->type ? expr->left->type : rhs_type;
        if (expr->left->type) {
            enforce_declared_initializer_type(var_type, expr->right, rhs_type, expr->location);
            rhs_inferred_type = rhs_type;
        }

        Symbol* lhs_sym = lookup_binding(expr->left.get());
        if (!lhs_sym) {
            throw CompileError("Internal error: unresolved declaration target", expr->location);
        }
        lhs_sym->kind = Symbol::Kind::Variable;
        lhs_sym->type = var_type;
        lhs_sym->is_mutable = true;

        // Invariant: declaration-site LHS is not a typed value expression.
        if (explicit_decl_type && !types_equal(rhs_inferred_type, explicit_decl_type)) {
            expr->declared_var_type = explicit_decl_type;
        }
        expr->left->type = nullptr;
        expr->creates_new_variable = true;

        CTValue value;
        if (try_evaluate_constexpr(expr->right, value)) {
            remember_constexpr_value(lhs_sym, value);
        } else {
            forget_constexpr_value(lhs_sym);
        }

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
        if (expr->left->name == "_") {
            throw CompileError("Cannot assign to read-only loop variable '_'", expr->location);
        }
        if (!sym->is_mutable) {
            bool infer_mutable_global =
                !sym->is_local &&
                (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant);
            bool infer_mutable_local =
                sym->is_local &&
                (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant);
            if (infer_mutable_global || infer_mutable_local) {
                sym->kind = Symbol::Kind::Variable;
                sym->is_mutable = true;
                if (sym->declaration && sym->declaration->kind == Stmt::Kind::VarDecl) {
                    sym->declaration->is_mutable = true;
                }
            }
        }
        if (!sym->is_mutable) {
            throw CompileError("Cannot assign to immutable constant: " + expr->left->name, expr->location);
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

    if (is_untyped_integer_primitive(rhs_type) &&
        literal_assignable_to(lhs_type, expr->right)) {
        apply_context_type_recursive(expr->right, lhs_type);
        rhs_type = lhs_type;
    }

    if (!types_compatible(rhs_type, lhs_type)) {
        if (literal_assignable_to(lhs_type, expr->right)) {
            apply_context_type_recursive(expr->right, lhs_type);
            expr->type = lhs_type;
            return lhs_type;
        }
        throw CompileError("Type mismatch in assignment", expr->location);
    }

    expr->creates_new_variable = false;
    expr->type = lhs_type;

    auto lookup_identifier_symbol = [&](ExprPtr node) -> Symbol* {
        if (!node || node->kind != Expr::Kind::Identifier) return nullptr;
        Symbol* sym = lookup_binding(node.get());
        if (!sym) {
            sym = lookup_global(node->name);
            if (sym && bindings) {
                bindings->bind(current_instance_id, node.get(), sym);
            }
        }
        return sym;
    };
    std::function<Symbol*(ExprPtr)> base_symbol = [&](ExprPtr node) -> Symbol* {
        if (!node) return nullptr;
        if (node->kind == Expr::Kind::Identifier) {
            return lookup_identifier_symbol(node);
        }
        if (node->kind == Expr::Kind::Member || node->kind == Expr::Kind::Index) {
            return base_symbol(node->operand);
        }
        return nullptr;
    };

    Symbol* assigned_sym = base_symbol(expr->left);
    if (assigned_sym) {
        if (expr->left->kind == Expr::Kind::Identifier) {
            CTValue value;
            if (try_evaluate_constexpr(expr->right, value)) {
                remember_constexpr_value(assigned_sym, value);
            } else {
                forget_constexpr_value(assigned_sym);
            }
        } else {
            forget_constexpr_value(assigned_sym);
        }
    }

    return lhs_type;
}

TypePtr TypeChecker::check_range(ExprPtr expr) {
    TypePtr start_type = check_expr(expr->left);
    TypePtr end_type = check_expr(expr->right);
    auto is_integer_primitive = [](TypePtr t) {
        return t &&
               t->kind == Type::Kind::Primitive &&
               (is_signed_int(t->primitive) || is_unsigned_int(t->primitive));
    };
    if (!is_integer_primitive(start_type) || !is_integer_primitive(end_type)) {
        throw CompileError("Range bounds must be integer expressions", expr->location);
    }

    auto fold_const = [&](ExprPtr e, int64_t& out) -> bool {
        if (e->kind == Expr::Kind::IntLiteral) {
            if (e->literal_is_unsigned &&
                e->uint_val > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return false;
            }
            out = static_cast<int64_t>(e->uint_val);
            return true;
        }
        CTValue value;
        if (!try_evaluate_constexpr(e, value)) {
            return false;
        }
        if (std::holds_alternative<int64_t>(value)) {
            out = std::get<int64_t>(value);
            return true;
        }
        if (std::holds_alternative<uint64_t>(value)) {
            uint64_t v = std::get<uint64_t>(value);
            if (v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return false;
            }
            out = static_cast<int64_t>(v);
            return true;
        }
        return false;
    };
    int64_t start_val = 0, end_val = 0;
    if (!fold_const(expr->left, start_val) || !fold_const(expr->right, end_val)) {
        throw CompileError("Range bounds must be compile-time constants", expr->location);
    }
    if (start_val == end_val) {
        throw CompileError("Range with equal bounds (a..a) would produce empty array", expr->location);
    }

    TypePtr elem_type = unify_types(start_type, end_type);
    if (!elem_type) {
        throw CompileError("Range bounds must have compatible numeric types", expr->location);
    }
    if (is_untyped_integer_primitive(elem_type)) {
        if (start_val < 0 || end_val < 0) {
            uint64_t bits = min_signed_bits(std::min(start_val, end_val));
            if (std::max(start_val, end_val) > 0) {
                bits = std::max(bits, min_signed_bits(std::max(start_val, end_val)));
            }
            elem_type = Type::make_primitive(PrimitiveType::Int,
                                             expr->location,
                                             normalize_inferred_int_bits(bits));
        } else {
            uint64_t max_v = static_cast<uint64_t>(std::max(start_val, end_val));
            elem_type = Type::make_primitive(
                PrimitiveType::UInt,
                expr->location,
                normalize_inferred_int_bits(min_unsigned_bits(max_v)));
        }
    }
    uint64_t count = (start_val < end_val)
        ? static_cast<uint64_t>(end_val - start_val)
        : static_cast<uint64_t>(start_val - end_val);
    ExprPtr size = Expr::make_int(count, expr->location);
    expr->type = Type::make_array(elem_type, size, expr->location);
    return expr->type;
}

TypePtr TypeChecker::check_length(ExprPtr expr) {
    check_expr(expr->operand);
    expr->type = Type::make_primitive(PrimitiveType::Int, expr->location, 32);
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
    auto saved_constexpr_values = known_constexpr_values;
    loop_depth++;
    check_expr(expr->right);
    loop_depth--;
    known_constexpr_values = std::move(saved_constexpr_values);

    expr->type = nullptr;
    return nullptr;
}

TypePtr TypeChecker::check_repeat(ExprPtr expr) {
    TypePtr cond_type = check_expr(expr->condition);
    require_boolean_expr(expr->condition, cond_type,
                         expr->condition ? expr->condition->location : expr->location,
                         "Repeat loop");
    auto saved_constexpr_values = known_constexpr_values;
    loop_depth++;
    check_expr(expr->right);
    loop_depth--;
    known_constexpr_values = std::move(saved_constexpr_values);
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

} // namespace vexel
