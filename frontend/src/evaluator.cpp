#include "evaluator.h"
#include "constants.h"
#include <algorithm>
#include <functional>

namespace vexel {

namespace {
struct EvalBreak {};
struct EvalContinue {};
struct EvalReturn {
    CTValue value;
};

CTValue clone_value(const CTValue& value) {
    if (std::holds_alternative<std::shared_ptr<CTComposite>>(value)) {
        auto src = std::get<std::shared_ptr<CTComposite>>(value);
        if (!src) {
            return std::shared_ptr<CTComposite>();
        }
        auto dst = std::make_shared<CTComposite>();
        dst->type_name = src->type_name;
        for (const auto& entry : src->fields) {
            dst->fields[entry.first] = clone_value(entry.second);
        }
        return dst;
    }
    if (std::holds_alternative<std::shared_ptr<CTArray>>(value)) {
        auto src = std::get<std::shared_ptr<CTArray>>(value);
        if (!src) {
            return std::shared_ptr<CTArray>();
        }
        auto dst = std::make_shared<CTArray>();
        dst->elements.reserve(src->elements.size());
        for (const auto& elem : src->elements) {
            dst->elements.push_back(clone_value(elem));
        }
        return dst;
    }
    return value;
}
} // namespace

bool CompileTimeEvaluator::try_evaluate(ExprPtr expr, CTValue& result) {
    if (!expr) {
        error_msg = "Null expression";
        return false;
    }

    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        error_msg = "Recursion depth limit exceeded in compile-time evaluation";
        return false;
    }

    struct DepthGuard {
        int& depth;
        explicit DepthGuard(int& d) : depth(d) { depth++; }
        ~DepthGuard() { depth--; }
    } guard(recursion_depth);
    bool success = false;

    try {
        switch (expr->kind) {
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
            success = eval_literal(expr, result);
            break;
        case Expr::Kind::Binary:
            success = eval_binary(expr, result);
            break;
        case Expr::Kind::Unary:
            success = eval_unary(expr, result);
            break;
        case Expr::Kind::Call:
            success = eval_call(expr, result);
            break;
        case Expr::Kind::Identifier:
            success = eval_identifier(expr, result);
            break;
        case Expr::Kind::Member:
            success = eval_member_access(expr, result);
            break;
        case Expr::Kind::Conditional:
            success = eval_conditional(expr, result);
            break;
        case Expr::Kind::Cast:
            success = eval_cast(expr, result);
            break;
        case Expr::Kind::Assignment:
            success = eval_assignment(expr, result);
            break;
        case Expr::Kind::ArrayLiteral:
            success = eval_array_literal(expr, result);
            break;
        case Expr::Kind::TupleLiteral:
            success = eval_tuple_literal(expr, result);
            break;
        case Expr::Kind::Range:
            success = eval_range(expr, result);
            break;
        case Expr::Kind::Index:
            success = eval_index(expr, result);
            break;
        case Expr::Kind::Iteration:
            success = eval_iteration(expr, result);
            break;
        case Expr::Kind::Repeat:
            success = eval_repeat(expr, result);
            break;
        case Expr::Kind::Length:
            success = eval_length(expr, result);
            break;
        case Expr::Kind::Block:
            // Evaluate block - process statements, then evaluate result
            {
                // Save current constants
                auto saved_constants = constants;
                auto saved_uninitialized = uninitialized_locals;
                std::unordered_set<std::string> locals_declared;

                std::function<bool(StmtPtr)> eval_stmt;
                eval_stmt = [&](StmtPtr stmt) -> bool {
                    if (!stmt) return true;
                    switch (stmt->kind) {
                        case Stmt::Kind::Expr: {
                            if (!stmt->expr) return true;
                            if (stmt->expr->kind == Expr::Kind::Assignment &&
                                stmt->expr->left && stmt->expr->left->kind == Expr::Kind::Identifier) {
                                if (stmt->expr->creates_new_variable) {
                                    locals_declared.insert(stmt->expr->left->name);
                                }
                                CTValue stmt_val;
                                if (!eval_assignment(stmt->expr, stmt_val)) {
                                    return false;
                                }
                                return true;
                            }
                            CTValue stmt_val;
                            return try_evaluate(stmt->expr, stmt_val);
                        }
                        case Stmt::Kind::VarDecl: {
                            if (stmt->var_init) {
                                CTValue init_val;
                                if (!try_evaluate(stmt->var_init, init_val)) {
                                    return false;
                                }
                                constants[stmt->var_name] = clone_value(init_val);
                                uninitialized_locals.erase(stmt->var_name);
                                locals_declared.insert(stmt->var_name);
                            } else {
                                uninitialized_locals.insert(stmt->var_name);
                                locals_declared.insert(stmt->var_name);
                            }
                            return true;
                        }
                        case Stmt::Kind::ConditionalStmt: {
                            CTValue cond_val;
                            if (!try_evaluate(stmt->condition, cond_val)) {
                                return false;
                            }
                            bool is_true = false;
                            if (std::holds_alternative<int64_t>(cond_val)) {
                                is_true = std::get<int64_t>(cond_val) != 0;
                            } else if (std::holds_alternative<uint64_t>(cond_val)) {
                                is_true = std::get<uint64_t>(cond_val) != 0;
                            } else if (std::holds_alternative<bool>(cond_val)) {
                                is_true = std::get<bool>(cond_val);
                            } else if (std::holds_alternative<double>(cond_val)) {
                                is_true = std::get<double>(cond_val) != 0.0;
                            } else {
                                error_msg = "Conditional expression condition must be a scalar value";
                                return false;
                            }
                            if (is_true && stmt->true_stmt) {
                                return eval_stmt(stmt->true_stmt);
                            }
                            return true;
                        }
                        case Stmt::Kind::Return: {
                            if (!stmt->return_expr) {
                                error_msg = "Return statement requires an expression at compile time";
                                return false;
                            }
                            CTValue ret_val;
                            if (!try_evaluate(stmt->return_expr, ret_val)) {
                                return false;
                            }
                            throw EvalReturn{clone_value(ret_val)};
                        }
                        case Stmt::Kind::Break:
                            throw EvalBreak{};
                        case Stmt::Kind::Continue:
                            throw EvalContinue{};
                        default:
                            error_msg = "Statement type not supported at compile time";
                            return false;
                    }
                };

                for (const auto& stmt : expr->statements) {
                    if (!eval_stmt(stmt)) {
                        constants = saved_constants;
                        uninitialized_locals = saved_uninitialized;
                        return false;
                    }
                }

                // Evaluate the result expression
                if (expr->result_expr) {
                    success = try_evaluate(expr->result_expr, result);
                    if (!success) {
                        constants = saved_constants;
                        uninitialized_locals = saved_uninitialized;
                        return false;
                    }
                } else {
                    result = (int64_t)0;
                    success = true;
                }

                // Remove locals declared in this block, keep updates to outer variables
                for (const auto& name : locals_declared) {
                    constants.erase(name);
                    uninitialized_locals.erase(name);
                }
            }
            break;
        default:
            error_msg = "Expression kind not supported at compile time";
            success = false;
            break;
        }
    } catch (const EvalBreak&) {
        if (loop_depth > 0) {
            throw;
        }
        error_msg = "Break used outside of loop in compile-time evaluation";
        success = false;
    } catch (const EvalContinue&) {
        if (loop_depth > 0) {
            throw;
        }
        error_msg = "Continue used outside of loop in compile-time evaluation";
        success = false;
    } catch (const EvalReturn&) {
        if (return_depth > 0) {
            throw;
        }
        error_msg = "Return used outside of function in compile-time evaluation";
        success = false;
    } catch (const CompileError& e) {
        error_msg = e.what();
        success = false;
    }

