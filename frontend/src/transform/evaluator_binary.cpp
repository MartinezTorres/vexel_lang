#include "evaluator.h"
#include "cte_value_utils.h"

namespace vexel {

bool CompileTimeEvaluator::eval_binary(ExprPtr expr, CTValue& result) {
    CTValue left_val, right_val;
    if (!try_evaluate(expr->left, left_val)) return false;

    auto is_int = [&](const CTValue& v) {
        return std::holds_alternative<int64_t>(v) || std::holds_alternative<uint64_t>(v);
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

    if (expr->op == "|" || expr->op == "&" || expr->op == "^" ||
        expr->op == "<<" || expr->op == ">>") {
        if (!is_int(left_val) || !is_int(right_val)) {
            error_msg = "Unsupported operand types for bitwise operation";
            return false;
        }
        bool use_unsigned = std::holds_alternative<uint64_t>(left_val) ||
                            std::holds_alternative<uint64_t>(right_val);
        uint64_t l = std::holds_alternative<uint64_t>(left_val)
            ? std::get<uint64_t>(left_val)
            : (uint64_t)std::get<int64_t>(left_val);
        uint64_t r = std::holds_alternative<uint64_t>(right_val)
            ? std::get<uint64_t>(right_val)
            : (uint64_t)std::get<int64_t>(right_val);

        uint64_t out = 0;
        if (expr->op == "|") out = l | r;
        else if (expr->op == "&") out = l & r;
        else if (expr->op == "^") out = l ^ r;
        else if (expr->op == "<<") out = l << r;
        else if (expr->op == ">>") out = l >> r;

        if (use_unsigned) {
            result = out;
        } else {
            result = (int64_t)out;
        }
        return true;
    }

    if (std::holds_alternative<uint64_t>(left_val) || std::holds_alternative<uint64_t>(right_val)) {
        uint64_t l = std::holds_alternative<uint64_t>(left_val)
            ? std::get<uint64_t>(left_val)
            : (uint64_t)to_int(left_val);
        uint64_t r = std::holds_alternative<uint64_t>(right_val)
            ? std::get<uint64_t>(right_val)
            : (uint64_t)to_int(right_val);

        if (expr->op == "+") result = l + r;
        else if (expr->op == "-") result = l - r;
        else if (expr->op == "*") result = l * r;
        else if (expr->op == "/") {
            if (r == 0) {
                error_msg = "Division by zero in compile-time evaluation";
                return false;
            }
            result = l / r;
        }
        else if (expr->op == "%") {
            if (r == 0) {
                error_msg = "Modulo by zero in compile-time evaluation";
                return false;
            }
            result = l % r;
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

    auto eval_int = [&](int64_t l, int64_t r) -> bool {
        if (expr->op == "+") result = l + r;
        else if (expr->op == "-") result = l - r;
        else if (expr->op == "*") result = l * r;
        else if (expr->op == "/") {
            if (r == 0) {
                error_msg = "Division by zero in compile-time evaluation";
                return false;
            }
            result = l / r;
        }
        else if (expr->op == "%") {
            if (r == 0) {
                error_msg = "Modulo by zero in compile-time evaluation";
                return false;
            }
            result = l % r;
        }
        else if (expr->op == "==") result = (int64_t)(l == r);
        else if (expr->op == "!=") result = (int64_t)(l != r);
        else if (expr->op == "<") result = (int64_t)(l < r);
        else if (expr->op == "<=") result = (int64_t)(l <= r);
        else if (expr->op == ">") result = (int64_t)(l > r);
        else if (expr->op == ">=") result = (int64_t)(l >= r);
        else if (expr->op == "&&") result = (int64_t)(l && r);
        else if (expr->op == "||") result = (int64_t)(l || r);
        else {
            error_msg = "Unsupported binary operator at compile time: " + expr->op;
            return false;
        }
        return true;
    };

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

    // Integer operations
    if (std::holds_alternative<int64_t>(left_val) && std::holds_alternative<int64_t>(right_val)) {
        int64_t l = std::get<int64_t>(left_val);
        int64_t r = std::get<int64_t>(right_val);
        return eval_int(l, r);
    }

    if (std::holds_alternative<bool>(left_val) || std::holds_alternative<bool>(right_val)) {
        int64_t l = to_int(left_val);
        int64_t r = to_int(right_val);
        return eval_int(l, r);
    }

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
