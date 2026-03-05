#include "typechecker.h"
#include <algorithm>
#include <functional>
#include <limits>
#include "constants.h"
#include "resolver.h"

namespace vexel {

namespace {

uint64_t effective_type_bits(const TypePtr& type) {
    if (!type || type->kind != Type::Kind::Primitive) return 0;
    if (is_signed_int(type->primitive) || is_unsigned_int(type->primitive)) {
        return type->integer_bits;
    }
    int64_t bits = type_bits(type->primitive, type->integer_bits, type->fractional_bits);
    return bits > 0 ? static_cast<uint64_t>(bits) : 0;
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

uint64_t normalize_inferred_int_bits(uint64_t bits) {
    if (bits <= 8) return 8;
    if (bits <= 16) return 16;
    if (bits <= 32) return 32;
    return 64;
}

TypePtr make_int_type(const SourceLocation& loc, uint64_t bits, bool is_unsigned) {
    return Type::make_primitive(is_unsigned ? PrimitiveType::UInt : PrimitiveType::Int, loc, bits);
}

bool is_untyped_integer_type(const TypePtr& type) {
    return type &&
           type->kind == Type::Kind::Primitive &&
           (is_signed_int(type->primitive) || is_unsigned_int(type->primitive)) &&
           type->integer_bits == 0;
}

bool is_numeric_primitive_type(const TypePtr& type) {
    return type &&
           type->kind == Type::Kind::Primitive &&
           (is_signed_int(type->primitive) || is_unsigned_int(type->primitive) ||
            is_signed_fixed(type->primitive) || is_unsigned_fixed(type->primitive) ||
            is_float(type->primitive));
}

bool is_fixed_primitive_type(const TypePtr& type) {
    return type &&
           type->kind == Type::Kind::Primitive &&
           (is_signed_fixed(type->primitive) || is_unsigned_fixed(type->primitive));
}

bool fixed_native_storage_width_supported(const TypePtr& type) {
    if (!is_fixed_primitive_type(type)) return false;
    int64_t bits = type_bits(type->primitive, type->integer_bits, type->fractional_bits);
    return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}

bool fixed_bitwise_shift_supported(const TypePtr& type) {
    if (!is_fixed_primitive_type(type)) return false;
    return type_bits(type->primitive, type->integer_bits, type->fractional_bits) > 0;
}

bool is_side_effect_free_for_array_lift(const ExprPtr& expr) {
    if (!expr) return true;
    switch (expr->kind) {
        case Expr::Kind::Identifier:
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
        case Expr::Kind::Resource:
            return true;
        case Expr::Kind::Binary:
            return is_side_effect_free_for_array_lift(expr->left) &&
                   is_side_effect_free_for_array_lift(expr->right);
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Member:
        case Expr::Kind::Length:
            return is_side_effect_free_for_array_lift(expr->operand);
        case Expr::Kind::Index:
            if (!is_side_effect_free_for_array_lift(expr->operand)) return false;
            for (const auto& arg : expr->args) {
                if (!is_side_effect_free_for_array_lift(arg)) return false;
            }
            return true;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) {
                if (!is_side_effect_free_for_array_lift(elem)) return false;
            }
            return true;
        case Expr::Kind::Conditional:
            return is_side_effect_free_for_array_lift(expr->condition) &&
                   is_side_effect_free_for_array_lift(expr->true_expr) &&
                   is_side_effect_free_for_array_lift(expr->false_expr);
        default:
            return false;
    }
}

bool exact_array_size_u64(const TypePtr& type, uint64_t& out) {
    if (!type || type->kind != Type::Kind::Array || !type->array_size) {
        return false;
    }
    ExprPtr size_expr = type->array_size;
    if (size_expr->kind != Expr::Kind::IntLiteral) {
        return false;
    }
    if (size_expr->has_exact_int_val) {
        if (!size_expr->exact_int_val.fits_u64()) {
            return false;
        }
        out = size_expr->exact_int_val.to_u64();
        return true;
    }
    out = size_expr->uint_val;
    return true;
}

bool collect_array_like_shape(const TypePtr& type, std::vector<uint64_t>& shape_out) {
    shape_out.clear();
    TypePtr current = type;
    while (current) {
        if (current->kind == Type::Kind::Array) {
            uint64_t size = 0;
            if (!exact_array_size_u64(current, size)) {
                return false;
            }
            shape_out.push_back(size);
            current = current->element_type;
            continue;
        }
        break;
    }
    return true;
}

bool is_array_like_type(const TypePtr& type) {
    return type && type->kind == Type::Kind::Array;
}

TypePtr array_like_scalar_type(const TypePtr& type) {
    TypePtr current = type;
    while (current) {
        if (current->kind == Type::Kind::Array) {
            current = current->element_type;
            continue;
        }
        return current;
    }
    return nullptr;
}

bool compute_broadcast_shape(const std::vector<std::vector<uint64_t>>& arg_shapes,
                             std::vector<uint64_t>& out_shape) {
    out_shape.clear();
    size_t max_rank = 0;
    for (const auto& s : arg_shapes) {
        max_rank = std::max(max_rank, s.size());
    }
    out_shape.resize(max_rank, 1);
    for (size_t dim = 0; dim < max_rank; ++dim) {
        uint64_t target = 1;
        for (const auto& shape : arg_shapes) {
            uint64_t size = 1;
            if (dim >= max_rank - shape.size()) {
                size_t local_idx = dim - (max_rank - shape.size());
                size = shape[local_idx];
            }
            if (size == 1) continue;
            if (target == 1) {
                target = size;
                continue;
            }
            if (target != size) {
                return false;
            }
        }
        out_shape[dim] = target;
    }
    return true;
}

ExprPtr apply_broadcast_indices(ExprPtr base,
                                const std::vector<uint64_t>& arg_shape,
                                const std::vector<uint64_t>& result_shape,
                                const std::vector<uint64_t>& result_indices,
                                const SourceLocation& loc) {
    if (!base || arg_shape.empty()) {
        return base;
    }
    ExprPtr current = base;
    const size_t rank = arg_shape.size();
    const size_t offset = result_shape.size() - rank;
    for (size_t d = 0; d < rank; ++d) {
        uint64_t idx = (arg_shape[d] == 1) ? 0 : result_indices[offset + d];
        ExprPtr idx_expr = Expr::make_int_exact(APInt(idx), true, loc);
        current = Expr::make_index(current, idx_expr, loc);
    }
    return current;
}

} // namespace

bool TypeChecker::is_bundled_std_math_symbol(const Symbol* sym) const {
    if (!sym || !program || sym->module_id < 0) return false;
    const ModuleInfo* mod = program->module(sym->module_id);
    if (!mod || mod->origin != ModuleOrigin::BundledStd) return false;
    const std::string& path = mod->path;
    return path == "std/math.vx" ||
           (path.size() >= 11 && path.compare(path.size() - 11, 11, "std/math.vx") == 0);
}

bool TypeChecker::try_rewrite_std_math_array_call(ExprPtr expr, Symbol* sym) {
    if (!expr || !sym || sym->kind != Symbol::Kind::Function || !sym->declaration) {
        return false;
    }
    if (!is_bundled_std_math_symbol(sym)) {
        return false;
    }
    if (!expr->receivers.empty()) {
        return false;
    }

    std::vector<TypePtr> arg_types;
    std::vector<std::vector<uint64_t>> arg_shapes;
    arg_types.reserve(expr->args.size());
    arg_shapes.reserve(expr->args.size());
    size_t array_arg_count = 0;
    for (const auto& arg : expr->args) {
        TypePtr t = arg ? resolve_type(arg->type) : nullptr;
        arg_types.push_back(t);
        std::vector<uint64_t> shape;
        if (is_array_like_type(t)) {
            if (!collect_array_like_shape(t, shape)) {
                throw CompileError("Bundled std::math array lifting requires concrete array sizes", expr->location);
            }
            array_arg_count++;
        }
        arg_shapes.push_back(std::move(shape));
    }
    if (array_arg_count == 0) {
        return false;
    }

    const size_t arity = expr->args.size();
    if (arity != 1 && arity != 2) {
        throw CompileError("Bundled std::math array lifting currently supports unary and binary functions only",
                           expr->location);
    }

    if (arity == 1 && !is_array_like_type(arg_types[0])) {
        return false;
    }

    for (size_t i = 0; i < expr->args.size(); ++i) {
        if (!is_side_effect_free_for_array_lift(expr->args[i])) {
            throw CompileError("Bundled std::math array lifting currently requires side-effect-free arguments",
                               expr->args[i] ? expr->args[i]->location : expr->location);
        }
    }

    std::vector<uint64_t> result_shape;
    if (!compute_broadcast_shape(arg_shapes, result_shape)) {
        throw CompileError("Bundled std::math array calls require broadcast-compatible shapes", expr->location);
    }

    std::function<ExprPtr(size_t, std::vector<uint64_t>&)> build;
    build = [&](size_t depth, std::vector<uint64_t>& result_indices) -> ExprPtr {
        if (depth == result_shape.size()) {
            ExprPtr callee = Expr::make_identifier(expr->operand ? expr->operand->name : "", expr->location);
            std::vector<ExprPtr> scalar_args;
            scalar_args.reserve(expr->args.size());
            for (size_t arg_i = 0; arg_i < expr->args.size(); ++arg_i) {
                scalar_args.push_back(apply_broadcast_indices(expr->args[arg_i],
                                                              arg_shapes[arg_i],
                                                              result_shape,
                                                              result_indices,
                                                              expr->location));
            }
            ExprPtr call = Expr::make_call(callee, scalar_args, expr->location);
            call->receivers.clear();
            return call;
        }

        uint64_t count = result_shape[depth];
        std::vector<ExprPtr> elements;
        elements.reserve(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i) {
            result_indices.push_back(i);
            elements.push_back(build(depth + 1, result_indices));
            result_indices.pop_back();
        }
        return Expr::make_array(elements, expr->location);
    };

    std::vector<uint64_t> result_indices;
    ExprPtr rewritten = build(0, result_indices);
    replace_expr_in_place(expr, rewritten);
    return true;
}

bool TypeChecker::try_rewrite_dotted_array_binary(ExprPtr expr, TypePtr left_type, TypePtr right_type) {
    if (!expr || expr->kind != Expr::Kind::Binary || expr->op.size() < 2 || expr->op[0] != '.') {
        return false;
    }
    if (!expr->left || !expr->right) {
        return false;
    }

    TypePtr lhs = resolve_type(left_type);
    TypePtr rhs = resolve_type(right_type);
    const bool left_array = is_array_like_type(lhs);
    const bool right_array = is_array_like_type(rhs);
    if (!left_array && !right_array) {
        return false;
    }

    if (!is_side_effect_free_for_array_lift(expr->left) || !is_side_effect_free_for_array_lift(expr->right)) {
        throw CompileError("Per-element array operators currently require side-effect-free operands", expr->location);
    }

    std::vector<std::vector<uint64_t>> arg_shapes(2);
    if (left_array && !collect_array_like_shape(lhs, arg_shapes[0])) {
        throw CompileError("Per-element array operators require concrete array sizes", expr->location);
    }
    if (right_array && !collect_array_like_shape(rhs, arg_shapes[1])) {
        throw CompileError("Per-element array operators require concrete array sizes", expr->location);
    }

    std::vector<uint64_t> result_shape;
    if (!compute_broadcast_shape(arg_shapes, result_shape)) {
        throw CompileError("Per-element array operators require broadcast-compatible shapes", expr->location);
    }

    auto peel_array_element_type = [&](TypePtr t) -> TypePtr {
        TypePtr cur = resolve_type(t);
        while (cur && cur->kind == Type::Kind::Array) {
            cur = resolve_type(cur->element_type);
        }
        return cur;
    };

    TypePtr scalar_left_type = peel_array_element_type(lhs ? lhs : left_type);
    const bool preserve_dotted_leaf_op = scalar_left_type && scalar_left_type->kind == Type::Kind::Named;
    const std::string leaf_op = preserve_dotted_leaf_op ? expr->op : expr->op.substr(1);

    std::function<ExprPtr(size_t, std::vector<uint64_t>&)> build;
    build = [&](size_t depth, std::vector<uint64_t>& result_indices) -> ExprPtr {
        if (depth == result_shape.size()) {
            ExprPtr l_scalar = apply_broadcast_indices(expr->left, arg_shapes[0], result_shape, result_indices, expr->location);
            ExprPtr r_scalar = apply_broadcast_indices(expr->right, arg_shapes[1], result_shape, result_indices, expr->location);
            return Expr::make_binary(leaf_op, l_scalar, r_scalar, expr->location);
        }
        uint64_t count = result_shape[depth];
        std::vector<ExprPtr> elements;
        elements.reserve(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i) {
            result_indices.push_back(i);
            elements.push_back(build(depth + 1, result_indices));
            result_indices.pop_back();
        }
        return Expr::make_array(elements, expr->location);
    };

    std::vector<uint64_t> result_indices;
    ExprPtr rewritten = build(0, result_indices);
    replace_expr_in_place(expr, rewritten);
    return true;
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
            Symbol* sym = lookup_binding(expr.get());
            if (sym && sym->name != expr->name) {
                // Rewriters may replace AST nodes in place. Bindings is keyed by
                // raw node pointers, so allocator reuse can surface stale
                // bindings for newly created identifiers at the same address.
                sym = nullptr;
            }
            if (!sym) {
                sym = expr->resolved_symbol;
            }
            if (!sym) {
                sym = lookup_value_global(expr->name);
                if (sym && bindings) {
                    bindings->bind(current_instance_id, expr.get(), sym);
                }
            }
            if (!sym) {
                throw CompileError("Undefined identifier: " + expr->name,
                                   expr->location,
                                   CompileErrorCode::UndefinedIdentifier);
            }
            expr->resolved_symbol = sym;
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

    if (try_rewrite_dotted_array_binary(expr, left_type, right_type)) {
        return check_expr(expr);
    }

    auto concretize_untyped_against = [&](ExprPtr side_expr, TypePtr& side_type, TypePtr target_type, const std::string& op_name) {
        if (!is_untyped_integer_type(side_type)) {
            return;
        }
        if (!target_type || target_type->kind != Type::Kind::Primitive ||
            target_type->primitive == PrimitiveType::Bool ||
            is_untyped_integer_type(target_type)) {
            return;
        }
        if (!apply_type_constraint(side_expr, target_type)) {
            throw CompileError("Integer literal is not representable for operator " + op_name,
                               side_expr ? side_expr->location : expr->location);
        }
        side_type = target_type;
    };

    if (is_fixed_primitive_type(left_type) || is_fixed_primitive_type(right_type)) {
        if (!is_fixed_primitive_type(left_type) || !is_fixed_primitive_type(right_type) ||
            !types_equal(left_type, right_type)) {
            throw CompileError("Fixed-point operators currently require matching fixed-point operand types",
                               expr->location);
        }
        if (expr->op == "&" || expr->op == "|" || expr->op == "^" ||
            expr->op == "<<" || expr->op == ">>") {
            if (!fixed_bitwise_shift_supported(left_type)) {
                throw CompileError(
                    "Fixed-point bitwise/shift operators require fixed-point operands with positive storage width",
                    expr->location);
            }
            expr->type = left_type;
            return expr->type;
        }
        if (expr->op == "+" || expr->op == "-") {
            expr->type = left_type;
            return expr->type;
        }
        if (expr->op == "*" || expr->op == "/" || expr->op == "%") {
            expr->type = left_type;
            return expr->type;
        }
        if (expr->op == "==" || expr->op == "!=" || expr->op == "<" ||
            expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
            expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
            return expr->type;
        }
        throw CompileError("Fixed-point operator '" + expr->op + "' is not implemented yet", expr->location);
    }

    auto is_numeric_like = [&](TypePtr t) {
        return !t || t->kind == Type::Kind::TypeVar || is_numeric_primitive_type(t);
    };

    if (expr->op == "&&" || expr->op == "||") {
        std::string context = expr->op == "&&" ? "Logical operator &&" : "Logical operator ||";
        require_boolean_expr(expr->left, left_type, expr->left->location, context);
        require_boolean_expr(expr->right, right_type, expr->right->location, context);
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
        concretize_untyped_against(expr->left, left_type, right_type, expr->op);
        concretize_untyped_against(expr->right, right_type, left_type, expr->op);
        if (!is_numeric_like(left_type) || !is_numeric_like(right_type)) {
            throw CompileError("Operator " + expr->op + " requires numeric operands", expr->location);
        }
        TypePtr result = unify_types(left_type, right_type);
        if (!result) {
            throw CompileError("Operator " + expr->op + " requires operands from the same numeric family", expr->location);
        }
        expr->type = result;
        return result;
    }

    // Modulo and bitwise: unsigned only
    if (expr->op == "%" || expr->op == "&" || expr->op == "|" || expr->op == "^" ||
        expr->op == "<<" || expr->op == ">>") {
        concretize_untyped_against(expr->left, left_type, right_type, expr->op);
        concretize_untyped_against(expr->right, right_type, left_type, expr->op);
        auto materialize_unsigned_untyped = [&](ExprPtr side_expr, TypePtr& side_type) {
            if (!is_untyped_integer_type(side_type)) return;
            CTEQueryResult query = query_constexpr(side_expr);
            if (query.status != CTEQueryStatus::Known) {
                return;
            }

            uint64_t value = 0;
            if (std::holds_alternative<uint64_t>(query.value)) {
                value = std::get<uint64_t>(query.value);
            } else if (std::holds_alternative<int64_t>(query.value)) {
                int64_t signed_value = std::get<int64_t>(query.value);
                if (signed_value < 0) {
                    throw CompileError("Operator " + expr->op + " requires unsigned integer operands",
                                       side_expr ? side_expr->location : expr->location);
                }
                value = static_cast<uint64_t>(signed_value);
            } else if (std::holds_alternative<bool>(query.value)) {
                value = std::get<bool>(query.value) ? 1ULL : 0ULL;
            } else {
                return;
            }

            TypePtr inferred = make_int_type(side_expr ? side_expr->location : expr->location,
                                             normalize_inferred_int_bits(min_unsigned_bits(value)),
                                             true);
            apply_type_constraint(side_expr, inferred);
            side_type = inferred;
        };
        materialize_unsigned_untyped(expr->left, left_type);
        materialize_unsigned_untyped(expr->right, right_type);
        require_unsigned_integer(left_type, expr->left ? expr->left->location : expr->location,
                                 "Operator " + expr->op);
        require_unsigned_integer(right_type, expr->right ? expr->right->location : expr->location,
                                 "Operator " + expr->op);

        if (expr->op == "<<" || expr->op == ">>") {
            expr->type = left_type;
            return expr->type;
        }

        expr->type = unify_types(left_type, right_type);
        if (!expr->type) {
            throw CompileError("Operator " + expr->op + " requires operands from the same numeric family", expr->location);
        }
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
    std::vector<Symbol*> overloads = lookup_functions_global(func_name);
    if (overloads.size() != 1) {
        return nullptr;
    }
    Symbol* sym = overloads.front();
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

TypePtr TypeChecker::check_unary(ExprPtr expr) {
    TypePtr operand_type = check_expr(expr->operand);
    auto is_numeric_like = [&](TypePtr t) {
        return !t || t->kind == Type::Kind::TypeVar || is_numeric_primitive_type(t);
    };

    if (expr->op == "-") {
        if (is_fixed_primitive_type(operand_type)) {
            if (!(fixed_native_storage_width_supported(operand_type) ||
                  type_bits(operand_type->primitive, operand_type->integer_bits, operand_type->fractional_bits) > 0)) {
                throw CompileError(
                    "Fixed-point unary operators currently support fixed-point operands with positive storage width",
                    expr->location);
            }
            expr->type = operand_type;
            return operand_type;
        }
        if (!is_numeric_like(operand_type)) {
            throw CompileError("Unary - requires numeric operand", expr->location);
        }
        if (is_untyped_integer_type(operand_type)) {
            operand_type = make_int_type(expr->location, 0, false);
            if (expr->operand) {
                expr->operand->type = operand_type;
            }
        }
        expr->type = operand_type;
        return operand_type;
    }

    if (expr->op == "!") {
        require_boolean_expr(expr->operand, operand_type,
                             expr->operand ? expr->operand->location : expr->location,
                             "Logical operator !");
        expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
        return expr->type;
    }

        if (expr->op == "~") {
        if (is_fixed_primitive_type(operand_type)) {
            if (!fixed_bitwise_shift_supported(operand_type)) {
                throw CompileError(
                    "Bitwise NOT on fixed-point values requires a fixed-point operand with positive storage width",
                    expr->location);
            }
        } else if (operand_type && operand_type->kind == Type::Kind::Primitive &&
                   !is_unsigned_int(operand_type->primitive)) {
            throw CompileError("Bitwise NOT requires unsigned integer", expr->location);
        }
        expr->type = operand_type;
        return operand_type;
    }

    return operand_type;
}

TypeChecker::OverloadResolutionResult TypeChecker::resolve_function_overload(
    const std::vector<Symbol*>& candidates,
    const std::vector<TypePtr>& receiver_types,
    const std::vector<ExprPtr>& args) {
    OverloadResolutionResult result;
    std::vector<bool> arg_checked(args.size(), false);

    auto candidate_matches = [&](Symbol* candidate, int& specificity, bool& exact_match) -> bool {
        if (!candidate || candidate->kind != Symbol::Kind::Function || !candidate->declaration) {
            return false;
        }
        StmtPtr decl = candidate->declaration;
        if (decl->ref_params.size() != receiver_types.size()) {
            return false;
        }
        if (decl->params.size() != args.size()) {
            return false;
        }

        specificity = 0;
        exact_match = true;

        for (size_t i = 0; i < receiver_types.size(); ++i) {
            TypePtr param_type = nullptr;
            if (i == 0 && !decl->type_namespace.empty()) {
                param_type = Type::make_named(decl->type_namespace, candidate->declaration->location);
            } else if (i < decl->ref_param_types.size()) {
                param_type = decl->ref_param_types[i];
            }
            if (!param_type || param_type->kind == Type::Kind::TypeVar) {
                exact_match = false;
                continue;
            }
            specificity += 2;
            if (!types_compatible(receiver_types[i], param_type)) {
                return false;
            }
            if (!types_equal(receiver_types[i], param_type)) {
                exact_match = false;
            }
        }

        for (size_t i = 0; i < args.size(); ++i) {
            const Parameter& param = decl->params[i];
            if (param.is_expression_param) {
                exact_match = false;
                continue;
            }
            ExprPtr arg_expr = args[i];
            if (!arg_checked[i]) {
                check_expr(arg_expr);
                arg_checked[i] = true;
            }
            if (!param.type || param.type->kind == Type::Kind::TypeVar) {
                exact_match = false;
                continue;
            }
            specificity += 1;
            if (!types_compatible(arg_expr->type, param.type) &&
                !literal_assignable_to(param.type, arg_expr)) {
                return false;
            }
            if (!types_equal(arg_expr->type, param.type)) {
                exact_match = false;
            }
        }
        return true;
    };

    Symbol* best = nullptr;
    int best_specificity = std::numeric_limits<int>::min();
    bool best_exact = false;
    bool ambiguous = false;
    for (Symbol* candidate : candidates) {
        int specificity = 0;
        bool exact = false;
        if (!candidate_matches(candidate, specificity, exact)) {
            continue;
        }
        if (!best ||
            exact > best_exact ||
            (exact == best_exact && specificity > best_specificity)) {
            best = candidate;
            best_specificity = specificity;
            best_exact = exact;
            ambiguous = false;
            continue;
        }
        if (exact == best_exact && specificity == best_specificity) {
            ambiguous = true;
        }
    }

    if (!best) {
        result.status = OverloadResolutionResult::Status::NoMatch;
        return result;
    }
    if (ambiguous) {
        result.status = OverloadResolutionResult::Status::Ambiguous;
        return result;
    }

    result.status = OverloadResolutionResult::Status::Unique;
    result.symbol = best;
    return result;
}

TypePtr TypeChecker::check_probe_call(ExprPtr expr) {
    auto materialize_result = [&](bool exists) -> TypePtr {
        expr->kind = Expr::Kind::IntLiteral;
        expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
        expr->uint_val = exists ? 1 : 0;
        expr->exact_int_val = APInt(static_cast<int64_t>(exists ? 1 : 0));
        expr->has_exact_int_val = true;
        expr->literal_is_unsigned = false;
        expr->raw_literal = exists ? "1" : "0";
        expr->name.clear();
        expr->op.clear();
        expr->left = nullptr;
        expr->right = nullptr;
        expr->operand = nullptr;
        expr->condition = nullptr;
        expr->true_expr = nullptr;
        expr->false_expr = nullptr;
        expr->args.clear();
        expr->receivers.clear();
        expr->elements.clear();
        expr->result_expr = nullptr;
        expr->is_constructor_call = false;
        expr->is_existence_probe = false;
        return expr->type;
    };

    auto probe_type_ready = [&](TypePtr type, bool allow_untyped_integer_literal) -> bool {
        std::function<bool(TypePtr)> ready = [&](TypePtr t) -> bool {
            t = resolve_type(t);
            if (!t) return false;
            switch (t->kind) {
                case Type::Kind::Primitive:
                    if (is_untyped_integer_type(t)) {
                        return allow_untyped_integer_literal;
                    }
                    return true;
                case Type::Kind::Named:
                    return true;
                case Type::Kind::Array:
                    return ready(t->element_type);
                case Type::Kind::TypeVar:
                case Type::Kind::TypeOf:
                    return false;
            }
            return false;
        };
        return ready(type);
    };

    std::vector<TypePtr> receiver_types;
    if (!expr->receivers.empty()) {
        receiver_types.reserve(expr->receivers.size());
        bool multi_receiver = expr->receivers.size() > 1;
        for (const auto& rec : expr->receivers) {
            if (multi_receiver && rec && rec->kind != Expr::Kind::Identifier) {
                throw CompileError("Multi-receiver calls require identifier receivers", expr->location);
            }
            TypePtr rec_type = check_expr(rec);
            if (!probe_type_ready(rec_type, false)) {
                throw CompileError("Existence probe requires concrete receiver types", rec->location);
            }
            receiver_types.push_back(rec_type);
        }
    }

    if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
        throw CompileError("Existence probe requires a direct member call name", expr->location);
    }

    const std::string original_name = expr->operand->name;
    std::string func_name = original_name;
    if (expr->receivers.size() == 1) {
        TypePtr receiver_type = receiver_types.empty() ? nullptr : receiver_types[0];
        if (receiver_type && receiver_type->kind == Type::Kind::Named &&
            original_name.find("::") == std::string::npos) {
            func_name = receiver_type->type_name + "::" + original_name;
        }
    }

    for (const auto& arg : expr->args) {
        TypePtr arg_type = check_expr(arg);
        const bool allow_untyped_integer = arg && arg->kind == Expr::Kind::IntLiteral;
        if (!probe_type_ready(arg_type, allow_untyped_integer)) {
            throw CompileError("Existence probe requires concrete argument types", arg->location);
        }
    }

    std::vector<Symbol*> candidates = lookup_functions_global(func_name);
    if (candidates.empty()) {
        return materialize_result(false);
    }

    OverloadResolutionResult resolved = resolve_function_overload(candidates, receiver_types, expr->args);
    if (resolved.status == OverloadResolutionResult::Status::Ambiguous) {
        throw CompileError("Ambiguous overload for function: " + func_name, expr->location);
    }
    return materialize_result(resolved.status == OverloadResolutionResult::Status::Unique);
}

TypePtr TypeChecker::check_probe_member(ExprPtr expr) {
    auto materialize_result = [&](bool exists) -> TypePtr {
        expr->kind = Expr::Kind::IntLiteral;
        expr->type = Type::make_primitive(PrimitiveType::Bool, expr->location);
        expr->uint_val = exists ? 1 : 0;
        expr->exact_int_val = APInt(static_cast<int64_t>(exists ? 1 : 0));
        expr->has_exact_int_val = true;
        expr->literal_is_unsigned = false;
        expr->raw_literal = exists ? "1" : "0";
        expr->name.clear();
        expr->op.clear();
        expr->left = nullptr;
        expr->right = nullptr;
        expr->operand = nullptr;
        expr->condition = nullptr;
        expr->true_expr = nullptr;
        expr->false_expr = nullptr;
        expr->args.clear();
        expr->receivers.clear();
        expr->elements.clear();
        expr->result_expr = nullptr;
        expr->is_constructor_call = false;
        expr->is_existence_probe = false;
        return expr->type;
    };

    auto receiver_ready = [&](TypePtr type) -> bool {
        std::function<bool(TypePtr)> ready = [&](TypePtr t) -> bool {
            t = resolve_type(t);
            if (!t) return false;
            switch (t->kind) {
                case Type::Kind::Primitive:
                    return !is_untyped_integer_type(t);
                case Type::Kind::Named:
                    return true;
                case Type::Kind::Array:
                    return ready(t->element_type);
                case Type::Kind::TypeVar:
                case Type::Kind::TypeOf:
                    return false;
            }
            return false;
        };
        return ready(type);
    };

    TypePtr obj_type = check_expr(expr->operand);
    if (!receiver_ready(obj_type)) {
        throw CompileError("Existence probe requires a concrete receiver type", expr->location);
    }

    obj_type = resolve_type(obj_type);
    if (obj_type && obj_type->kind == Type::Kind::Named) {
        if (obj_type->type_name.find(TUPLE_TYPE_PREFIX) == 0 &&
            expr->name.size() >= 3 && expr->name.substr(0, 2) == std::string(MANGLED_PREFIX)) {
            size_t field_index = std::stoull(expr->name.substr(2));
            auto tuple_it = forced_tuple_types.find(obj_type->type_name);
            if (tuple_it != forced_tuple_types.end()) {
                return materialize_result(field_index < tuple_it->second.size());
            }
            return materialize_result(false);
        }

        Symbol* type_sym = nullptr;
        if (bindings) {
            type_sym = bindings->lookup(current_instance_id, obj_type.get());
        }
        if (!type_sym) {
            type_sym = lookup_type_global(obj_type->type_name);
        }
        if (type_sym && type_sym->kind == Symbol::Kind::Type && type_sym->declaration) {
            for (const auto& field : type_sym->declaration->fields) {
                if (field.name == expr->name) {
                    return materialize_result(true);
                }
            }
        }
    }

    return materialize_result(false);
}

TypePtr TypeChecker::check_call(ExprPtr expr) {
    if (expr->is_existence_probe) {
        return check_probe_call(expr);
    }

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
        const std::string original_name = expr->operand->name;
        func_name = original_name;

        if (expr->receivers.size() == 1) {
            TypePtr receiver_type = receiver_types.empty() ? nullptr : receiver_types[0];
            if (receiver_type && receiver_type->kind == Type::Kind::Named && original_name.find("::") == std::string::npos) {
                func_name = receiver_type->type_name + "::" + original_name;
            }
        }

        Symbol* bound_sym = lookup_binding(expr->operand.get());
        if (bound_sym &&
            ((bound_sym->kind == Symbol::Kind::Function && bound_sym->name == expr->operand->name) ||
             (bound_sym->kind == Symbol::Kind::Type && bound_sym->surface_name == func_name))) {
            sym = bound_sym;
        }

        if (expr->is_constructor_call) {
            if (!sym) {
                sym = lookup_type_global(func_name);
            }
            if (!sym) {
                throw CompileError("Undefined constructor type: " + func_name,
                                   expr->location,
                                   CompileErrorCode::UndefinedConstructorType);
            }
            expr->operand->name = sym->surface_name;
            has_symbol = true;
        } else {
            std::vector<Symbol*> candidates;
            if (sym && sym->kind == Symbol::Kind::Function) {
                candidates.push_back(sym);
            }
            if (candidates.empty()) {
                candidates = lookup_functions_global(func_name);
            }
            if (candidates.empty()) {
                Symbol* type_sym = lookup_type_global(func_name);
                if (type_sym) {
                    sym = type_sym;
                    has_symbol = true;
                } else {
                    throw CompileError("Undefined function: " + func_name,
                                       expr->location,
                                       CompileErrorCode::UndefinedFunction);
                }
            } else {
                auto try_std_math_array_overload = [&]() -> OverloadResolutionResult {
                    OverloadResolutionResult math_result;
                    if (!expr->receivers.empty()) {
                        return math_result;
                    }

                    std::vector<TypePtr> scalar_arg_types;
                    scalar_arg_types.reserve(expr->args.size());
                    bool saw_array_like = false;
                    for (const auto& arg : expr->args) {
                        TypePtr arg_type = arg ? resolve_type(arg->type) : nullptr;
                        if (is_array_like_type(arg_type)) {
                            saw_array_like = true;
                            scalar_arg_types.push_back(array_like_scalar_type(arg_type));
                        } else {
                            scalar_arg_types.push_back(arg_type);
                        }
                    }
                    if (!saw_array_like) {
                        return math_result;
                    }

                    auto candidate_matches = [&](Symbol* candidate, int& specificity, bool& exact_match) -> bool {
                        if (!candidate || candidate->kind != Symbol::Kind::Function || !candidate->declaration) {
                            return false;
                        }
                        if (!is_bundled_std_math_symbol(candidate)) {
                            return false;
                        }
                        StmtPtr decl = candidate->declaration;
                        if (!decl->ref_params.empty() || decl->params.size() != scalar_arg_types.size()) {
                            return false;
                        }

                        specificity = 0;
                        exact_match = true;
                        for (size_t i = 0; i < scalar_arg_types.size(); ++i) {
                            TypePtr arg_type = scalar_arg_types[i];
                            TypePtr param_type = decl->params[i].type;
                            if (!param_type || param_type->kind == Type::Kind::TypeVar) {
                                exact_match = false;
                                continue;
                            }
                            specificity += 1;
                            if (!types_compatible(arg_type, param_type) &&
                                !literal_assignable_to(param_type, expr->args[i])) {
                                return false;
                            }
                            if (!types_equal(arg_type, param_type)) {
                                exact_match = false;
                            }
                        }
                        return true;
                    };

                    Symbol* best = nullptr;
                    int best_specificity = std::numeric_limits<int>::min();
                    bool best_exact = false;
                    bool ambiguous = false;
                    for (Symbol* candidate : candidates) {
                        int specificity = 0;
                        bool exact = false;
                        if (!candidate_matches(candidate, specificity, exact)) {
                            continue;
                        }
                        if (!best ||
                            exact > best_exact ||
                            (exact == best_exact && specificity > best_specificity)) {
                            best = candidate;
                            best_specificity = specificity;
                            best_exact = exact;
                            ambiguous = false;
                            continue;
                        }
                        if (exact == best_exact && specificity == best_specificity) {
                            ambiguous = true;
                        }
                    }

                    if (!best) {
                        return math_result;
                    }
                    if (ambiguous) {
                        math_result.status = OverloadResolutionResult::Status::Ambiguous;
                        return math_result;
                    }
                    math_result.status = OverloadResolutionResult::Status::Unique;
                    math_result.symbol = best;
                    return math_result;
                };

                OverloadResolutionResult resolved = resolve_function_overload(candidates, receiver_types, expr->args);
                if (resolved.status == OverloadResolutionResult::Status::NoMatch) {
                    OverloadResolutionResult math_resolved = try_std_math_array_overload();
                    if (math_resolved.status == OverloadResolutionResult::Status::Ambiguous) {
                        throw CompileError("Ambiguous overload for function: " + func_name, expr->location);
                    }
                    if (math_resolved.status == OverloadResolutionResult::Status::NoMatch) {
                        throw CompileError("No matching overload for function: " + func_name,
                                           expr->location,
                                           CompileErrorCode::NoMatchingFunctionOverload);
                    }
                    resolved = math_resolved;
                }
                if (resolved.status == OverloadResolutionResult::Status::Ambiguous) {
                    throw CompileError("Ambiguous overload for function: " + func_name, expr->location);
                }
                sym = resolved.symbol;
                expr->operand->name = sym->name;
                has_symbol = true;
            }
        }
        if (has_symbol && bindings) {
            bindings->bind(current_instance_id, expr->operand.get(), sym);
        }
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

    if (try_rewrite_std_math_array_call(expr, sym)) {
        return check_expr(expr);
    }

    auto invalidate_receiver_constexpr = [&]() {
        auto lookup_identifier_symbol = [&](ExprPtr node) -> Symbol* {
            if (!node || node->kind != Expr::Kind::Identifier) return nullptr;
            Symbol* target = lookup_binding(node.get());
            if (!target) {
                target = lookup_value_global(node->name);
                if (target && bindings) {
                    bindings->bind(current_instance_id, node.get(), target);
                }
            }
            return target;
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
        for (const auto& rec : expr->receivers) {
            Symbol* target = base_symbol(rec);
            if (target) {
                forget_constexpr_value(target);
            }
        }
    };

    if (sym->kind == Symbol::Kind::Type && !expr->is_constructor_call) {
        throw CompileError("Type '" + expr->operand->name + "' is not callable; use #" +
                           expr->operand->name + "(...)",
                           expr->location);
    }

    if (sym->kind == Symbol::Kind::Type && sym->declaration) {
        for (size_t i = 0; i < expr->args.size() && i < sym->declaration->fields.size(); i++) {
            TypePtr field_type = sym->declaration->fields[i].type;
            ExprPtr arg_expr = expr->args[i];

            if (!field_type || field_type->kind == Type::Kind::TypeVar) {
                continue;
            }

            (void)apply_type_constraint(arg_expr, field_type);

            if (!types_compatible(arg_expr->type, field_type) &&
                !literal_assignable_to(field_type, arg_expr)) {
                throw CompileError("Constructor argument is not compatible with field '" +
                                   sym->declaration->fields[i].name + "'",
                                   arg_expr->location);
            }
        }

        expr->type = Type::make_named(expr->operand->name, expr->location);
        if (bindings) {
            bindings->bind(current_instance_id, expr->type.get(), sym);
        }
        return expr->type;
    }

    if (sym->kind == Symbol::Kind::Function && sym->declaration) {
        bool is_generic_func = !sym->declaration->is_instantiation &&
                               is_generic_function(sym->declaration);
        sym->declaration->is_generic = is_generic_func;

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
                TypePtr declared_receiver = nullptr;
                if (!sym->declaration->type_namespace.empty() && i == 0) {
                    declared_receiver = Type::make_named(sym->declaration->type_namespace, expr->location);
                } else {
                    declared_receiver = sym->declaration->ref_param_types[i];
                }

                if (is_generic_func) {
                    if (declared_receiver &&
                        declared_receiver->kind != Type::Kind::TypeVar &&
                        !types_compatible(recv_type, declared_receiver)) {
                        throw CompileError(
                            "Receiver '" + sym->declaration->ref_params[i] + "' expects type " +
                            declared_receiver->to_string(),
                            expr->location);
                    }
                    continue;
                }

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

        // Validate argument count (skip expression parameters)
        size_t expected_args = sym->declaration->params.size();
        if (expr->args.size() != expected_args) {
            throw CompileError(
                "Function '" + sym->declaration->func_name + "' expects " +
                std::to_string(expected_args) + " argument(s)", expr->location);
        }

        if (is_generic_func) {
            std::vector<TypePtr> call_types;
            call_types.reserve(receiver_types.size() + expr->args.size());
            for (const auto& receiver_type : receiver_types) {
                call_types.push_back(receiver_type);
            }
            for (size_t i = 0; i < expr->args.size() && i < sym->declaration->params.size(); i++) {
                ExprPtr arg_expr = expr->args[i];
                TypePtr param_type = sym->declaration->params[i].type;
                bool param_is_unconstrained = !param_type || param_type->kind == Type::Kind::TypeVar;
                if (type_strictness >= 2 &&
                    !sym->declaration->params[i].is_expression_param &&
                    is_untyped_integer_type(arg_expr->type) &&
                    param_is_unconstrained) {
                    throw CompileError(
                        "Type strictness level 2 requires explicit cast or parameter type for argument '" +
                            sym->declaration->params[i].name + "' in call to '" +
                            sym->declaration->func_name + "'",
                        expr->location);
                }
                if (param_type && param_type->kind != Type::Kind::TypeVar) {
                    (void)apply_type_constraint(arg_expr, param_type);
                    if (!types_compatible(arg_expr->type, param_type) &&
                        !literal_assignable_to(param_type, arg_expr)) {
                        throw CompileError(
                            "Type mismatch for parameter '" + sym->declaration->params[i].name +
                            "' in call to '" + sym->declaration->func_name + "'", expr->location);
                    }
                }

                if (!sym->declaration->params[i].is_expression_param) {
                    call_types.push_back(arg_expr->type);
                }
            }

            int instantiation_owner = sym->is_local ? current_instance_id : sym->instance_id;
            std::string mangled_name = get_or_create_instantiation(sym->name,
                                                                   call_types,
                                                                   sym->declaration,
                                                                   instantiation_owner);
            expr->operand->name = mangled_name;
            if (bindings) {
                Symbol* inst_sym = resolver
                    ? resolver->lookup_internal_in_instance(instantiation_owner, mangled_name)
                    : (global_scope ? global_scope->lookup_internal(mangled_name) : nullptr);
                if (inst_sym) {
                    bindings->bind(current_instance_id, expr->operand.get(), inst_sym);
                }
            }

            TypeSignature sig;
            sig.param_types = call_types;
            std::string lookup_key = sym->name + "_inst" + std::to_string(instantiation_owner);
            auto func_it = instantiations.find(lookup_key);
            if (func_it != instantiations.end()) {
                auto inst_it = func_it->second.find(sig);
                if (inst_it != func_it->second.end()) {
                    expr->type = inst_it->second.declaration->return_type;
                    invalidate_receiver_constexpr();
                    return expr->type;
                }
            }

            expr->type = make_fresh_typevar();
            invalidate_receiver_constexpr();
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

            bool param_is_unconstrained = !param_type || param_type->kind == Type::Kind::TypeVar;
            if (type_strictness >= 2 &&
                is_untyped_integer_type(arg_expr->type) &&
                param_is_unconstrained) {
                throw CompileError(
                    "Type strictness level 2 requires explicit cast or parameter type for argument '" +
                        param.name + "' in call to '" + sym->declaration->func_name + "'",
                    expr->location);
            }

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
            (void)apply_type_constraint(arg_expr, param_type);
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
            invalidate_receiver_constexpr();
            return expr->type;
        }

        if (sym->declaration->return_type) {
            expr->type = sym->declaration->return_type;
            invalidate_receiver_constexpr();
            return expr->type;
        }

        // No declared return type: treat as void (no value)
        expr->type = nullptr;
        invalidate_receiver_constexpr();
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
        expr->type = make_int_type(expr->location, 8, true);
        return expr->type;
    }

    expr->type = make_fresh_typevar();
    return expr->type;
}

TypePtr TypeChecker::check_member(ExprPtr expr) {
    if (expr->is_existence_probe) {
        return check_probe_member(expr);
    }

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
            if (!field_type_str.empty() && field_type_str[0] == '#') {
                field_type_str = field_type_str.substr(1);
            }
            TypePtr field_type;
            if (field_type_str == "i") {
                field_type = Type::make_primitive(PrimitiveType::Int, expr->location, 0);
            } else if (field_type_str == "u") {
                field_type = Type::make_primitive(PrimitiveType::UInt, expr->location, 0);
            } else {
                field_type = parse_type_from_string(field_type_str, expr->location);
            }
            expr->type = field_type;
            return expr->type;
        }

        Symbol* type_sym = nullptr;
        if (bindings) {
            type_sym = bindings->lookup(current_instance_id, obj_type.get());
        }
        if (!type_sym) {
            type_sym = lookup_type_global(obj_type->type_name);
        }
        if (type_sym && type_sym->kind == Symbol::Kind::Type && type_sym->declaration) {
            // Find the field in the type declaration
            for (const auto& field : type_sym->declaration->fields) {
                if (field.name == expr->name) {
                    expr->type = field.type;
                    return expr->type;
                }
            }
            throw CompileError("Type " + obj_type->type_name + " has no field: " + expr->name,
                               expr->location,
                               CompileErrorCode::MissingField);
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
    for (size_t i = 0; i < expr->elements.size(); ++i) {
        auto& elem = expr->elements[i];
        TypePtr et = check_expr(elem);
        if (!elem_type) {
            elem_type = et;
        } else {
            TypePtr unified = unify_types(elem_type, et);
            if (!unified) {
                throw CompileError("Array literal elements must have compatible types", elem->location);
            }
            elem_type = unified;
        }
    }

    std::function<bool(TypePtr)> fully_concrete = [&](TypePtr t) -> bool {
        if (!t) return false;
        if (t->kind == Type::Kind::TypeVar || t->kind == Type::Kind::TypeOf) return false;
        if (t->kind == Type::Kind::Primitive) {
            return !is_untyped_integer_type(t);
        }
        if (t->kind == Type::Kind::Array) {
            return fully_concrete(t->element_type);
        }
        return true;
    };

    if (elem_type && fully_concrete(elem_type)) {
        for (auto& elem : expr->elements) {
            if (!elem) continue;
            if (!apply_type_constraint(elem, elem_type)) {
                throw CompileError("Array literal elements must have compatible types", elem->location);
            }
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

    bool tuple_fully_concrete = true;
    for (const auto& et : element_types) {
        TypePtr rt = resolve_type(et);
        if (!rt ||
            rt->kind == Type::Kind::TypeVar ||
            rt->kind == Type::Kind::TypeOf ||
            is_untyped_integer_type(rt)) {
            tuple_fully_concrete = false;
            break;
        }
    }
    if (tuple_fully_concrete) {
        register_tuple_type(type_name, element_types);
    }

    // Create named type (the actual struct will be generated in codegen)
    expr->type = Type::make_named(type_name, expr->location);
    return expr->type;
}

bool TypeChecker::types_equal(TypePtr a, TypePtr b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case Type::Kind::Primitive:
            if (a->primitive != b->primitive) return false;
            if (is_signed_int(a->primitive) || is_unsigned_int(a->primitive) ||
                is_signed_fixed(a->primitive) || is_unsigned_fixed(a->primitive)) {
                return a->integer_bits == b->integer_bits &&
                       a->fractional_bits == b->fractional_bits;
            }
            return true;
        case Type::Kind::Array:
            // Arrays are equal if element types match AND sizes match
            if (!types_equal(a->element_type, b->element_type)) {
                return false;
            }
            // Check array sizes if both are known
            if (a->array_size && b->array_size &&
                a->array_size->kind == Expr::Kind::IntLiteral &&
                b->array_size->kind == Expr::Kind::IntLiteral) {
                if (a->array_size->has_exact_int_val && b->array_size->has_exact_int_val) {
                    return a->array_size->exact_int_val == b->array_size->exact_int_val;
                }
                return a->array_size->uint_val == b->array_size->uint_val;
            }
            return true; // Unknown sizes are considered equal
        case Type::Kind::Named:
            return a->type_name == b->type_name;
        case Type::Kind::TypeVar:
            return a->var_name == b->var_name;
        case Type::Kind::TypeOf:
            return a->typeof_expr.get() == b->typeof_expr.get();
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
                bool size_equal = false;
                if (a->array_size->has_exact_int_val && b->array_size->has_exact_int_val) {
                    size_equal = (a->array_size->exact_int_val == b->array_size->exact_int_val);
                } else {
                    size_equal = (a->array_size->uint_val == b->array_size->uint_val);
                }
                if (!size_equal) {
                    return false;
                }
            }
        }

        return true;
    }

    // Enhanced type promotion for primitives
    if (a->kind == Type::Kind::Primitive && b->kind == Type::Kind::Primitive) {
        // Untyped integers are not implicitly compatible with concrete numeric
        // types. They must be concretized through representability checks.
        if (is_untyped_integer_type(a)) {
            return is_untyped_integer_type(b);
        }
        if (is_untyped_integer_type(b)) {
            return false;
        }

        // Same type family check
        if (types_in_same_family(a, b)) {
            return effective_type_bits(a) <= effective_type_bits(b);
        }

        // No cross-category promotions (need explicit cast)
        return false;
    }

    return false;
}

TypePtr TypeChecker::unify_types(TypePtr a, TypePtr b) {
    if (!a) return b;
    if (!b) return a;
    if (a->kind == Type::Kind::TypeVar && b->kind == Type::Kind::TypeVar) {
        if (a->var_name == b->var_name) {
            return a;
        }
        bind_typevar(b, a);
        return a;
    }
    if (a->kind == Type::Kind::TypeVar) {
        return bind_typevar(a, b);
    }
    if (b->kind == Type::Kind::TypeVar) {
        return bind_typevar(b, a);
    }
    if (types_equal(a, b)) return a;

    if (a->kind == Type::Kind::Array && b->kind == Type::Kind::Array) {
        TypePtr unified_elem = unify_types(a->element_type, b->element_type);
        if (!unified_elem) {
            return nullptr;
        }

        ExprPtr size = nullptr;
        if (a->array_size && b->array_size) {
            if (a->array_size->kind == Expr::Kind::IntLiteral &&
                b->array_size->kind == Expr::Kind::IntLiteral) {
                bool size_equal = false;
                if (a->array_size->has_exact_int_val && b->array_size->has_exact_int_val) {
                    size_equal = (a->array_size->exact_int_val == b->array_size->exact_int_val);
                } else {
                    size_equal = (a->array_size->uint_val == b->array_size->uint_val);
                }
                if (!size_equal) {
                    return nullptr;
                }
                size = a->array_size;
            } else if (a->array_size.get() == b->array_size.get()) {
                size = a->array_size;
            } else {
                size = a->array_size;
            }
        } else if (a->array_size) {
            size = a->array_size;
        } else {
            size = b->array_size;
        }

        return Type::make_array(unified_elem, size, a->location);
    }

    // Enhanced type unification respecting type families
    if (a->kind == Type::Kind::Primitive && b->kind == Type::Kind::Primitive) {
        if (is_untyped_integer_type(a) && is_untyped_integer_type(b)) {
            bool needs_signed = (a->primitive == PrimitiveType::Int) || (b->primitive == PrimitiveType::Int);
            return make_int_type(a->location, 0, !needs_signed);
        }
        if (is_untyped_integer_type(a)) {
            if (b->primitive == PrimitiveType::Bool) {
                return nullptr;
            }
            if (is_numeric_primitive_type(b)) {
                return b;
            }
        }
        if (is_untyped_integer_type(b)) {
            if (a->primitive == PrimitiveType::Bool) {
                return nullptr;
            }
            if (is_numeric_primitive_type(a)) {
                return a;
            }
        }

        // Same family - promote to larger
        if (types_in_same_family(a, b)) {
            if (effective_type_bits(a) <= effective_type_bits(b)) {
                return b;
            }
            return a;
        }

        // Different families - no automatic unification.
        return nullptr;
    }

    return nullptr;
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
    if (type->kind == Type::Kind::TypeOf && type->typeof_expr && type->typeof_expr->type) {
        return resolve_type(type->typeof_expr->type);
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
            // Integer literals are context-typed. They start as unresolved integer
            // carriers and are concretized by surrounding type constraints.
            return Type::make_primitive(expr->literal_is_unsigned ? PrimitiveType::UInt : PrimitiveType::Int,
                                        expr->location,
                                        0);
        }
        case Expr::Kind::FloatLiteral:
            return Type::make_primitive(PrimitiveType::F64, expr->location);
        case Expr::Kind::StringLiteral:
            return Type::make_primitive(PrimitiveType::String, expr->location);
        case Expr::Kind::CharLiteral:
            return make_int_type(expr->location, 8, true);
        default:
            return nullptr;
    }
}

bool TypeChecker::literal_assignable_to(TypePtr target, ExprPtr expr) {
    if (!target || target->kind != Type::Kind::Primitive || !expr) return false;

    if (expr->kind == Expr::Kind::Conditional) {
        if (!expr->true_expr || !expr->false_expr) return false;
        if (auto cond = constexpr_condition(expr->condition)) {
            return literal_assignable_to(target, cond.value() ? expr->true_expr : expr->false_expr);
        }
        return literal_assignable_to(target, expr->true_expr) &&
               literal_assignable_to(target, expr->false_expr);
    }

    TypePtr source_type = resolve_type(expr->type);
    if (source_type &&
        source_type->kind == Type::Kind::Primitive &&
        source_type->primitive == PrimitiveType::Bool) {
        return target->primitive == PrimitiveType::Bool;
    }

    struct IntConstValue {
        bool known = false;
        bool is_unsigned = false;
        bool from_boolean = false;
        APInt value = APInt(uint64_t(0));
    };

    auto load_int_const = [&](ExprPtr node) -> IntConstValue {
        IntConstValue out;
        if (!node) {
            return out;
        }

        Expr::Kind kind = node->kind;
        if (kind == Expr::Kind::CharLiteral) {
            kind = Expr::Kind::IntLiteral;
        }

        if (kind == Expr::Kind::IntLiteral) {
            out.known = true;
            out.is_unsigned = node->literal_is_unsigned;
            if (node->has_exact_int_val) {
                out.value = node->exact_int_val;
            } else if (out.is_unsigned) {
                out.value = APInt(node->uint_val);
            } else {
                out.value = APInt(static_cast<int64_t>(node->uint_val));
            }
            return out;
        }

        CTEQueryResult query = query_constexpr(node);
        if (query.status != CTEQueryStatus::Known) {
            return out;
        }

        const CTValue& value = query.value;
        if (std::holds_alternative<uint64_t>(value)) {
            out.known = true;
            out.is_unsigned = true;
            out.value = APInt(std::get<uint64_t>(value));
        } else if (std::holds_alternative<int64_t>(value)) {
            out.known = true;
            out.is_unsigned = false;
            out.value = APInt(std::get<int64_t>(value));
        } else if (std::holds_alternative<CTExactInt>(value)) {
            out.known = true;
            out.is_unsigned = std::get<CTExactInt>(value).is_unsigned;
            out.value = std::get<CTExactInt>(value).value;
        } else if (std::holds_alternative<bool>(value)) {
            out.known = true;
            out.is_unsigned = true;
            out.from_boolean = true;
            out.value = APInt(uint64_t(std::get<bool>(value) ? 1ULL : 0ULL));
        }
        return out;
    };

    IntConstValue value = load_int_const(expr);
    auto fits_signed_width = [&](uint64_t bits) {
        if (!value.known) return false;
        if (bits == 0) return false;
        return value.value.fits_signed(bits);
    };
    auto fits_unsigned_width = [&](uint64_t bits) {
        if (!value.known) return false;
        if (bits == 0) return false;
        return value.value.fits_unsigned(bits);
    };

    if (value.known) {
        switch (target->primitive) {
            case PrimitiveType::Bool:
                return fits_unsigned_width(1);
            case PrimitiveType::Int:
                if (value.from_boolean) return false;
                return fits_signed_width(target->integer_bits);
            case PrimitiveType::UInt:
                if (value.from_boolean) return false;
                return fits_unsigned_width(target->integer_bits);
            case PrimitiveType::FixedInt:
            case PrimitiveType::FixedUInt:
                return false;
            case PrimitiveType::F16:
            case PrimitiveType::F32:
            case PrimitiveType::F64:
                if (value.from_boolean) return false;
                return true; // Integer literals can widen to floats
            case PrimitiveType::String:
                return false;
        }
    }

    if (expr->kind == Expr::Kind::FloatLiteral) {
        if (target->primitive == PrimitiveType::F16 ||
            target->primitive == PrimitiveType::F32 ||
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