    return success;
}

bool CompileTimeEvaluator::eval_literal(ExprPtr expr, CTValue& result) {
    switch (expr->kind) {
        case Expr::Kind::IntLiteral:
            if (expr->literal_is_unsigned) {
                result = (uint64_t)expr->uint_val;
            } else {
                result = (int64_t)expr->uint_val;
            }
            return true;
        case Expr::Kind::FloatLiteral:
            result = expr->float_val;
            return true;
        case Expr::Kind::CharLiteral:
            result = (int64_t)(uint8_t)expr->uint_val;
            return true;
        case Expr::Kind::StringLiteral:
            result = expr->string_val;
            return true;
        default:
            error_msg = "Not a literal";
            return false;
    }
}

bool CompileTimeEvaluator::eval_binary(ExprPtr expr, CTValue& result) {
    CTValue left_val, right_val;
    if (!try_evaluate(expr->left, left_val)) return false;

    auto is_int = [&](const CTValue& v) {
        return std::holds_alternative<int64_t>(v) || std::holds_alternative<uint64_t>(v);
    };

    auto to_bool = [&](const CTValue& v, bool& out) -> bool {
        if (std::holds_alternative<int64_t>(v)) {
            out = std::get<int64_t>(v) != 0;
            return true;
        }
        if (std::holds_alternative<uint64_t>(v)) {
            out = std::get<uint64_t>(v) != 0;
            return true;
        }
        if (std::holds_alternative<bool>(v)) {
            out = std::get<bool>(v);
            return true;
        }
        if (std::holds_alternative<double>(v)) {
            out = std::get<double>(v) != 0.0;
            return true;
        }
        return false;
    };

    if (expr->op == "&&" || expr->op == "||") {
        bool left_bool = false;
        if (!to_bool(left_val, left_bool)) {
            error_msg = "Unsupported operand types for logical operation";
            return false;
        }

        if (expr->op == "&&" && !left_bool) {
            result = (int64_t)0;
            return true;
        }
        if (expr->op == "||" && left_bool) {
            result = (int64_t)1;
            return true;
        }

        if (!try_evaluate(expr->right, right_val)) return false;
        bool right_bool = false;
        if (!to_bool(right_val, right_bool)) {
            error_msg = "Unsupported operand types for logical operation";
            return false;
        }
        result = (int64_t)((expr->op == "&&") ? (left_bool && right_bool) : (left_bool || right_bool));
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

bool CompileTimeEvaluator::eval_unary(ExprPtr expr, CTValue& result) {
    CTValue operand_val;
    if (!try_evaluate(expr->operand, operand_val)) return false;

    if (expr->op == "~") {
        if (std::holds_alternative<uint64_t>(operand_val)) {
            result = ~std::get<uint64_t>(operand_val);
            return true;
        }
        if (std::holds_alternative<int64_t>(operand_val)) {
            result = (int64_t)~std::get<int64_t>(operand_val);
            return true;
        }
        error_msg = "Unsupported operand type for bitwise not";
        return false;
    }

    if (std::holds_alternative<int64_t>(operand_val)) {
        int64_t v = std::get<int64_t>(operand_val);
        if (expr->op == "-") result = -v;
        else if (expr->op == "!") result = (int64_t)!v;
        else {
            error_msg = "Unsupported unary operator: " + expr->op;
            return false;
        }
        return true;
    }

    if (std::holds_alternative<double>(operand_val)) {
        double v = std::get<double>(operand_val);
        if (expr->op == "-") result = -v;
        else if (expr->op == "!") result = (int64_t)!v;
        else {
            error_msg = "Unsupported unary operator: " + expr->op;
            return false;
        }
        return true;
    }

    if (std::holds_alternative<bool>(operand_val)) {
        bool v = std::get<bool>(operand_val);
        if (expr->op == "!") {
            result = (int64_t)!v;
            return true;
        }
    }

    error_msg = "Unsupported operand type for unary operation";
    return false;
}

bool CompileTimeEvaluator::eval_call(ExprPtr expr, CTValue& result) {
    // Look up function or type
    if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
        error_msg = "Cannot evaluate non-identifier function calls at compile time";
        return false;
    }

    std::string func_name = expr->operand->name;
    Symbol* sym = nullptr;
    if (type_checker) {
        sym = type_checker->binding_for(expr->operand.get());
    }
    if (!sym && type_checker && type_checker->get_scope()) {
        sym = type_checker->get_scope()->lookup(func_name);
    }

    if (!sym) {
        error_msg = "Symbol not found: " + func_name;
        return false;
    }

    // Check if this is a type constructor call
    if (sym->kind == Symbol::Kind::Type) {
        return eval_type_constructor(expr, result);
    }

    if (sym->kind != Symbol::Kind::Function || !sym->declaration) {
        error_msg = "Not a function: " + func_name;
        return false;
    }

    StmtPtr func = sym->declaration;

    // Check if function is pure enough for compile-time evaluation
    if (func->is_external) {
        error_msg = "External functions cannot be evaluated at compile time";
        return false;
    }

    // Check for mutable global access
    std::string impurity_reason;
    if (!is_pure_for_compile_time(func, impurity_reason)) {
        error_msg = "Function is not pure for compile-time evaluation: " + impurity_reason;
        return false;
    }

    // Evaluate arguments
    std::unordered_map<std::string, CTValue> saved_constants = constants;
    if (!func->ref_params.empty()) {
        if (expr->receivers.size() != func->ref_params.size()) {
            error_msg = "Receiver count mismatch in compile-time evaluation";
            return false;
        }
        for (size_t i = 0; i < func->ref_params.size(); i++) {
            CTValue rec_val;
            if (!try_evaluate(expr->receivers[i], rec_val)) {
                constants = saved_constants;
                return false;
            }
            constants[func->ref_params[i]] = clone_value(rec_val);
        }
    }
    for (size_t i = 0; i < expr->args.size(); i++) {
        CTValue arg_val;
        if (!try_evaluate(expr->args[i], arg_val)) {
            constants = saved_constants;
            return false;
        }
        constants[func->params[i].name] = clone_value(arg_val);
    }

    // Evaluate function body
    if (!func->body) {
        error_msg = "Function has no body";
        constants = saved_constants;
        return false;
    }

    push_ref_params(func);
    struct ReturnGuard {
        int& depth;
        explicit ReturnGuard(int& d) : depth(d) { depth++; }
        ~ReturnGuard() { depth--; }
    } return_guard(return_depth);

    bool success = false;
    try {
        success = try_evaluate(func->body, result);
    } catch (const EvalReturn& ret) {
        result = clone_value(ret.value);
        success = true;
    }
    pop_ref_params();
    constants = saved_constants;
    return success;
}

bool CompileTimeEvaluator::eval_identifier(ExprPtr expr, CTValue& result) {
    auto it = constants.find(expr->name);
    if (it != constants.end()) {
        result = it->second;
        return true;
    }

    if (uninitialized_locals.count(expr->name)) {
        error_msg = "uninitialized variable accessed at compile time: " + expr->name;
        return false;
    }

    // Try to look up global constant
    Symbol* sym = type_checker ? type_checker->binding_for(expr.get()) : nullptr;
    if (!sym && type_checker && type_checker->get_scope()) {
        sym = type_checker->get_scope()->lookup(expr->name);
    }
    if (sym && sym->kind == Symbol::Kind::Constant && sym->declaration && sym->declaration->var_init) {
        return try_evaluate(sym->declaration->var_init, result);
    }

    error_msg = "Identifier not found or not a compile-time constant: " + expr->name;
    return false;
}

int64_t CompileTimeEvaluator::to_int(const CTValue& v) {
    if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
    if (std::holds_alternative<uint64_t>(v)) return (int64_t)std::get<uint64_t>(v);
    if (std::holds_alternative<double>(v)) return (int64_t)std::get<double>(v);
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? 1 : 0;
    throw CompileError("Cannot convert value to integer in compile-time evaluation", SourceLocation());
}

double CompileTimeEvaluator::to_float(const CTValue& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    if (std::holds_alternative<int64_t>(v)) return (double)std::get<int64_t>(v);
    if (std::holds_alternative<uint64_t>(v)) return (double)std::get<uint64_t>(v);
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? 1.0 : 0.0;
    throw CompileError("Cannot convert value to float in compile-time evaluation", SourceLocation());
}

bool CompileTimeEvaluator::eval_type_constructor(ExprPtr expr, CTValue& result) {
    // expr is a Call node where operand is a type name
    if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
        error_msg = "Type constructor must have identifier operand";
        return false;
    }

    std::string type_name = expr->operand->name;
    Symbol* sym = nullptr;
    if (type_checker) {
        sym = type_checker->binding_for(expr->operand.get());
    }
    if (!sym && type_checker && type_checker->get_scope()) {
        sym = type_checker->get_scope()->lookup(type_name);
    }

    if (!sym || sym->kind != Symbol::Kind::Type || !sym->declaration) {
        error_msg = "Type not found: " + type_name;
        return false;
    }

    // Get the type declaration to know the field names
    StmtPtr type_decl = sym->declaration;
    if (type_decl->kind != Stmt::Kind::TypeDecl) {
        error_msg = "Not a type declaration";
        return false;
    }

    // Evaluate all arguments
    if (expr->args.size() != type_decl->fields.size()) {
        error_msg = "Type constructor argument count mismatch";
        return false;
    }

    auto composite = std::make_shared<CTComposite>();
    composite->type_name = type_name;

    for (size_t i = 0; i < expr->args.size(); i++) {
        CTValue arg_val;

        // Try to evaluate the argument
        if (!try_evaluate(expr->args[i], arg_val)) {
            // If we can't evaluate it (e.g., it's an array or identifier pointing to array),
            // store a placeholder value of 0 for pointer/array types
            // This allows us to still track scalar fields like len, cap
            std::string field_name = type_decl->fields[i].name;
            composite->fields[field_name] = (int64_t)0;
            continue;
        }

        // Store the field value
        std::string field_name = type_decl->fields[i].name;

        // Convert CTValue to the field storage type
        composite->fields[field_name] = clone_value(arg_val);
    }

    result = composite;
    return true;
}

