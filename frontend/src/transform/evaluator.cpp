#include "evaluator.h"
#include "constants.h"
#include "expr_access.h"
#include "typechecker.h"
#include <algorithm>
#include <functional>

namespace vexel {

namespace {
struct EvalBreak {};
struct EvalContinue {};
struct EvalReturn {
    CTValue value;
};

std::string ct_value_kind(const CTValue& value) {
    if (std::holds_alternative<int64_t>(value)) return "int";
    if (std::holds_alternative<uint64_t>(value)) return "uint";
    if (std::holds_alternative<double>(value)) return "float";
    if (std::holds_alternative<bool>(value)) return "bool";
    if (std::holds_alternative<std::string>(value)) return "string";
    if (std::holds_alternative<CTUninitialized>(value)) return "uninitialized";
    if (std::holds_alternative<std::shared_ptr<CTComposite>>(value)) return "composite";
    if (std::holds_alternative<std::shared_ptr<CTArray>>(value)) return "array";
    return "unknown";
}

} // namespace

CTEQueryResult CompileTimeEvaluator::query(ExprPtr expr) {
    error_msg.clear();
    hard_error = false;

    CTEQueryResult out;
    CTValue value;
    if (try_evaluate(expr, value)) {
        out.status = CTEQueryStatus::Known;
        out.value = copy_ct_value(value);
        return out;
    }

    out.status = hard_error ? CTEQueryStatus::Error : CTEQueryStatus::Unknown;
    out.message = error_msg;
    return out;
}

void CompileTimeEvaluator::reset_state() {
    constants.clear();
    symbol_constants.clear();
    uninitialized_locals.clear();
    ref_param_stack.clear();
    error_msg.clear();
    recursion_depth = 0;
    loop_depth = 0;
    return_depth = 0;
    constant_eval_stack.clear();
    constant_value_cache.clear();
    expr_param_stack.clear();
    expanding_expr_params.clear();
    expr_param_expansion_depth = 0;
    hard_error = false;
    value_observer = nullptr;
    symbol_read_observer = nullptr;
}

bool CompileTimeEvaluator::try_evaluate(ExprPtr expr, CTValue& result) {
    if (!expr) {
        error_msg = "Null expression";
        return false;
    }

    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        error_msg = "Recursion depth limit exceeded in compile-time evaluation";
        hard_error = true;
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
            success = eval_block(expr, result);
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
        hard_error = true;
        success = false;
    }

    if (success && expr && value_observer) {
        value_observer(expr.get(), result);
    }
    return success;
}

bool CompileTimeEvaluator::evaluate_constant_symbol(Symbol* sym, CTValue& result) {
    if (!sym || sym->kind != Symbol::Kind::Constant || !sym->declaration || !sym->declaration->var_init) {
        return false;
    }

    auto cached = constant_value_cache.find(sym);
    if (cached != constant_value_cache.end()) {
        result = copy_ct_value(cached->second);
        return true;
    }

    if (constant_eval_stack.count(sym) > 0) {
        error_msg = "Compile-time dependency cycle detected at symbol: " + sym->name;
        hard_error = true;
        return false;
    }

    constant_eval_stack.insert(sym);
    const bool ok = try_evaluate(sym->declaration->var_init, result);
    constant_eval_stack.erase(sym);
    if (!ok) {
        return false;
    }

    if (sym->type) {
        CTValue coerced;
        if (!coerce_value_to_type(result, sym->type, coerced)) {
            return false;
        }
        result = copy_ct_value(coerced);
    }

    constant_value_cache[sym] = copy_ct_value(result);
    return true;
}

