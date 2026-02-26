#include "evaluator.h"
#include "evaluator_internal.h"
#include "cte_value_utils.h"
#include "typechecker.h"
#include <functional>

namespace vexel {

namespace {

bool is_fixed_primitive_type(const TypePtr& type) {
    return type &&
           type->kind == Type::Kind::Primitive &&
           (is_signed_fixed(type->primitive) || is_unsigned_fixed(type->primitive));
}

bool fixed_native_meta(const TypePtr& type,
                       uint64_t& total_bits,
                       bool& is_signed_raw,
                       int64_t& fractional_bits) {
    if (!is_fixed_primitive_type(type)) return false;
    int64_t bits_i64 = type_bits(type->primitive, type->integer_bits, type->fractional_bits);
    if (!(bits_i64 == 8 || bits_i64 == 16 || bits_i64 == 32 || bits_i64 == 64)) return false;
    total_bits = static_cast<uint64_t>(bits_i64);
    is_signed_raw = (type->primitive == PrimitiveType::FixedInt);
    fractional_bits = type->fractional_bits;
    return true;
}

bool fixed_muldiv_meta_supported(const TypePtr& type,
                                 uint64_t& total_bits,
                                 bool& is_signed_raw,
                                 int64_t& fractional_bits) {
    if (!fixed_native_meta(type, total_bits, is_signed_raw, fractional_bits)) return false;
    return total_bits == 8 || total_bits == 16 || total_bits == 32;
}

APInt trunc_div_pow2(const APInt& value, uint64_t shift) {
    if (shift == 0) return value;
    if (value.is_negative()) {
        APInt mag = -value;
        return -(mag >> shift);
    }
    return value >> shift;
}

APInt scale_by_pow2_trunc_zero(const APInt& value, int64_t shift) {
    if (shift == 0) return value;
    if (shift > 0) return value << static_cast<uint64_t>(shift);
    return trunc_div_pow2(value, static_cast<uint64_t>(-shift));
}

bool fixed_raw_div(const APInt& lhs,
                   const APInt& rhs,
                   int64_t frac_bits,
                   APInt& out) {
    if (rhs.is_zero()) return false;
    if (frac_bits >= 0) {
        out = (lhs << static_cast<uint64_t>(frac_bits)) / rhs;
    } else {
        APInt denom = rhs << static_cast<uint64_t>(-frac_bits);
        if (denom.is_zero()) return false;
        out = lhs / denom;
    }
    return true;
}

bool fixed_raw_mod(const APInt& lhs,
                   const APInt& rhs,
                   int64_t frac_bits,
                   APInt& out) {
    (void)frac_bits;
    if (rhs.is_zero()) return false;
    APInt q = lhs / rhs; // trunc-to-zero integer quotient on same-scale raw values
    out = lhs - (q * rhs);
    return true;
}

} // namespace

bool CompileTimeEvaluator::eval_assignment(ExprPtr expr, CTValue& result) {
    const std::string assign_op = expr->op.empty() ? "=" : expr->op;

    auto is_local = [&](const std::string& name) {
        return constants.count(name) > 0 || uninitialized_locals.count(name) > 0;
    };

    const bool creates_local_identifier =
        expr->creates_new_variable &&
        expr->left &&
        expr->left->kind == Expr::Kind::Identifier;

    std::string base = base_identifier(expr->left);
    if (!base.empty()) {
        if (base == "_") {
            error_msg = "Cannot assign to read-only loop variable '_'";
            return false;
        }
        if (is_ref_param(base)) {
            error_msg = "Cannot mutate receiver at compile time: " + base;
            return false;
        }
        Symbol* sym = type_checker && type_checker->get_scope()
            ? type_checker->get_scope()->lookup(base)
            : nullptr;
        if (!creates_local_identifier &&
            sym &&
            sym->is_external &&
            (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Constant)) {
            error_msg = "External binding cannot be written at compile time: " + base;
            return false;
        }
        if (!creates_local_identifier && sym && !sym->is_mutable && !is_local(base)) {
            error_msg = "Cannot assign to immutable constant: " + base;
            return false;
        }
        if (!creates_local_identifier &&
            sym &&
            sym->kind == Symbol::Kind::Variable &&
            sym->is_mutable &&
            !is_local(base)) {
            error_msg = "Cannot modify mutable globals at compile time: " + base;
            return false;
        }
    }

    auto clone_composite = [&](const std::shared_ptr<CTComposite>& src) {
        if (!src) return std::shared_ptr<CTComposite>();
        auto dst = std::make_shared<CTComposite>();
        dst->type_name = src->type_name;
        for (const auto& entry : src->fields) {
            dst->fields[entry.first] = clone_ct_value(entry.second);
        }
        return dst;
    };
    auto clone_array = [&](const std::shared_ptr<CTArray>& src) {
        if (!src) return std::shared_ptr<CTArray>();
        auto dst = std::make_shared<CTArray>();
        dst->elements.reserve(src->elements.size());
        for (const auto& entry : src->elements) {
            dst->elements.push_back(clone_ct_value(entry));
        }
        return dst;
    };
    auto parse_index = [&](ExprPtr index_expr, int64_t& idx) -> bool {
        CTValue index_val;
        if (!try_evaluate(index_expr, index_val)) return false;
        if (!ctvalue_is_integer(index_val) &&
            !std::holds_alternative<bool>(index_val)) {
            error_msg = "Index must be an integer/bool constant, got " + ct_value_kind(index_val);
            return false;
        }
        if (std::holds_alternative<bool>(index_val)) {
            idx = std::get<bool>(index_val) ? 1 : 0;
        } else if (!ctvalue_to_i64_exact(index_val, idx)) {
            error_msg = "Index out of compile-time range";
            return false;
        }
        if (idx < 0) {
            error_msg = "Index cannot be negative";
            return false;
        }
        return true;
    };

    std::function<bool(ExprPtr, CTValue*&)> resolve_lvalue_slot;
    resolve_lvalue_slot = [&](ExprPtr target, CTValue*& out) -> bool {
        if (!target) {
            error_msg = "Assignment target is not addressable at compile time";
            return false;
        }
        switch (target->kind) {
            case Expr::Kind::Identifier: {
                auto it = constants.find(target->name);
                if (it == constants.end()) {
                    // Assignment writes can materialize an lvalue slot without reading prior value.
                    constants[target->name] = CTUninitialized{};
                    it = constants.find(target->name);
                }
                out = &it->second;
                return true;
            }
            case Expr::Kind::Member: {
                CTValue* base_slot = nullptr;
                if (!resolve_lvalue_slot(target->operand, base_slot)) return false;
                if (!base_slot || !std::holds_alternative<std::shared_ptr<CTComposite>>(*base_slot)) {
                    error_msg = "Member access on non-composite value";
                    return false;
                }
                auto& comp_ref = std::get<std::shared_ptr<CTComposite>>(*base_slot);
                if (!comp_ref) {
                    error_msg = "Member access on null composite value";
                    return false;
                }
                if (!comp_ref.unique()) {
                    comp_ref = clone_composite(comp_ref);
                }
                auto it = comp_ref->fields.find(target->name);
                if (it == comp_ref->fields.end()) {
                    error_msg = "Field not found: " + target->name;
                    return false;
                }
                out = &it->second;
                return true;
            }
            case Expr::Kind::Index: {
                if (target->args.empty()) {
                    error_msg = "Index expression missing index";
                    return false;
                }
                CTValue* base_slot = nullptr;
                if (!resolve_lvalue_slot(target->operand, base_slot)) return false;
                if (!base_slot || !std::holds_alternative<std::shared_ptr<CTArray>>(*base_slot)) {
                    error_msg = "Indexing non-array value at compile time";
                    return false;
                }
                auto& array_ref = std::get<std::shared_ptr<CTArray>>(*base_slot);
                if (!array_ref) {
                    error_msg = "Indexing null array";
                    return false;
                }
                if (!array_ref.unique()) {
                    array_ref = clone_array(array_ref);
                }
                int64_t idx = 0;
                if (!parse_index(target->args[0], idx)) return false;
                if (static_cast<size_t>(idx) >= array_ref->elements.size()) {
                    error_msg = "Index out of bounds in compile-time evaluation";
                    return false;
                }
                out = &array_ref->elements[static_cast<size_t>(idx)];
                return true;
            }
            default:
                error_msg = "Assignment target is not addressable at compile time";
                return false;
        }
    };

    if (creates_local_identifier &&
        constants.count(expr->left->name) == 0 &&
        uninitialized_locals.count(expr->left->name) == 0) {
        constants[expr->left->name] = CTUninitialized{};
    }

    CTValue* slot = nullptr;
    if (!resolve_lvalue_slot(expr->left, slot)) {
        return false;
    }
    if (!slot) {
        error_msg = "Assignment target is not addressable at compile time";
        return false;
    }

    auto evaluate_rhs = [&](CTValue& out) -> bool {
        return try_evaluate(expr->right, out);
    };
    auto apply_compound = [&](const CTValue& lhs_in, const CTValue& rhs_in, CTValue& out) -> bool {
        const std::string binary_op = assign_op.substr(0, assign_op.size() - 1);
        TypePtr fixed_type = expr && expr->left ? expr->left->type : nullptr;
        if (!is_fixed_primitive_type(fixed_type) && expr && expr->type && is_fixed_primitive_type(expr->type)) {
            fixed_type = expr->type;
        }
        uint64_t fixed_bits = 0;
        bool fixed_signed = false;
        int64_t fixed_frac = 0;
        if (is_fixed_primitive_type(fixed_type) &&
            fixed_native_meta(fixed_type, fixed_bits, fixed_signed, fixed_frac)) {
            APInt l(uint64_t(0));
            APInt r(uint64_t(0));
            bool lu = false, ru = false;
            if (!ctvalue_to_exact_int(lhs_in, l, lu) || !ctvalue_to_exact_int(rhs_in, r, ru)) {
                error_msg = "Unsupported operand types for fixed-point compound assignment";
                return false;
            }
            auto wrap_raw = [&](const APInt& raw) {
                return fixed_signed ? raw.wrapped_signed(fixed_bits)
                                    : raw.wrapped_unsigned(fixed_bits);
            };
            if (binary_op == "+") {
                out = ctvalue_from_exact_int(wrap_raw(l + r), !fixed_signed);
                return true;
            }
            if (binary_op == "-") {
                out = ctvalue_from_exact_int(wrap_raw(l - r), !fixed_signed);
                return true;
            }
            if (binary_op == "*" || binary_op == "/" || binary_op == "%") {
                uint64_t muldiv_bits = 0;
                bool muldiv_signed = false;
                int64_t muldiv_frac = 0;
                if (!fixed_muldiv_meta_supported(fixed_type, muldiv_bits, muldiv_signed, muldiv_frac)) {
                    error_msg = "Fixed-point compound assignment '" + assign_op +
                                "' currently supports only native storage widths up to 32 bits (8/16/32)";
                    return false;
                }
                APInt raw(uint64_t(0));
                if (binary_op == "*") {
                    raw = scale_by_pow2_trunc_zero(l * r, -muldiv_frac);
                } else if (binary_op == "/") {
                    if (!fixed_raw_div(l, r, muldiv_frac, raw)) {
                        error_msg = "Division by zero in compile-time evaluation";
                        return false;
                    }
                } else {
                    if (!fixed_raw_mod(l, r, muldiv_frac, raw)) {
                        error_msg = "Modulo by zero in compile-time evaluation";
                        return false;
                    }
                }
                out = ctvalue_from_exact_int(muldiv_signed ? raw.wrapped_signed(muldiv_bits)
                                                           : raw.wrapped_unsigned(muldiv_bits),
                                             !muldiv_signed);
                return true;
            }
        }

        if (binary_op == "&&" || binary_op == "||") {
            bool lhs_bool = false;
            bool rhs_bool = false;
            if (!cte_scalar_to_bool(lhs_in, lhs_bool) || !cte_scalar_to_bool(rhs_in, rhs_bool)) {
                error_msg = "Unsupported operand types for logical compound assignment";
                return false;
            }
            out = (binary_op == "&&") ? (lhs_bool && rhs_bool) : (lhs_bool || rhs_bool);
            return true;
        }

        auto is_integer_like = [&](const CTValue& v) {
            return ctvalue_is_integer(v) || std::holds_alternative<bool>(v);
        };
        auto integer_unsigned_hint = [&](const CTValue& v) {
            if (std::holds_alternative<uint64_t>(v)) return true;
            if (std::holds_alternative<CTExactInt>(v)) return std::get<CTExactInt>(v).is_unsigned;
            if (std::holds_alternative<bool>(v)) return true;
            return false;
        };

        if (binary_op == "|" || binary_op == "&" || binary_op == "^" ||
            binary_op == "<<" || binary_op == ">>") {
            if (!is_integer_like(lhs_in) || !is_integer_like(rhs_in)) {
                error_msg = "Unsupported operand types for bitwise compound assignment";
                return false;
            }
            APInt l(uint64_t(0));
            APInt r(uint64_t(0));
            bool lu = false, ru = false;
            if (!ctvalue_to_exact_int(lhs_in, l, lu) || !ctvalue_to_exact_int(rhs_in, r, ru)) {
                error_msg = "Unsupported operand types for bitwise compound assignment";
                return false;
            }
            bool use_unsigned = integer_unsigned_hint(lhs_in) || integer_unsigned_hint(rhs_in);
            if (binary_op == "|") out = ctvalue_from_exact_int(l | r, use_unsigned);
            else if (binary_op == "&") out = ctvalue_from_exact_int(l & r, use_unsigned);
            else if (binary_op == "^") out = ctvalue_from_exact_int(l ^ r, use_unsigned);
            else {
                if (r.is_negative()) {
                    error_msg = "Negative shift count in compile-time evaluation";
                    return false;
                }
                if (!r.fits_u64()) {
                    error_msg = "Shift count too large in compile-time evaluation";
                    return false;
                }
                uint64_t shift = r.to_u64();
                if (binary_op == "<<") out = ctvalue_from_exact_int(l << shift, use_unsigned);
                else out = ctvalue_from_exact_int(l >> shift, use_unsigned);
            }
            return true;
        }

        if (std::holds_alternative<double>(lhs_in) || std::holds_alternative<double>(rhs_in)) {
            double l = to_float(lhs_in);
            double r = to_float(rhs_in);
            if (binary_op == "+") out = l + r;
            else if (binary_op == "-") out = l - r;
            else if (binary_op == "*") out = l * r;
            else if (binary_op == "/") {
                if (r == 0.0) {
                    error_msg = "Division by zero in compile-time evaluation";
                    return false;
                }
                out = l / r;
            } else {
                error_msg = "Unsupported compound assignment operator at compile time: " + assign_op;
                return false;
            }
            return true;
        }

        if (is_integer_like(lhs_in) && is_integer_like(rhs_in)) {
            APInt l(uint64_t(0));
            APInt r(uint64_t(0));
            bool lu = false, ru = false;
            if (!ctvalue_to_exact_int(lhs_in, l, lu) || !ctvalue_to_exact_int(rhs_in, r, ru)) {
                error_msg = "Unsupported operand types for compound assignment";
                return false;
            }
            bool use_unsigned = integer_unsigned_hint(lhs_in) || integer_unsigned_hint(rhs_in);
            if (binary_op == "+") out = ctvalue_from_exact_int(l + r, use_unsigned);
            else if (binary_op == "-") out = ctvalue_from_exact_int(l - r, use_unsigned);
            else if (binary_op == "*") out = ctvalue_from_exact_int(l * r, use_unsigned);
            else if (binary_op == "/") {
                if (r.is_zero()) {
                    error_msg = "Division by zero in compile-time evaluation";
                    return false;
                }
                out = ctvalue_from_exact_int(l / r, use_unsigned);
            } else if (binary_op == "%") {
                if (r.is_zero()) {
                    error_msg = "Modulo by zero in compile-time evaluation";
                    return false;
                }
                out = ctvalue_from_exact_int(l % r, use_unsigned);
            } else {
                error_msg = "Unsupported compound assignment operator at compile time: " + assign_op;
                return false;
            }
            return true;
        }

        error_msg = "Unsupported operand types for compound assignment";
        return false;
    };

    CTValue assign_val;
    if (assign_op == "=") {
        if (!evaluate_rhs(assign_val)) {
            return false;
        }
    } else if (assign_op == "&&=" || assign_op == "||=") {
        if (std::holds_alternative<CTUninitialized>(*slot)) {
            error_msg = "Compound assignment reads uninitialized value";
            return false;
        }
        bool lhs_bool = false;
        if (!cte_scalar_to_bool(*slot, lhs_bool)) {
            error_msg = "Unsupported operand types for logical compound assignment";
            return false;
        }
        const bool short_circuit = (assign_op == "&&=") ? !lhs_bool : lhs_bool;
        if (short_circuit) {
            assign_val = lhs_bool;
        } else {
            CTValue rhs_val;
            if (!evaluate_rhs(rhs_val)) {
                return false;
            }
            if (!apply_compound(*slot, rhs_val, assign_val)) {
                return false;
            }
        }
    } else {
        if (std::holds_alternative<CTUninitialized>(*slot)) {
            error_msg = "Compound assignment reads uninitialized value";
            return false;
        }
        CTValue rhs_val;
        if (!evaluate_rhs(rhs_val)) {
            return false;
        }
        if (!apply_compound(*slot, rhs_val, assign_val)) {
            return false;
        }
    }

    TypePtr assignment_type = expr->type;
    if (expr->creates_new_variable && expr->declared_var_type) {
        assignment_type = expr->declared_var_type;
    } else if (!assignment_type && expr->left && expr->left->type) {
        assignment_type = expr->left->type;
    }
    if (assignment_type &&
        !coerce_value_to_type(assign_val, assignment_type, assign_val)) {
        return false;
    }

    CTValue stored_val;
    if (!coerce_value_to_lvalue_type(expr->left, assign_val, stored_val)) {
        return false;
    }
    *slot = copy_ct_value(stored_val);
    ExprPtr root_ident = expr->left;
    while (root_ident &&
           (root_ident->kind == Expr::Kind::Member || root_ident->kind == Expr::Kind::Index)) {
        root_ident = root_ident->operand;
    }
    if (root_ident && root_ident->kind == Expr::Kind::Identifier) {
        uninitialized_locals.erase(root_ident->name);
    }
    result = copy_ct_value(stored_val);
    return true;
}

} // namespace vexel