bool CompileTimeEvaluator::eval_member_access(ExprPtr expr, CTValue& result) {
    // Evaluate the object expression
    CTValue obj_val;
    if (!try_evaluate(expr->operand, obj_val)) {
        return false;
    }

    // Check if it's a composite value
    if (!std::holds_alternative<std::shared_ptr<CTComposite>>(obj_val)) {
        error_msg = "Member access on non-composite value";
        return false;
    }

    std::shared_ptr<CTComposite> composite = std::get<std::shared_ptr<CTComposite>>(obj_val);
    if (!composite) {
        error_msg = "Member access on null composite value";
        return false;
    }

    // Look up the field
    auto it = composite->fields.find(expr->name);
    if (it == composite->fields.end()) {
        error_msg = "Field not found: " + expr->name;
        return false;
    }

    // Convert the field value to CTValue
    result = it->second;

    return true;
}

bool CompileTimeEvaluator::eval_conditional(ExprPtr expr, CTValue& result) {
    // Evaluate condition
    CTValue cond_val;
    if (!try_evaluate(expr->condition, cond_val)) {
        return false;
    }

    // Convert condition to boolean
    bool is_true = false;
    if (std::holds_alternative<int64_t>(cond_val)) {
        is_true = std::get<int64_t>(cond_val) != 0;
    } else if (std::holds_alternative<uint64_t>(cond_val)) {
        is_true = std::get<uint64_t>(cond_val) != 0;
    } else if (std::holds_alternative<bool>(cond_val)) {
        is_true = std::get<bool>(cond_val);
    } else if (std::holds_alternative<double>(cond_val)) {
        is_true = std::get<double>(cond_val) != 0.0;
    } else {
        error_msg = "Conditional expression condition must be a scalar value";
        return false;
    }

    // Evaluate the appropriate branch
    if (is_true) {
        return try_evaluate(expr->true_expr, result);
    } else {
        return try_evaluate(expr->false_expr, result);
    }
}