bool CompileTimeEvaluator::declare_uninitialized_local(const StmtPtr& stmt) {
    if (!stmt || stmt->kind != Stmt::Kind::VarDecl) {
        return false;
    }

    if (!stmt->var_type) {
        uninitialized_locals.insert(stmt->var_name);
        return true;
    }

    TypePtr var_type = stmt->var_type;
    if (var_type->kind == Type::Kind::Array) {
        if (!var_type->array_size) {
            error_msg = "Array local requires compile-time size";
            return false;
        }
        CTValue size_val;
        if (!try_evaluate(var_type->array_size, size_val)) {
            error_msg = "Array local requires compile-time size";
            return false;
        }
        int64_t size = 0;
        if (std::holds_alternative<int64_t>(size_val)) {
            size = std::get<int64_t>(size_val);
        } else if (std::holds_alternative<uint64_t>(size_val)) {
            size = static_cast<int64_t>(std::get<uint64_t>(size_val));
        } else {
            error_msg = "Array local size must be an integer constant";
            return false;
        }
        if (size < 0) {
            error_msg = "Array local size cannot be negative";
            return false;
        }
        auto array = std::make_shared<CTArray>();
        array->elements.resize(static_cast<size_t>(size), CTUninitialized{});
        constants[stmt->var_name] = array;
        uninitialized_locals.erase(stmt->var_name);
        return true;
    }

    if (var_type->kind == Type::Kind::Named &&
        var_type->resolved_symbol &&
        var_type->resolved_symbol->declaration &&
        var_type->resolved_symbol->declaration->kind == Stmt::Kind::TypeDecl) {
        auto composite = std::make_shared<CTComposite>();
        composite->type_name = var_type->type_name;
        for (const auto& field : var_type->resolved_symbol->declaration->fields) {
            composite->fields[field.name] = CTUninitialized{};
        }
        constants[stmt->var_name] = composite;
        uninitialized_locals.erase(stmt->var_name);
        return true;
    }

    uninitialized_locals.insert(stmt->var_name);
    return true;
}

