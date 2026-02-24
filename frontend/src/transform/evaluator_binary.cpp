#include "evaluator.h"
#include "cte_value_utils.h"

namespace vexel {

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