bool CompileTimeEvaluator::eval_cast(ExprPtr expr, CTValue& result) {
    // Evaluate the operand
    CTValue operand_val;
    if (!try_evaluate(expr->operand, operand_val)) {
        return false;
    }

    // Get target type
    TypePtr target_type = expr->target_type;
    if (!target_type) {
        error_msg = "Cast expression has no target type";
        return false;
    }

    // Primitive to byte array conversion (big-endian order)
    if (target_type->kind == Type::Kind::Array &&
        target_type->element_type &&
        target_type->element_type->kind == Type::Kind::Primitive &&
        target_type->element_type->primitive == PrimitiveType::U8 &&
        expr->operand && expr->operand->type &&
        expr->operand->type->kind == Type::Kind::Primitive &&
        !is_float(expr->operand->type->primitive)) {

        if (!target_type->array_size) {
            error_msg = "Array length must be a compile-time constant";
            return false;
        }
        CTValue size_val;
        if (!try_evaluate(target_type->array_size, size_val)) {
            error_msg = "Array length must be a compile-time constant";
            return false;
        }
        int64_t length = std::holds_alternative<int64_t>(size_val)
            ? std::get<int64_t>(size_val)
            : static_cast<int64_t>(std::get<uint64_t>(size_val));
        if (length < 0) {
            error_msg = "Array length cannot be negative";
            return false;
        }

        int bits = type_bits(expr->operand->type->primitive);
        if (bits < 0 || bits / 8 != length) {
            error_msg = "Array length/type size mismatch in cast";
            return false;
        }

        uint64_t value_bits = 0;
        if (std::holds_alternative<uint64_t>(operand_val)) {
            value_bits = std::get<uint64_t>(operand_val);
        } else if (std::holds_alternative<int64_t>(operand_val)) {
            value_bits = static_cast<uint64_t>(std::get<int64_t>(operand_val));
        } else if (std::holds_alternative<bool>(operand_val)) {
            value_bits = std::get<bool>(operand_val) ? 1u : 0u;
        } else {
            error_msg = "Unsupported operand type for byte array cast";
            return false;
        }

        if (bits < 64) {
            uint64_t mask = (bits == 64) ? ~0ull : ((1ull << bits) - 1ull);
            value_bits &= mask;
        }

        auto array = std::make_shared<CTArray>();
        array->elements.reserve(static_cast<size_t>(length));
        for (int64_t i = 0; i < length; ++i) {
            int64_t shift = (length - 1 - i) * 8;
            uint64_t byte = (value_bits >> shift) & 0xFFu;
            array->elements.push_back(byte);
        }
        result = array;
        return true;
    }

    // Pack boolean arrays into unsigned integers
    if (target_type->kind == Type::Kind::Primitive &&
        is_unsigned_int(target_type->primitive) &&
        expr->operand && expr->operand->type &&
        expr->operand->type->kind == Type::Kind::Array &&
        expr->operand->type->element_type &&
        expr->operand->type->element_type->kind == Type::Kind::Primitive &&
        expr->operand->type->element_type->primitive == PrimitiveType::Bool) {

        int64_t length = 0;
        if (std::holds_alternative<std::shared_ptr<CTArray>>(operand_val)) {
            auto array = std::get<std::shared_ptr<CTArray>>(operand_val);
            if (!array) {
                error_msg = "Cast from null boolean array";
                return false;
            }
            length = static_cast<int64_t>(array->elements.size());
        } else if (expr->operand->type->array_size) {
            CTValue size_val;
            if (!try_evaluate(expr->operand->type->array_size, size_val)) {
                error_msg = "Array length must be a compile-time constant";
                return false;
            }
            length = std::holds_alternative<int64_t>(size_val)
                ? std::get<int64_t>(size_val)
                : static_cast<int64_t>(std::get<uint64_t>(size_val));
        }

        if (length <= 0) {
            error_msg = "Boolean array size must be non-zero";
            return false;
        }
        if (length != type_bits(target_type->primitive)) {
            error_msg = "Boolean array size mismatch for cast to #" +
                        primitive_name(target_type->primitive);
            return false;
        }

        uint64_t out = 0;
        auto to_bit = [&](const CTValue& v, bool& bit) -> bool {
            if (std::holds_alternative<bool>(v)) {
                bit = std::get<bool>(v);
                return true;
            }
            if (std::holds_alternative<int64_t>(v)) {
                bit = std::get<int64_t>(v) != 0;
                return true;
            }
            if (std::holds_alternative<uint64_t>(v)) {
                bit = std::get<uint64_t>(v) != 0;
                return true;
            }
            return false;
        };

        if (!std::holds_alternative<std::shared_ptr<CTArray>>(operand_val)) {
            error_msg = "Boolean array cast requires compile-time array";
            return false;
        }
        auto array = std::get<std::shared_ptr<CTArray>>(operand_val);
        if (!array) {
            error_msg = "Cast from null boolean array";
            return false;
        }
        if (static_cast<int64_t>(array->elements.size()) != length) {
            error_msg = "Boolean array size mismatch for cast to #" +
                        primitive_name(target_type->primitive);
            return false;
        }
        for (int64_t i = 0; i < length; ++i) {
            bool bit = false;
            if (!to_bit(array->elements[static_cast<size_t>(i)], bit)) {
                error_msg = "Boolean array contains non-boolean value";
                return false;
            }
            if (bit) {
                int64_t shift = (length - 1 - i);
                out |= (uint64_t)(1u) << shift;
            }
        }
        result = out;
        return true;
    }

    // Only handle primitive types at compile time
    if (target_type->kind != Type::Kind::Primitive) {
        error_msg = "Can only cast to primitive types at compile time";
        return false;
    }

    // Perform the cast based on target primitive type
    if (target_type->primitive == PrimitiveType::I8 || target_type->primitive == PrimitiveType::I16 ||
        target_type->primitive == PrimitiveType::I32 || target_type->primitive == PrimitiveType::I64) {
        // Cast to signed integer
        result = to_int(operand_val);
        return true;
    } else if (target_type->primitive == PrimitiveType::U8 || target_type->primitive == PrimitiveType::U16 ||
               target_type->primitive == PrimitiveType::U32 || target_type->primitive == PrimitiveType::U64) {
        // Cast to unsigned integer
        result = (uint64_t)to_int(operand_val);
        return true;
    } else if (target_type->primitive == PrimitiveType::F32 || target_type->primitive == PrimitiveType::F64) {
        // Cast to float
        result = to_float(operand_val);
        return true;
    } else if (target_type->primitive == PrimitiveType::Bool) {
        // Cast to bool
        result = to_int(operand_val) != 0;
        return true;
    }

    error_msg = "Unsupported cast type at compile time";
    return false;
}