bool CompileTimeEvaluator::eval_block(ExprPtr expr, CTValue& result) {
    if (!expr || expr->kind != Expr::Kind::Block) {
        return false;
    }

    struct LocalShadow {
        bool had_const = false;
        CTValue old_const = static_cast<int64_t>(0);
        bool had_uninitialized = false;
    };

    std::unordered_map<std::string, LocalShadow> shadows;
    std::vector<std::string> local_names;

    auto remember_local = [&](const std::string& name) {
        if (name.empty() || shadows.count(name) > 0) {
            return;
        }

        LocalShadow shadow;
        auto const_it = constants.find(name);
        if (const_it != constants.end()) {
            shadow.had_const = true;
            shadow.old_const = copy_ct_value(const_it->second);
        }
        shadow.had_uninitialized = uninitialized_locals.count(name) > 0;
        shadows[name] = std::move(shadow);
        local_names.push_back(name);
    };

    auto cleanup_locals = [&]() {
        for (const auto& name : local_names) {
            constants.erase(name);
            uninitialized_locals.erase(name);
            const LocalShadow& shadow = shadows[name];
            if (shadow.had_const) {
                constants[name] = copy_ct_value(shadow.old_const);
            }
            if (shadow.had_uninitialized) {
                uninitialized_locals.insert(name);
            }
        }
    };

    auto scalar_to_bool = [&](const CTValue& v, bool& out) -> bool {
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

    std::function<bool(const StmtPtr&)> eval_stmt = [&](const StmtPtr& stmt) -> bool {
        if (!stmt) return true;

        switch (stmt->kind) {
            case Stmt::Kind::Expr: {
                if (!stmt->expr) return true;
                if (stmt->expr->kind == Expr::Kind::Assignment &&
                    stmt->expr->creates_new_variable &&
                    stmt->expr->left &&
                    stmt->expr->left->kind == Expr::Kind::Identifier) {
                    remember_local(stmt->expr->left->name);
                }
                CTValue ignored;
                if (!try_evaluate(stmt->expr, ignored)) {
                    cleanup_locals();
                    return false;
                }
                return true;
            }
            case Stmt::Kind::VarDecl: {
                remember_local(stmt->var_name);
                if (stmt->var_init) {
                    CTValue init_val;
                    if (!try_evaluate(stmt->var_init, init_val)) {
                        cleanup_locals();
                        return false;
                    }
                    CTValue stored_val = copy_ct_value(init_val);
                    if (stmt->var_type &&
                        !coerce_value_to_type(stored_val, stmt->var_type, stored_val)) {
                        cleanup_locals();
                        return false;
                    }
                    constants[stmt->var_name] = copy_ct_value(stored_val);
                    uninitialized_locals.erase(stmt->var_name);
                } else if (!declare_uninitialized_local(stmt)) {
                    cleanup_locals();
                    return false;
                }
                return true;
            }
            case Stmt::Kind::ConditionalStmt: {
                CTValue cond_val;
                if (!try_evaluate(stmt->condition, cond_val)) {
                    cleanup_locals();
                    return false;
                }
                bool is_true = false;
                if (!scalar_to_bool(cond_val, is_true)) {
                    error_msg = "Conditional expression condition must be a scalar value";
                    cleanup_locals();
                    return false;
                }
                if (is_true && !eval_stmt(stmt->true_stmt)) {
                    return false;
                }
                return true;
            }
            case Stmt::Kind::Return: {
                if (!stmt->return_expr) {
                    error_msg = "Return statement requires an expression at compile time";
                    cleanup_locals();
                    return false;
                }
                CTValue ret_val;
                if (!try_evaluate(stmt->return_expr, ret_val)) {
                    cleanup_locals();
                    return false;
                }
                throw EvalReturn{copy_ct_value(ret_val)};
            }
            case Stmt::Kind::Break:
                if (loop_depth > 0) {
                    throw EvalBreak{};
                }
                error_msg = "Break used outside of loop in compile-time evaluation";
                cleanup_locals();
                return false;
            case Stmt::Kind::Continue:
                if (loop_depth > 0) {
                    throw EvalContinue{};
                }
                error_msg = "Continue used outside of loop in compile-time evaluation";
                cleanup_locals();
                return false;
            default:
                return true;
        }
    };

    for (const auto& stmt : expr->statements) {
        if (!eval_stmt(stmt)) {
            return false;
        }
    }

    if (expr->result_expr) {
        if (!try_evaluate(expr->result_expr, result)) {
            cleanup_locals();
            return false;
        }
    } else {
        result = static_cast<int64_t>(0);
    }

    cleanup_locals();
    return true;
}

bool CompileTimeEvaluator::eval_literal(ExprPtr expr, CTValue& result) {
    switch (expr->kind) {
        case Expr::Kind::IntLiteral:
            if (expr->type &&
                expr->type->kind == Type::Kind::Primitive &&
                expr->type->primitive == PrimitiveType::Bool) {
                result = (expr->uint_val != 0u);
                return true;
            }
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
            result = false;
            return true;
        }
        if (expr->op == "||" && left_bool) {
            result = true;
            return true;
        }

        if (!try_evaluate(expr->right, right_val)) return false;
        bool right_bool = false;
        if (!to_bool(right_val, right_bool)) {
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
        else if (expr->op == "!") result = !v;
        else {
            error_msg = "Unsupported unary operator: " + expr->op;
            return false;
        }
        return true;
    }

    if (std::holds_alternative<double>(operand_val)) {
        double v = std::get<double>(operand_val);
        if (expr->op == "-") result = -v;
        else if (expr->op == "!") result = !v;
        else {
            error_msg = "Unsupported unary operator: " + expr->op;
            return false;
        }
        return true;
    }

    if (std::holds_alternative<bool>(operand_val)) {
        bool v = std::get<bool>(operand_val);
        if (expr->op == "!") {
            result = !v;
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
    if (expr->operand) {
        sym = expr->operand->resolved_symbol;
    }
    if (!sym && type_checker) {
        sym = type_checker->binding_for(expr->operand.get());
        if (expr->operand) {
            expr->operand->resolved_symbol = sym;
        }
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
    if (expr->args.size() != func->params.size()) {
        error_msg = "Argument count mismatch in compile-time evaluation";
        return false;
    }

    // Check if function is pure enough for compile-time evaluation
    if (func->is_external) {
        error_msg = "External functions cannot be evaluated at compile time";
        return false;
    }

    // Do not reject calls with whole-function purity checks here.
    // Evaluation is path-sensitive: if the concrete call instance reaches an impure
    // operation (e.g., external call, mutable global write), try_evaluate fails.

    // Evaluate arguments
    std::unordered_map<std::string, CTValue> saved_constants = constants;
    std::unordered_set<std::string> saved_uninitialized = uninitialized_locals;
    std::unordered_set<std::string> call_bindings;
    std::unordered_map<std::string, ExprPtr> expr_param_bindings;

    auto restore_all_state = [&]() {
        constants = saved_constants;
        uninitialized_locals = saved_uninitialized;
    };

    auto restore_binding = [&](const std::string& name) {
        auto it = saved_constants.find(name);
        if (it != saved_constants.end()) {
            constants[name] = copy_ct_value(it->second);
        } else {
            constants.erase(name);
        }
        if (saved_uninitialized.count(name) > 0) {
            uninitialized_locals.insert(name);
        } else {
            uninitialized_locals.erase(name);
        }
    };

    auto cleanup_call_frame = [&]() {
        for (const auto& name : call_bindings) {
            restore_binding(name);
        }
    };

    if (!func->ref_params.empty()) {
        if (expr->receivers.size() != func->ref_params.size()) {
            error_msg = "Receiver count mismatch in compile-time evaluation";
            return false;
        }
        for (size_t i = 0; i < func->ref_params.size(); i++) {
            call_bindings.insert(func->ref_params[i]);
            CTValue rec_val;
            if (!try_evaluate(expr->receivers[i], rec_val)) {
                restore_all_state();
                return false;
            }
            if (i < func->ref_param_types.size() && func->ref_param_types[i]) {
                CTValue coerced;
                if (!coerce_value_to_type(rec_val, func->ref_param_types[i], coerced)) {
                    restore_all_state();
                    return false;
                }
                constants[func->ref_params[i]] = copy_ct_value(coerced);
            } else {
                constants[func->ref_params[i]] = copy_ct_value(rec_val);
            }
            uninitialized_locals.erase(func->ref_params[i]);
        }
    }
    for (size_t i = 0; i < expr->args.size(); i++) {
        const Parameter& param = func->params[i];
        call_bindings.insert(param.name);
        if (param.is_expression_param) {
            expr_param_bindings[param.name] = expr->args[i];
            continue;
        }
        CTValue arg_val;
        if (!try_evaluate(expr->args[i], arg_val)) {
            restore_all_state();
            return false;
        }
        if (param.type) {
            CTValue coerced;
            if (!coerce_value_to_type(arg_val, param.type, coerced)) {
                restore_all_state();
                return false;
            }
            constants[param.name] = copy_ct_value(coerced);
        } else {
            constants[param.name] = copy_ct_value(arg_val);
        }
        uninitialized_locals.erase(param.name);
    }

    // Evaluate function body
    if (!func->body) {
        error_msg = "Function has no body";
        restore_all_state();
        return false;
    }

    push_ref_params(func);
    struct RefParamGuard {
        CompileTimeEvaluator* self;
        ~RefParamGuard() {
            if (self) self->pop_ref_params();
        }
    } ref_guard{this};

    bool pushed_expr_params = false;
    if (!expr_param_bindings.empty()) {
        expr_param_stack.push_back(std::move(expr_param_bindings));
        pushed_expr_params = true;
    }
    struct ExprParamGuard {
        CompileTimeEvaluator* self;
        bool active = false;
        ~ExprParamGuard() {
            if (active && self && !self->expr_param_stack.empty()) {
                self->expr_param_stack.pop_back();
            }
        }
    } expr_param_guard{this, pushed_expr_params};

    struct ReturnGuard {
        int& depth;
        explicit ReturnGuard(int& d) : depth(d) { depth++; }
        ~ReturnGuard() { depth--; }
    } return_guard(return_depth);

    bool success = false;
    try {
        success = try_evaluate(func->body, result);
    } catch (const EvalReturn& ret) {
        result = copy_ct_value(ret.value);
        success = true;
    }
    if (success) {
        if (!func->return_types.empty()) {
            if (!std::holds_alternative<std::shared_ptr<CTComposite>>(result)) {
                error_msg = "Tuple return value expected for compile-time call";
                success = false;
            } else {
                auto in_comp = std::get<std::shared_ptr<CTComposite>>(result);
                if (!in_comp) {
                    error_msg = "Tuple return value is null in compile-time call";
                    success = false;
                } else {
                    auto out_comp = std::make_shared<CTComposite>();
                    if (expr->type && expr->type->kind == Type::Kind::Named) {
                        out_comp->type_name = expr->type->type_name;
                    } else {
                        out_comp->type_name = in_comp->type_name;
                    }
                    for (size_t i = 0; i < func->return_types.size(); ++i) {
                        const std::string field_name = std::string(MANGLED_PREFIX) + std::to_string(i);
                        auto it = in_comp->fields.find(field_name);
                        if (it == in_comp->fields.end()) {
                            error_msg = "Missing tuple return field in compile-time call: " + field_name;
                            success = false;
                            break;
                        }
                        CTValue coerced_field = copy_ct_value(it->second);
                        if (func->return_types[i] &&
                            !coerce_value_to_type(it->second, func->return_types[i], coerced_field)) {
                            success = false;
                            break;
                        }
                        out_comp->fields[field_name] = copy_ct_value(coerced_field);
                    }
                    if (success) {
                        result = out_comp;
                    }
                }
            }
        } else if (func->return_type) {
            CTValue coerced_result;
            if (!coerce_value_to_type(result, func->return_type, coerced_result)) {
                success = false;
            } else {
                result = copy_ct_value(coerced_result);
            }
        }
    }
    if (!success) {
        restore_all_state();
        return false;
    }

    cleanup_call_frame();
    return true;
}

bool CompileTimeEvaluator::eval_identifier(ExprPtr expr, CTValue& result) {
    if (expanding_expr_params.count(expr->name) == 0) {
        for (auto it = expr_param_stack.rbegin(); it != expr_param_stack.rend(); ++it) {
            auto found = it->find(expr->name);
            if (found == it->end() || !found->second) {
                continue;
            }
            expanding_expr_params.insert(expr->name);
            struct ExprParamExpansionGuard {
                int& depth;
                explicit ExprParamExpansionGuard(int& d) : depth(d) { depth++; }
                ~ExprParamExpansionGuard() { depth--; }
            } expansion_guard(expr_param_expansion_depth);
            const bool ok = try_evaluate(found->second, result);
            expanding_expr_params.erase(expr->name);
            return ok;
        }
    }

    Symbol* sym = expr->resolved_symbol;
    if (expr_param_expansion_depth == 0 && !sym && type_checker) {
        sym = type_checker->binding_for(expr.get());
        expr->resolved_symbol = sym;
    }

    auto it = constants.find(expr->name);
    if (it != constants.end()) {
        if (std::holds_alternative<CTUninitialized>(it->second)) {
            error_msg = "uninitialized variable accessed at compile time: " + expr->name;
            return false;
        }
        result = it->second;
        return true;
    }

    if (uninitialized_locals.count(expr->name)) {
        error_msg = "uninitialized variable accessed at compile time: " + expr->name;
        return false;
    }

    // Try to look up global constant
    if (!sym && type_checker) {
        sym = type_checker->binding_for(expr.get());
        expr->resolved_symbol = sym;
    }
    if (!sym && type_checker && type_checker->get_scope()) {
        sym = type_checker->get_scope()->lookup(expr->name);
    }
    if (sym && symbol_read_observer) {
        symbol_read_observer(sym);
    }
    if (sym) {
        auto known = symbol_constants.find(sym);
        if (known != symbol_constants.end()) {
            if (std::holds_alternative<CTUninitialized>(known->second)) {
                error_msg = "uninitialized variable accessed at compile time: " + expr->name;
                return false;
            }
            result = copy_ct_value(known->second);
            return true;
        }
    }
    if (sym && sym->kind == Symbol::Kind::Constant && sym->declaration && sym->declaration->var_init) {
        if (!evaluate_constant_symbol(sym, result)) {
            return false;
        }
        return true;
    }

    error_msg = "Identifier not found or not a compile-time constant: " + expr->name;
    return false;
}

int64_t CompileTimeEvaluator::to_int(const CTValue& v) {
    if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
    if (std::holds_alternative<uint64_t>(v)) return (int64_t)std::get<uint64_t>(v);
    if (std::holds_alternative<double>(v)) return (int64_t)std::get<double>(v);
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? 1 : 0;
    throw CompileError("Cannot convert value to integer in compile-time evaluation (" + ct_value_kind(v) + ")",
                       SourceLocation());
}

double CompileTimeEvaluator::to_float(const CTValue& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    if (std::holds_alternative<int64_t>(v)) return (double)std::get<int64_t>(v);
    if (std::holds_alternative<uint64_t>(v)) return (double)std::get<uint64_t>(v);
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? 1.0 : 0.0;
    throw CompileError("Cannot convert value to float in compile-time evaluation (" + ct_value_kind(v) + ")",
                       SourceLocation());
}

bool CompileTimeEvaluator::eval_type_constructor(ExprPtr expr, CTValue& result) {
    // expr is a Call node where operand is a type name
    if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
        error_msg = "Type constructor must have identifier operand";
        return false;
    }

    std::string type_name = expr->operand->name;
    Symbol* sym = expr->operand->resolved_symbol;
    if (!sym && type_checker) {
        sym = type_checker->binding_for(expr->operand.get());
        expr->operand->resolved_symbol = sym;
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

        // Type constructors are only constexpr when every field argument is constexpr.
        if (!try_evaluate(expr->args[i], arg_val)) {
            return false;
        }

        // Store the field value
        std::string field_name = type_decl->fields[i].name;

        // Convert CTValue to the field storage type
        composite->fields[field_name] = copy_ct_value(arg_val);
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
    if (std::holds_alternative<CTUninitialized>(it->second)) {
        error_msg = "uninitialized field accessed at compile time: " + expr->name;
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

bool CompileTimeEvaluator::coerce_value_to_type(const CTValue& input,
                                                TypePtr target_type,
                                                CTValue& output) {
    if (std::holds_alternative<CTUninitialized>(input)) {
        output = CTUninitialized{};
        return true;
    }
    if (!target_type || target_type->kind == Type::Kind::TypeVar) {
        output = copy_ct_value(input);
        return true;
    }

    if (target_type->kind == Type::Kind::Primitive) {
        switch (target_type->primitive) {
            case PrimitiveType::I8:
            case PrimitiveType::I16:
            case PrimitiveType::I32:
            case PrimitiveType::I64:
                output = to_int(input);
                return true;
            case PrimitiveType::U8:
            case PrimitiveType::U16:
            case PrimitiveType::U32:
            case PrimitiveType::U64:
                output = static_cast<uint64_t>(to_int(input));
                return true;
            case PrimitiveType::F32:
            case PrimitiveType::F64:
                output = to_float(input);
                return true;
            case PrimitiveType::Bool:
                output = to_int(input) != 0;
                return true;
            case PrimitiveType::String:
                if (!std::holds_alternative<std::string>(input)) {
                    error_msg = "Type mismatch in compile-time coercion to string";
                    return false;
                }
                output = std::get<std::string>(input);
                return true;
        }
    }

    if (target_type->kind == Type::Kind::Array) {
        if (!std::holds_alternative<std::shared_ptr<CTArray>>(input)) {
            error_msg = "Type mismatch in compile-time coercion to array";
            return false;
        }
        auto in_array = std::get<std::shared_ptr<CTArray>>(input);
        if (!in_array) {
            error_msg = "Type mismatch in compile-time coercion to null array";
            return false;
        }
        if (target_type->array_size) {
            CTValue size_val;
            if (!try_evaluate(target_type->array_size, size_val)) {
                error_msg = "Array size must be compile-time constant in coercion";
                return false;
            }
            int64_t expected_size = 0;
            if (std::holds_alternative<int64_t>(size_val)) {
                expected_size = std::get<int64_t>(size_val);
            } else if (std::holds_alternative<uint64_t>(size_val)) {
                expected_size = static_cast<int64_t>(std::get<uint64_t>(size_val));
            } else {
                error_msg = "Array size must be integer in compile-time coercion";
                return false;
            }
            if (expected_size < 0 ||
                static_cast<size_t>(expected_size) != in_array->elements.size()) {
                error_msg = "Array size mismatch in compile-time coercion";
                return false;
            }
        }
        auto out_array = std::make_shared<CTArray>();
        out_array->elements.reserve(in_array->elements.size());
        for (const auto& elem : in_array->elements) {
            CTValue coerced_elem;
            if (target_type->element_type) {
                if (!coerce_value_to_type(elem, target_type->element_type, coerced_elem)) {
                    return false;
                }
            } else {
                coerced_elem = copy_ct_value(elem);
            }
            out_array->elements.push_back(copy_ct_value(coerced_elem));
        }
        output = out_array;
        return true;
    }

    if (target_type->kind == Type::Kind::Named) {
        if (!std::holds_alternative<std::shared_ptr<CTComposite>>(input)) {
            error_msg = "Type mismatch in compile-time coercion to named type";
            return false;
        }
        auto in_comp = std::get<std::shared_ptr<CTComposite>>(input);
        if (!in_comp) {
            error_msg = "Type mismatch in compile-time coercion to null composite";
            return false;
        }
        auto out_comp = std::make_shared<CTComposite>();
        out_comp->type_name = target_type->type_name;

        Symbol* type_sym = target_type->resolved_symbol;
        if (!type_sym && type_checker && type_checker->get_scope()) {
            type_sym = type_checker->get_scope()->lookup(target_type->type_name);
        }
        if (type_sym &&
            type_sym->declaration &&
            type_sym->declaration->kind == Stmt::Kind::TypeDecl) {
            for (const auto& field : type_sym->declaration->fields) {
                auto it = in_comp->fields.find(field.name);
                if (it == in_comp->fields.end()) {
                    error_msg = "Missing field in compile-time coercion: " + field.name;
                    return false;
                }
                CTValue coerced_field;
                if (!coerce_value_to_type(it->second, field.type, coerced_field)) {
                    return false;
                }
                out_comp->fields[field.name] = copy_ct_value(coerced_field);
            }
            output = out_comp;
            return true;
        }

        const bool is_internal_tuple_type =
            target_type->type_name.rfind("__Tuple", 0) == 0;
        if (is_internal_tuple_type && in_comp->type_name == target_type->type_name) {
            // Lowered tuple temporaries are compiler-internal named composites.
            // Keep strict behavior for user named types, but allow exact tuple passthrough.
            output = copy_ct_value(input);
            return true;
        }

        error_msg = "Named type must be resolved for compile-time coercion: " + target_type->type_name;
        return false;
    }

    error_msg = "Unsupported target type in compile-time coercion";
    return false;
}

bool CompileTimeEvaluator::coerce_value_to_lvalue_type(ExprPtr lvalue,
                                                       const CTValue& input,
                                                       CTValue& output) {
    TypePtr target_type;
    if (lvalue) {
        target_type = lvalue->type;
    }
    if (!target_type && lvalue && lvalue->kind == Expr::Kind::Identifier) {
        Symbol* sym = lvalue->resolved_symbol;
        if (!sym && type_checker) {
            sym = type_checker->binding_for(lvalue.get());
            lvalue->resolved_symbol = sym;
        }
        if (!sym && type_checker && type_checker->get_scope()) {
            sym = type_checker->get_scope()->lookup(lvalue->name);
        }
        if (sym) {
            target_type = sym->type;
        }
    }
    if (!target_type) {
        output = copy_ct_value(input);
        return true;
    }
    return coerce_value_to_type(input, target_type, output);
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

    CTValue assign_val = rhs_val;
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
        if (!std::holds_alternative<int64_t>(index_val) &&
            !std::holds_alternative<uint64_t>(index_val) &&
            !std::holds_alternative<bool>(index_val)) {
            error_msg = "Index must be an integer/bool constant, got " + ct_value_kind(index_val);
            return false;
        }
        if (std::holds_alternative<int64_t>(index_val)) {
            idx = std::get<int64_t>(index_val);
        } else if (std::holds_alternative<uint64_t>(index_val)) {
            idx = static_cast<int64_t>(std::get<uint64_t>(index_val));
        } else {
            idx = std::get<bool>(index_val) ? 1 : 0;
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

bool CompileTimeEvaluator::eval_array_literal(ExprPtr expr, CTValue& result) {
    if (!expr) return false;
    auto array = std::make_shared<CTArray>();
    array->elements.reserve(expr->elements.size());
    for (const auto& elem : expr->elements) {
        CTValue elem_val;
        if (!try_evaluate(elem, elem_val)) {
            return false;
        }
        array->elements.push_back(copy_ct_value(elem_val));
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
        tuple->fields[field_name] = copy_ct_value(elem_val);
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
        !std::holds_alternative<uint64_t>(index_val) &&
        !std::holds_alternative<bool>(index_val)) {
        error_msg = "Index must be an integer/bool constant, got " + ct_value_kind(index_val);
        return false;
    }
    int64_t idx = 0;
    if (std::holds_alternative<int64_t>(index_val)) {
        idx = std::get<int64_t>(index_val);
    } else if (std::holds_alternative<uint64_t>(index_val)) {
        idx = static_cast<int64_t>(std::get<uint64_t>(index_val));
    } else {
        idx = std::get<bool>(index_val) ? 1 : 0;
    }
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
        if (std::holds_alternative<CTUninitialized>(array->elements[static_cast<size_t>(idx)])) {
            error_msg = "uninitialized array element accessed at compile time";
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

    const std::vector<CTValue>* elements = &array->elements;
    std::vector<CTValue> sorted_elements;
    if (expr->is_sorted_iteration && elements->size() > 1) {
        sorted_elements = *elements;
        elements = &sorted_elements;
        auto same_kind = [&](const CTValue& a, const CTValue& b) -> bool {
            return a.index() == b.index();
        };
        for (size_t i = 1; i < elements->size(); ++i) {
            if (!same_kind((*elements)[0], (*elements)[i])) {
                error_msg = "Sorted iteration requires uniform scalar element types";
                return false;
            }
        }

        if (std::holds_alternative<int64_t>((*elements)[0])) {
            std::sort(sorted_elements.begin(), sorted_elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<int64_t>(a) < std::get<int64_t>(b);
                      });
        } else if (std::holds_alternative<uint64_t>((*elements)[0])) {
            std::sort(sorted_elements.begin(), sorted_elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<uint64_t>(a) < std::get<uint64_t>(b);
                      });
        } else if (std::holds_alternative<double>((*elements)[0])) {
            std::sort(sorted_elements.begin(), sorted_elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<double>(a) < std::get<double>(b);
                      });
        } else if (std::holds_alternative<bool>((*elements)[0])) {
            std::sort(sorted_elements.begin(), sorted_elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<bool>(a) < std::get<bool>(b);
                      });
        } else if (std::holds_alternative<std::string>((*elements)[0])) {
            std::sort(sorted_elements.begin(), sorted_elements.end(),
                      [](const CTValue& a, const CTValue& b) {
                          return std::get<std::string>(a) < std::get<std::string>(b);
                      });
        } else {
            error_msg = "Sorted iteration not supported for composite values at compile time";
            return false;
        }
    }

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

    for (const auto& elem : *elements) {
        constants["_"] = copy_ct_value(elem);
        uninitialized_locals.erase("_");

        CTValue body_val;
        try {
            if (!try_evaluate(expr->right, body_val)) {
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
            return false;
        }
        bool is_true = false;
        if (!to_bool(cond_val, is_true)) {
            error_msg = "Repeat condition must be a scalar value";
            return false;
        }
        if (!is_true) {
            break;
        }

        if (iterations++ >= MAX_LOOP_ITERATIONS) {
            error_msg = "Repeat loop exceeded compile-time iteration limit";
            return false;
        }

        CTValue body_val;
        try {
            if (!try_evaluate(expr->right, body_val)) {
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
