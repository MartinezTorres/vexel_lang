#include "evaluator.h"
#include "cte_value_utils.h"

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
    if (bits_i64 <= 0) return false;
    total_bits = static_cast<uint64_t>(bits_i64);
    is_signed_raw = (type->primitive == PrimitiveType::FixedInt);
    fractional_bits = type->fractional_bits;
    return bits_i64 == 8 || bits_i64 == 16 || bits_i64 == 32 || bits_i64 == 64;
}

bool fixed_any_meta(const TypePtr& type,
                    uint64_t& total_bits,
                    bool& is_signed_raw,
                    int64_t& fractional_bits) {
    if (!is_fixed_primitive_type(type)) return false;
    int64_t bits_i64 = type_bits(type->primitive, type->integer_bits, type->fractional_bits);
    if (bits_i64 <= 0) return false;
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

bool CompileTimeEvaluator::eval_binary(ExprPtr expr, CTValue& result) {
    CTValue left_val, right_val;
    if (!try_evaluate(expr->left, left_val)) return false;

    auto is_integer_like = [&](const CTValue& v) {
        return ctvalue_is_integer(v) || std::holds_alternative<bool>(v);
    };
    auto integer_unsigned_hint = [&](const CTValue& v) {
        if (std::holds_alternative<uint64_t>(v)) return true;
        if (std::holds_alternative<CTExactInt>(v)) return std::get<CTExactInt>(v).is_unsigned;
        if (std::holds_alternative<bool>(v)) return true;
        return false;
    };

    if (expr->op == "&&" || expr->op == "||") {
        bool left_bool = false;
        if (!cte_scalar_to_bool(left_val, left_bool)) {
            error_msg = "Unsupported operand types for logical operation";
            return false;
        }

        if (expr->op == "&&" && !left_bool) {
            result = false;
            return true;
        }
        if (expr->op == "||" && left_bool) {
            result = true;
            return true;
        }

        if (!try_evaluate(expr->right, right_val)) return false;
        bool right_bool = false;
        if (!cte_scalar_to_bool(right_val, right_bool)) {
            error_msg = "Unsupported operand types for logical operation";
            return false;
        }
        result = (expr->op == "&&") ? (left_bool && right_bool) : (left_bool || right_bool);
        return true;
    }

    if (!try_evaluate(expr->right, right_val)) return false;

    if (expr && expr->type && is_fixed_primitive_type(expr->type) &&
        (expr->op == "&" || expr->op == "|" || expr->op == "^" ||
         expr->op == "<<" || expr->op == ">>")) {
        uint64_t fixed_bits = 0;
        bool fixed_signed = false;
        int64_t fixed_frac = 0;
        if (!fixed_any_meta(expr->type, fixed_bits, fixed_signed, fixed_frac)) {
            error_msg = "Unsupported fixed-point type in compile-time evaluation";
            return false;
        }
        if (fixed_signed || fixed_frac != 0) {
            error_msg =
                "Fixed-point compile-time bitwise/shift operators require unsigned fixed-point operands with zero fractional bits";
            return false;
        }
        APInt l(uint64_t(0));
        APInt r(uint64_t(0));
        bool lu = false;
        bool ru = false;
        if (!ctvalue_to_exact_int(left_val, l, lu) || !ctvalue_to_exact_int(right_val, r, ru)) {
            error_msg = "Unsupported operand types for fixed-point binary operation";
            return false;
        }
        auto wrap_raw = [&](const APInt& raw) { return raw.wrapped_unsigned(fixed_bits); };
        l = wrap_raw(l);
        r = wrap_raw(r);
        if (expr->op == "&") {
            result = ctvalue_from_exact_int(wrap_raw(l & r), true);
            return true;
        }
        if (expr->op == "|") {
            result = ctvalue_from_exact_int(wrap_raw(l | r), true);
            return true;
        }
        if (expr->op == "^") {
            result = ctvalue_from_exact_int(wrap_raw(l ^ r), true);
            return true;
        }
        if (r.is_negative()) {
            error_msg = "Negative shift count in compile-time evaluation";
            return false;
        }
        if (!r.fits_u64()) {
            error_msg = "Shift count too large in compile-time evaluation";
            return false;
        }
        uint64_t shift = r.to_u64();
        if (expr->op == "<<") {
            result = ctvalue_from_exact_int(wrap_raw(l << shift), true);
        } else {
            result = ctvalue_from_exact_int(wrap_raw(l >> shift), true);
        }
        return true;
    }

    uint64_t fixed_bits = 0;
    bool fixed_signed = false;
    int64_t fixed_frac = 0;
    if (expr && is_fixed_primitive_type(expr->type) &&
        fixed_native_meta(expr->type, fixed_bits, fixed_signed, fixed_frac)) {
        APInt l(uint64_t(0));
        APInt r(uint64_t(0));
        bool lu = false;
        bool ru = false;
        if (!ctvalue_to_exact_int(left_val, l, lu) || !ctvalue_to_exact_int(right_val, r, ru)) {
            error_msg = "Unsupported operand types for fixed-point binary operation";
            return false;
        }
        auto wrap_raw = [&](const APInt& raw) {
            return fixed_signed ? raw.wrapped_signed(fixed_bits)
                                : raw.wrapped_unsigned(fixed_bits);
        };
        l = wrap_raw(l);
        r = wrap_raw(r);
        if (expr->op == "+") {
            result = ctvalue_from_exact_int(wrap_raw(l + r), !fixed_signed);
            return true;
        }
        if (expr->op == "-") {
            result = ctvalue_from_exact_int(wrap_raw(l - r), !fixed_signed);
            return true;
        }
        if (expr->op == "==" || expr->op == "!=" || expr->op == "<" ||
            expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
            if (expr->op == "==") result = (int64_t)(l == r);
            else if (expr->op == "!=") result = (int64_t)(l != r);
            else if (expr->op == "<") result = (int64_t)(l < r);
            else if (expr->op == "<=") result = (int64_t)(l <= r);
            else if (expr->op == ">") result = (int64_t)(l > r);
            else result = (int64_t)(l >= r);
            return true;
        }
        if ((expr->op == "*" || expr->op == "/" || expr->op == "%")) {
            uint64_t muldiv_bits = 0;
            bool muldiv_signed = false;
            int64_t muldiv_frac = 0;
            if (!fixed_muldiv_meta_supported(expr->type, muldiv_bits, muldiv_signed, muldiv_frac)) {
                error_msg = "Fixed-point compile-time " + expr->op +
                            " currently supports only native storage widths up to 32 bits (8/16/32)";
                return false;
            }
            APInt raw(uint64_t(0));
            if (expr->op == "*") {
                raw = scale_by_pow2_trunc_zero(l * r, -muldiv_frac);
            } else if (expr->op == "/") {
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
            result = ctvalue_from_exact_int(muldiv_signed ? raw.wrapped_signed(muldiv_bits)
                                                          : raw.wrapped_unsigned(muldiv_bits),
                                            !muldiv_signed);
            return true;
        }
        error_msg = "Unsupported fixed-point binary operator at compile time: " + expr->op;
        return false;
    }

    const bool left_has_float = std::holds_alternative<double>(left_val);
    const bool right_has_float = std::holds_alternative<double>(right_val);
    if (!left_has_float && !right_has_float &&
        is_integer_like(left_val) && is_integer_like(right_val)) {
        APInt l(uint64_t(0));
        APInt r(uint64_t(0));
        bool l_unsigned = false;
        bool r_unsigned = false;
        if (!ctvalue_to_exact_int(left_val, l, l_unsigned) ||
            !ctvalue_to_exact_int(right_val, r, r_unsigned)) {
            error_msg = "Unsupported operand types for binary operation";
            return false;
        }
        const bool use_unsigned = integer_unsigned_hint(left_val) || integer_unsigned_hint(right_val);

        if (expr->op == "|" || expr->op == "&" || expr->op == "^" ||
            expr->op == "<<" || expr->op == ">>") {
            if (expr->op == "|") result = ctvalue_from_exact_int(l | r, use_unsigned);
            else if (expr->op == "&") result = ctvalue_from_exact_int(l & r, use_unsigned);
            else if (expr->op == "^") result = ctvalue_from_exact_int(l ^ r, use_unsigned);
            else {
                if (r.is_negative()) {
                    error_msg = "Negative shift count in compile-time evaluation";
                    return false;
                }
                if (!r.fits_u64()) {
                    error_msg = "Shift count too large in compile-time evaluation";
                    return false;
                }
                const uint64_t shift = r.to_u64();
                if (expr->op == "<<") result = ctvalue_from_exact_int(l << shift, use_unsigned);
                else result = ctvalue_from_exact_int(l >> shift, use_unsigned);
            }
            return true;
        }

        if (expr->op == "+") result = ctvalue_from_exact_int(l + r, use_unsigned);
        else if (expr->op == "-") result = ctvalue_from_exact_int(l - r, use_unsigned);
        else if (expr->op == "*") result = ctvalue_from_exact_int(l * r, use_unsigned);
        else if (expr->op == "/") {
            if (r.is_zero()) {
                error_msg = "Division by zero in compile-time evaluation";
                return false;
            }
            result = ctvalue_from_exact_int(l / r, use_unsigned);
        }
        else if (expr->op == "%") {
            if (r.is_zero()) {
                error_msg = "Modulo by zero in compile-time evaluation";
                return false;
            }
            result = ctvalue_from_exact_int(l % r, use_unsigned);
        }
        else if (expr->op == "==") result = (int64_t)(l == r);
        else if (expr->op == "!=") result = (int64_t)(l != r);
        else if (expr->op == "<") result = (int64_t)(l < r);
        else if (expr->op == "<=") result = (int64_t)(l <= r);
        else if (expr->op == ">") result = (int64_t)(l > r);
        else if (expr->op == ">=") result = (int64_t)(l >= r);
        else {
            error_msg = "Unsupported binary operator at compile time: " + expr->op;
            return false;
        }
        return true;
    }

    if (std::holds_alternative<std::string>(left_val) &&
        std::holds_alternative<std::string>(right_val)) {
        const auto& l = std::get<std::string>(left_val);
        const auto& r = std::get<std::string>(right_val);
        if (expr->op == "==") result = (int64_t)(l == r);
        else if (expr->op == "!=") result = (int64_t)(l != r);
        else if (expr->op == "<") result = (int64_t)(l < r);
        else if (expr->op == "<=") result = (int64_t)(l <= r);
        else if (expr->op == ">") result = (int64_t)(l > r);
        else if (expr->op == ">=") result = (int64_t)(l >= r);
        else {
            error_msg = "Unsupported binary operator for strings at compile time: " + expr->op;
            return false;
        }
        return true;
    }

    auto eval_float = [&](double l, double r) -> bool {
        if (expr->op == "+") result = l + r;
        else if (expr->op == "-") result = l - r;
        else if (expr->op == "*") result = l * r;
        else if (expr->op == "/") {
            if (r == 0.0) {
                error_msg = "Division by zero in compile-time evaluation";
                return false;
            }
            result = l / r;
        }
        else if (expr->op == "==") result = (int64_t)(l == r);
        else if (expr->op == "!=") result = (int64_t)(l != r);
        else if (expr->op == "<") result = (int64_t)(l < r);
        else if (expr->op == "<=") result = (int64_t)(l <= r);
        else if (expr->op == ">") result = (int64_t)(l > r);
        else if (expr->op == ">=") result = (int64_t)(l >= r);
        else {
            error_msg = "Unsupported binary operator at compile time: " + expr->op;
            return false;
        }
        return true;
    };

    // Floating-point operations
    if (std::holds_alternative<double>(left_val) || std::holds_alternative<double>(right_val)) {
        double l = to_float(left_val);
        double r = to_float(right_val);
        return eval_float(l, r);
    }

    error_msg = "Unsupported operand types for binary operation";
    return false;
}

} // namespace vexel