bool CompileTimeEvaluator::eval_assignment(ExprPtr expr, CTValue& result) {
    // Evaluate the right-hand side
    CTValue rhs_val;
    if (!try_evaluate(expr->right, rhs_val)) {
        return false;
    }

    auto is_local = [&](const std::string& name) {
        return constants.count(name) > 0 || uninitialized_locals.count(name) > 0;
    };

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
        Symbol* sym = type_checker ? type_checker->get_scope()->lookup(base) : nullptr;
        if (sym && !sym->is_mutable && !is_local(base)) {
            error_msg = "Cannot assign to immutable constant: " + base;
            return false;
        }
        if (sym && sym->kind == Symbol::Kind::Variable && sym->is_mutable && !is_local(base)) {
            error_msg = "Cannot modify mutable globals at compile time: " + base;
            return false;
        }
    }

    std::function<bool(ExprPtr, const CTValue&)> assign_lvalue;
    std::function<bool(ExprPtr, CTValue&)> fetch_lvalue;

    fetch_lvalue = [&](ExprPtr target, CTValue& out) -> bool {
        if (!target) return false;
        switch (target->kind) {
            case Expr::Kind::Identifier: {
                auto it = constants.find(target->name);
                if (it == constants.end()) {
                    error_msg = "Identifier not found or not a compile-time constant: " + target->name;
                    return false;
                }
                out = it->second;
                return true;
            }
            case Expr::Kind::Member: {
                CTValue base_val;
                if (!fetch_lvalue(target->operand, base_val)) return false;
                if (!std::holds_alternative<std::shared_ptr<CTComposite>>(base_val)) {
                    error_msg = "Member access on non-composite value";
                    return false;
                }
                auto comp = std::get<std::shared_ptr<CTComposite>>(base_val);
                if (!comp) {
                    error_msg = "Member access on null composite value";
                    return false;
                }
                auto it = comp->fields.find(target->name);
                if (it == comp->fields.end()) {
                    error_msg = "Field not found: " + target->name;
                    return false;
                }
                out = it->second;
                return true;
            }
            case Expr::Kind::Index: {
                CTValue base_val;
                if (!fetch_lvalue(target->operand, base_val)) return false;
                CTValue index_val;
                if (!try_evaluate(target->args[0], index_val)) return false;
                if (!std::holds_alternative<int64_t>(index_val) &&
                    !std::holds_alternative<uint64_t>(index_val)) {
                    error_msg = "Index must be an integer constant";
                    return false;
                }
                int64_t idx = std::holds_alternative<int64_t>(index_val)
                    ? std::get<int64_t>(index_val)
                    : static_cast<int64_t>(std::get<uint64_t>(index_val));
                if (idx < 0) {
                    error_msg = "Index cannot be negative";
                    return false;
                }
                if (!std::holds_alternative<std::shared_ptr<CTArray>>(base_val)) {
                    error_msg = "Indexing non-array value at compile time";
                    return false;
                }
                auto array = std::get<std::shared_ptr<CTArray>>(base_val);
                if (!array) {
                    error_msg = "Indexing null array";
                    return false;
                }
                if (static_cast<size_t>(idx) >= array->elements.size()) {
                    error_msg = "Index out of bounds in compile-time evaluation";
                    return false;
                }
                out = array->elements[static_cast<size_t>(idx)];
                return true;
            }
            default:
                error_msg = "Assignment target is not addressable at compile time";
                return false;
        }
    };

    assign_lvalue = [&](ExprPtr target, const CTValue& value) -> bool {
        if (!target) return false;
        switch (target->kind) {
            case Expr::Kind::Identifier:
                constants[target->name] = clone_value(value);
                uninitialized_locals.erase(target->name);
                result = constants[target->name];
                return true;
            case Expr::Kind::Member: {
                CTValue base_val;
                if (!fetch_lvalue(target->operand, base_val)) return false;
                if (!std::holds_alternative<std::shared_ptr<CTComposite>>(base_val)) {
                    error_msg = "Member access on non-composite value";
                    return false;
                }
                auto comp = std::get<std::shared_ptr<CTComposite>>(base_val);
                if (!comp) {
                    error_msg = "Member access on null composite value";
                    return false;
                }
                auto new_comp = std::make_shared<CTComposite>();
                new_comp->type_name = comp->type_name;
                for (const auto& entry : comp->fields) {
                    new_comp->fields[entry.first] = clone_value(entry.second);
                }
                new_comp->fields[target->name] = clone_value(value);
                if (!assign_lvalue(target->operand, new_comp)) {
                    return false;
                }
                result = clone_value(value);
                return true;
            }
            case Expr::Kind::Index: {
                CTValue base_val;
                if (!fetch_lvalue(target->operand, base_val)) return false;
                CTValue index_val;
                if (!try_evaluate(target->args[0], index_val)) return false;
                if (!std::holds_alternative<int64_t>(index_val) &&
                    !std::holds_alternative<uint64_t>(index_val)) {
                    error_msg = "Index must be an integer constant";
                    return false;
                }
                int64_t idx = std::holds_alternative<int64_t>(index_val)
                    ? std::get<int64_t>(index_val)
                    : static_cast<int64_t>(std::get<uint64_t>(index_val));
                if (idx < 0) {
                    error_msg = "Index cannot be negative";
                    return false;
                }
                if (!std::holds_alternative<std::shared_ptr<CTArray>>(base_val)) {
                    error_msg = "Indexing non-array value at compile time";
                    return false;
                }
                auto array = std::get<std::shared_ptr<CTArray>>(base_val);
                if (!array) {
                    error_msg = "Indexing null array";
                    return false;
                }
                if (static_cast<size_t>(idx) >= array->elements.size()) {
                    error_msg = "Index out of bounds in compile-time evaluation";
                    return false;
                }
                auto new_array = std::make_shared<CTArray>();
                new_array->elements.reserve(array->elements.size());
                for (const auto& elem : array->elements) {
                    new_array->elements.push_back(clone_value(elem));
                }
                new_array->elements[static_cast<size_t>(idx)] = clone_value(value);
                if (!assign_lvalue(target->operand, new_array)) {
                    return false;
                }
                result = clone_value(value);
                return true;
            }
            default:
                error_msg = "Assignment target is not addressable at compile time";
                return false;
        }
    };

    return assign_lvalue(expr->left, rhs_val);
}

bool CompileTimeEvaluator::eval_array_literal(ExprPtr expr, CTValue& result) {
    if (!expr) return false;
    auto array = std::make_shared<CTArray>();
    array->elements.reserve(expr->elements.size());
    for (const auto& elem : expr->elements) {
        CTValue elem_val;
        if (!try_evaluate(elem, elem_val)) {
            return false;
        }
        array->elements.push_back(clone_value(elem_val));
    }
    result = array;
    return true;
}

bool CompileTimeEvaluator::eval_tuple_literal(ExprPtr expr, CTValue& result) {
    if (!expr) return false;
    auto tuple = std::make_shared<CTComposite>();
    if (expr->type && expr->type->kind == Type::Kind::Named) {
        tuple->type_name = expr->type->type_name;
    }
    for (size_t i = 0; i < expr->elements.size(); ++i) {
        CTValue elem_val;
        if (!try_evaluate(expr->elements[i], elem_val)) {
            return false;
        }
        std::string field_name = std::string(MANGLED_PREFIX) + std::to_string(i);
        tuple->fields[field_name] = clone_value(elem_val);
    }
    result = tuple;
    return true;
}

bool CompileTimeEvaluator::eval_range(ExprPtr expr, CTValue& result) {
    if (!expr || !expr->left || !expr->right) return false;
    CTValue start_val;
    CTValue end_val;
    if (!try_evaluate(expr->left, start_val)) return false;
    if (!try_evaluate(expr->right, end_val)) return false;

    if (!std::holds_alternative<int64_t>(start_val) &&
        !std::holds_alternative<uint64_t>(start_val)) {
        error_msg = "Range bounds must be integer constants";
        return false;
    }
    if (!std::holds_alternative<int64_t>(end_val) &&
        !std::holds_alternative<uint64_t>(end_val)) {
        error_msg = "Range bounds must be integer constants";
        return false;
    }

    int64_t start = std::holds_alternative<int64_t>(start_val)
        ? std::get<int64_t>(start_val)
        : static_cast<int64_t>(std::get<uint64_t>(start_val));
    int64_t end = std::holds_alternative<int64_t>(end_val)
        ? std::get<int64_t>(end_val)
        : static_cast<int64_t>(std::get<uint64_t>(end_val));

    if (start == end) {
        error_msg = "Range cannot produce an empty array";
        return false;
    }

    auto array = std::make_shared<CTArray>();
    if (start < end) {
        array->elements.reserve(static_cast<size_t>(end - start));
        for (int64_t v = start; v < end; ++v) {
            array->elements.push_back((int64_t)v);
        }
    } else {
        array->elements.reserve(static_cast<size_t>(start - end));
        for (int64_t v = start; v > end; --v) {
            array->elements.push_back((int64_t)v);
        }
    }

    result = array;
    return true;
}

bool CompileTimeEvaluator::eval_index(ExprPtr expr, CTValue& result) {
    if (!expr || !expr->operand || expr->args.empty()) return false;
    CTValue container_val;
    if (!try_evaluate(expr->operand, container_val)) {
        return false;
    }
    CTValue index_val;
    if (!try_evaluate(expr->args[0], index_val)) {
        return false;
    }

    if (!std::holds_alternative<int64_t>(index_val) &&
        !std::holds_alternative<uint64_t>(index_val)) {
        error_msg = "Index must be an integer constant";
        return false;
    }
    int64_t idx = std::holds_alternative<int64_t>(index_val)
        ? std::get<int64_t>(index_val)
        : static_cast<int64_t>(std::get<uint64_t>(index_val));
    if (idx < 0) {
        error_msg = "Index cannot be negative";
        return false;
    }

    if (std::holds_alternative<std::shared_ptr<CTArray>>(container_val)) {
        auto array = std::get<std::shared_ptr<CTArray>>(container_val);
        if (!array) {
            error_msg = "Indexing null array";
            return false;
        }
        if (static_cast<size_t>(idx) >= array->elements.size()) {
            error_msg = "Index out of bounds in compile-time evaluation";
            return false;
        }
        result = array->elements[static_cast<size_t>(idx)];
        return true;
    }

    if (std::holds_alternative<std::string>(container_val)) {
        const auto& str = std::get<std::string>(container_val);
        if (static_cast<size_t>(idx) >= str.size()) {
            error_msg = "Index out of bounds in compile-time evaluation";
            return false;
        }
        result = (uint64_t)(uint8_t)str[static_cast<size_t>(idx)];
        return true;
    }

    error_msg = "Indexing non-array value at compile time";
    return false;
}

bool CompileTimeEvaluator::eval_iteration(ExprPtr expr, CTValue& result) {
    if (!expr || !expr->operand || !expr->right) return false;

    CTValue iterable_val;
    if (!try_evaluate(expr->operand, iterable_val)) {
        return false;
    }

    if (!std::holds_alternative<std::shared_ptr<CTArray>>(iterable_val)) {
        error_msg = "Iteration requires compile-time array or range";
        return false;
    }

    auto array = std::get<std::shared_ptr<CTArray>>(iterable_val);
    if (!array) {
        error_msg = "Iteration over null array";
        return false;
    }

    std::vector<CTValue> elements = array->elements;
    if (expr->is_sorted_iteration && elements.size() > 1) {
        auto same_kind = [&](const CTValue& a, const CTValue& b) -> bool {
            return a.index() == b.index();
        };
        for (size_t i = 1; i < elements.size(); ++i) {
            if (!same_kind(elements[0], elements[i])) {
                error_msg = "Sorted iteration requires uniform scalar element types";
                return false;
            }
        }

        if (std::holds_alternative<int64_t>(elements[0])) {
            std::sort(elements.begin(), elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<int64_t>(a) < std::get<int64_t>(b);
                      });
        } else if (std::holds_alternative<uint64_t>(elements[0])) {
            std::sort(elements.begin(), elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<uint64_t>(a) < std::get<uint64_t>(b);
                      });
        } else if (std::holds_alternative<double>(elements[0])) {
            std::sort(elements.begin(), elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<double>(a) < std::get<double>(b);
                      });
        } else if (std::holds_alternative<bool>(elements[0])) {
            std::sort(elements.begin(), elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<bool>(a) < std::get<bool>(b);
                      });
        } else if (std::holds_alternative<std::string>(elements[0])) {
            std::sort(elements.begin(), elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<std::string>(a) < std::get<std::string>(b);
                      });
        } else {
            error_msg = "Sorted iteration not supported for composite values at compile time";
            return false;
        }
    }

    auto saved_constants = constants;
    auto saved_uninitialized = uninitialized_locals;

    bool had_underscore = constants.count("_") > 0;
    CTValue saved_underscore;
    bool underscore_uninit = uninitialized_locals.count("_") > 0;
    if (had_underscore) {
        saved_underscore = constants["_"];
    }

    struct LoopGuard {
        int& depth;
        explicit LoopGuard(int& d) : depth(d) { depth++; }
        ~LoopGuard() { depth--; }
    } loop_guard(loop_depth);

    for (const auto& elem : elements) {
        constants["_"] = clone_value(elem);
        uninitialized_locals.erase("_");

        CTValue body_val;
        try {
            if (!try_evaluate(expr->right, body_val)) {
                constants = saved_constants;
                uninitialized_locals = saved_uninitialized;
                return false;
            }
        } catch (const EvalContinue&) {
            continue;
        } catch (const EvalBreak&) {
            break;
        } catch (const EvalReturn&) {
            throw;
        }
    }

    if (had_underscore) {
        constants["_"] = saved_underscore;
    } else {
        constants.erase("_");
    }
    if (underscore_uninit) {
        uninitialized_locals.insert("_");
    } else {
        uninitialized_locals.erase("_");
    }

    result = (int64_t)0;
    return true;
}

bool CompileTimeEvaluator::eval_repeat(ExprPtr expr, CTValue& result) {
    if (!expr || !expr->condition || !expr->right) return false;

    auto saved_constants = constants;
    auto saved_uninitialized = uninitialized_locals;

    struct LoopGuard {
        int& depth;
        explicit LoopGuard(int& d) : depth(d) { depth++; }
        ~LoopGuard() { depth--; }
    } loop_guard(loop_depth);

    auto to_bool = [&](const CTValue& v, bool& out) -> bool {
        if (std::holds_alternative<int64_t>(v)) {
            out = std::get<int64_t>(v) != 0;
            return true;
        }
        if (std::holds_alternative<uint64_t>(v)) {
            out = std::get<uint64_t>(v) != 0;
            return true;
        }
        if (std::holds_alternative<bool>(v)) {
            out = std::get<bool>(v);
            return true;
        }
        if (std::holds_alternative<double>(v)) {
            out = std::get<double>(v) != 0.0;
            return true;
        }
        return false;
    };

    int iterations = 0;
    while (true) {
        CTValue cond_val;
        if (!try_evaluate(expr->condition, cond_val)) {
            constants = saved_constants;
            uninitialized_locals = saved_uninitialized;
            return false;
        }
        bool is_true = false;
        if (!to_bool(cond_val, is_true)) {
            error_msg = "Repeat condition must be a scalar value";
            constants = saved_constants;
            uninitialized_locals = saved_uninitialized;
            return false;
        }
        if (!is_true) {
            break;
        }

        if (iterations++ >= MAX_LOOP_ITERATIONS) {
            error_msg = "Repeat loop exceeded compile-time iteration limit";
            constants = saved_constants;
            uninitialized_locals = saved_uninitialized;
            return false;
        }

        CTValue body_val;
        try {
            if (!try_evaluate(expr->right, body_val)) {
                constants = saved_constants;
                uninitialized_locals = saved_uninitialized;
                return false;
            }
        } catch (const EvalContinue&) {
            continue;
        } catch (const EvalBreak&) {
            break;
        } catch (const EvalReturn&) {
            throw;
        }
    }

    result = (int64_t)0;
    return true;
}

bool CompileTimeEvaluator::eval_length(ExprPtr expr, CTValue& result) {
    if (!expr || !expr->operand) return false;
    CTValue val;
    bool evaluated = try_evaluate(expr->operand, val);
    if (evaluated) {
        if (std::holds_alternative<std::shared_ptr<CTArray>>(val)) {
            auto array = std::get<std::shared_ptr<CTArray>>(val);
            if (!array) {
                error_msg = "Length on null array";
                return false;
            }
            result = (int64_t)array->elements.size();
            return true;
        }
        if (std::holds_alternative<std::string>(val)) {
            result = (int64_t)std::get<std::string>(val).size();
            return true;
        }
    }

    if (expr->operand->type && expr->operand->type->kind == Type::Kind::Array &&
        expr->operand->type->array_size) {
        CTValue size_val;
        if (try_evaluate(expr->operand->type->array_size, size_val)) {
            if (std::holds_alternative<int64_t>(size_val)) {
                result = std::get<int64_t>(size_val);
                return true;
            }
            if (std::holds_alternative<uint64_t>(size_val)) {
                result = (int64_t)std::get<uint64_t>(size_val);
                return true;
            }
        }
    }
    error_msg = "Length requires array or string at compile time";
    return false;
}

bool CompileTimeEvaluator::is_pure_for_compile_time(StmtPtr func, std::string& reason) {
    if (!func || !func->body) {
        return true;
    }

    // Prevent infinite recursion when analysing recursive functions
    if (purity_stack.count(func.get())) {
        return true;
    }

    purity_stack.insert(func.get());
    push_ref_params(func);
    bool pure = is_expr_pure(func->body, reason);
    pop_ref_params();
    purity_stack.erase(func.get());
    return pure;
}

bool CompileTimeEvaluator::is_expr_pure(ExprPtr expr, std::string& reason) {
    if (!expr) return true;

    switch (expr->kind) {
        case Expr::Kind::Assignment:
            if (expr->left) {
                std::string base = base_identifier(expr->left);
                if (!base.empty() && is_ref_param(base)) {
                    reason = "mutates receiver '" + base + "'";
                    return false;
                }
            }
            // Check if assigning to a global mutable variable
            if (expr->left) {
                std::string base = base_identifier(expr->left);
                if (!base.empty()) {
                    Symbol* sym = type_checker->get_scope()->lookup(base);
                    if (sym && sym->kind == Symbol::Kind::Variable && sym->is_mutable) {
                        reason = "modifies mutable global variable '" + base + "'";
                        return false;
                    }
                }
            }
            return is_expr_pure(expr->right, reason);

        case Expr::Kind::Call:
            // Check if calling an external function
            if (expr->operand && expr->operand->kind == Expr::Kind::Identifier) {
                Symbol* sym = type_checker->get_scope()->lookup(expr->operand->name);
                if (sym && sym->kind == Symbol::Kind::Function) {
                    if (sym->declaration && sym->declaration->is_external) {
                        reason = "calls external function '" + expr->operand->name + "'";
                        return false;
                    }
                    // Recursively check called function
                    if (sym->declaration && !is_pure_for_compile_time(sym->declaration, reason)) {
                        return false;
                    }
                }
            }
            for (const auto& rec : expr->receivers) {
                if (!is_expr_pure(rec, reason)) return false;
            }
            // Check arguments
            for (const auto& arg : expr->args) {
                if (!is_expr_pure(arg, reason)) return false;
            }
            return true;

        case Expr::Kind::Binary:
            return is_expr_pure(expr->left, reason) && is_expr_pure(expr->right, reason);

        case Expr::Kind::Unary:
            return is_expr_pure(expr->operand, reason);

        case Expr::Kind::Conditional:
            return is_expr_pure(expr->condition, reason) &&
                   is_expr_pure(expr->true_expr, reason) &&
                   is_expr_pure(expr->false_expr, reason);

        case Expr::Kind::Block:
            for (const auto& stmt : expr->statements) {
                if (!is_stmt_pure(stmt, reason)) return false;
            }
            return is_expr_pure(expr->result_expr, reason);

        case Expr::Kind::Index:
            return is_expr_pure(expr->operand, reason) &&
                   (expr->args.empty() ? true : is_expr_pure(expr->args[0], reason));

        case Expr::Kind::Member:
            return is_expr_pure(expr->operand, reason);

        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) {
                if (!is_expr_pure(elem, reason)) return false;
            }
            return true;

        case Expr::Kind::Cast:
            return is_expr_pure(expr->operand, reason);

        case Expr::Kind::Range:
            return is_expr_pure(expr->left, reason) && is_expr_pure(expr->right, reason);

        case Expr::Kind::Length:
            return is_expr_pure(expr->operand, reason);

        case Expr::Kind::Iteration:
            return is_expr_pure(expr->operand, reason) && is_expr_pure(expr->right, reason);

        case Expr::Kind::Repeat:
            return is_expr_pure(expr->condition, reason) && is_expr_pure(expr->right, reason);

        default:
            // Literals and identifiers are pure
            return true;
    }
}

bool CompileTimeEvaluator::is_stmt_pure(StmtPtr stmt, std::string& reason) {
    if (!stmt) return true;

    switch (stmt->kind) {
        case Stmt::Kind::Expr:
            return is_expr_pure(stmt->expr, reason);

        case Stmt::Kind::Return:
            return is_expr_pure(stmt->return_expr, reason);

        case Stmt::Kind::VarDecl:
            return is_expr_pure(stmt->var_init, reason);

        case Stmt::Kind::ConditionalStmt:
            return is_expr_pure(stmt->condition, reason) &&
                   is_stmt_pure(stmt->true_stmt, reason);

        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            // These are pure (they just control flow)
            return true;

        default:
            return true;
    }
}

void CompileTimeEvaluator::push_ref_params(StmtPtr func) {
    std::unordered_set<std::string> refs;
    if (func) {
        for (const auto& name : func->ref_params) {
            refs.insert(name);
        }
    }
    ref_param_stack.push_back(std::move(refs));
}

void CompileTimeEvaluator::pop_ref_params() {
    if (!ref_param_stack.empty()) {
        ref_param_stack.pop_back();
    }
}

bool CompileTimeEvaluator::is_ref_param(const std::string& name) const {
    if (ref_param_stack.empty()) return false;
    return ref_param_stack.back().count(name) > 0;
}

std::string CompileTimeEvaluator::base_identifier(ExprPtr expr) const {
    while (expr) {
        if (expr->kind == Expr::Kind::Identifier) {
            return expr->name;
        }
        if (expr->kind == Expr::Kind::Member || expr->kind == Expr::Kind::Index) {
            expr = expr->operand;
            continue;
        }
        break;
    }
    return "";
}

} // namespace vexel
