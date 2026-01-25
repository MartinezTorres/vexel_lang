#include "evaluator.h"

namespace vexel {

bool CompileTimeEvaluator::try_evaluate(ExprPtr expr, CTValue& result) {
    if (!expr) {
        error_msg = "Null expression";
        return false;
    }

    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        error_msg = "Recursion depth limit exceeded in compile-time evaluation";
        return false;
    }

    recursion_depth++;
    bool success = false;

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
        case Expr::Kind::Block:
            // Evaluate block - process statements, then evaluate result
            {
                // Save current constants
                auto saved_constants = constants;
                auto saved_uninitialized = uninitialized_locals;

                // Process each statement in the block
                for (const auto& stmt : expr->statements) {
                    if (stmt->kind == Stmt::Kind::Expr && stmt->expr) {
                        // Handle assignment expressions that create variables
                        if (stmt->expr->kind == Expr::Kind::Assignment &&
                            stmt->expr->left->kind == Expr::Kind::Identifier) {

                            // Evaluate the right-hand side
                            CTValue rhs_val;
                            if (!try_evaluate(stmt->expr->right, rhs_val)) {
                                constants = saved_constants;
                                recursion_depth--;
                                return false;
                            }

                            // Store as a local constant
                            constants[stmt->expr->left->name] = rhs_val;
                            uninitialized_locals.erase(stmt->expr->left->name);
                        } else {
                            // Other expression statements - try to evaluate them
                            // (they might have side effects we can't handle at compile time)
                            CTValue stmt_val;
                            if (!try_evaluate(stmt->expr, stmt_val)) {
                                constants = saved_constants;
                                uninitialized_locals = saved_uninitialized;
                                recursion_depth--;
                                return false;
                            }
                        }
                    } else if (stmt->kind == Stmt::Kind::VarDecl) {
                        // Handle variable declarations
                        if (stmt->var_init) {
                            CTValue init_val;
                            if (!try_evaluate(stmt->var_init, init_val)) {
                                constants = saved_constants;
                                uninitialized_locals = saved_uninitialized;
                                recursion_depth--;
                                return false;
                            }
                            constants[stmt->var_name] = init_val;
                            uninitialized_locals.erase(stmt->var_name);
                        } else {
                            uninitialized_locals.insert(stmt->var_name);
                        }
                    } else {
                        // Can't handle other statement types at compile time
                        error_msg = "Statement type not supported at compile time";
                        constants = saved_constants;
                        uninitialized_locals = saved_uninitialized;
                        recursion_depth--;
                        return false;
                    }
                }

                // Evaluate the result expression
                if (expr->result_expr) {
                    success = try_evaluate(expr->result_expr, result);
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                } else {
                    error_msg = "Block has no result expression";
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                    success = false;
                }
            }
            break;
        default:
            error_msg = "Expression kind not supported at compile time";
            success = false;
            break;
    }

    recursion_depth--;
    return success;
}

bool CompileTimeEvaluator::eval_literal(ExprPtr expr, CTValue& result) {
    switch (expr->kind) {
        case Expr::Kind::IntLiteral:
            result = (int64_t)expr->uint_val;
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
    if (!try_evaluate(expr->right, right_val)) return false;

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
    Symbol* sym = type_checker->get_scope()->lookup(func_name);

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

    // Check for reference parameters (mutation)
    if (!func->ref_params.empty()) {
        error_msg = "Functions with reference parameters cannot be evaluated at compile time";
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
    for (size_t i = 0; i < expr->args.size(); i++) {
        CTValue arg_val;
        if (!try_evaluate(expr->args[i], arg_val)) {
            constants = saved_constants;
            return false;
        }
        constants[func->params[i].name] = arg_val;
    }

    // Evaluate function body
    if (!func->body) {
        error_msg = "Function has no body";
        constants = saved_constants;
        return false;
    }

    bool success = try_evaluate(func->body, result);
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
    Symbol* sym = type_checker->get_scope()->lookup(expr->name);
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
    Symbol* sym = type_checker->get_scope()->lookup(type_name);

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

    CTComposite composite;
    composite.type_name = type_name;

    for (size_t i = 0; i < expr->args.size(); i++) {
        CTValue arg_val;

        // Try to evaluate the argument
        if (!try_evaluate(expr->args[i], arg_val)) {
            // If we can't evaluate it (e.g., it's an array or identifier pointing to array),
            // store a placeholder value of 0 for pointer/array types
            // This allows us to still track scalar fields like len, cap
            std::string field_name = type_decl->fields[i].name;
            composite.fields[field_name] = (int64_t)0;
            continue;
        }

        // Store the field value
        std::string field_name = type_decl->fields[i].name;

        // Convert CTValue to the field storage type
        if (std::holds_alternative<int64_t>(arg_val)) {
            composite.fields[field_name] = std::get<int64_t>(arg_val);
        } else if (std::holds_alternative<uint64_t>(arg_val)) {
            composite.fields[field_name] = std::get<uint64_t>(arg_val);
        } else if (std::holds_alternative<double>(arg_val)) {
            composite.fields[field_name] = std::get<double>(arg_val);
        } else if (std::holds_alternative<bool>(arg_val)) {
            composite.fields[field_name] = std::get<bool>(arg_val);
        } else if (std::holds_alternative<std::string>(arg_val)) {
            composite.fields[field_name] = std::get<std::string>(arg_val);
        } else {
            // Unknown type, store 0 as placeholder
            composite.fields[field_name] = (int64_t)0;
        }
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
    if (!std::holds_alternative<CTComposite>(obj_val)) {
        error_msg = "Member access on non-composite value";
        return false;
    }

    const CTComposite& composite = std::get<CTComposite>(obj_val);

    // Look up the field
    auto it = composite.fields.find(expr->name);
    if (it == composite.fields.end()) {
        error_msg = "Field not found: " + expr->name;
        return false;
    }

    // Convert the field value to CTValue
    const auto& field_val = it->second;
    if (std::holds_alternative<int64_t>(field_val)) {
        result = std::get<int64_t>(field_val);
    } else if (std::holds_alternative<uint64_t>(field_val)) {
        result = std::get<uint64_t>(field_val);
    } else if (std::holds_alternative<double>(field_val)) {
        result = std::get<double>(field_val);
    } else if (std::holds_alternative<bool>(field_val)) {
        result = std::get<bool>(field_val);
    } else if (std::holds_alternative<std::string>(field_val)) {
        result = std::get<std::string>(field_val);
    } else {
        error_msg = "Unsupported field type";
        return false;
    }

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

    // If the left side is an identifier, check if it's a local or global
    if (expr->left->kind == Expr::Kind::Identifier) {
        // Check if this is a global mutable variable (not allowed at compile-time)
        Symbol* sym = type_checker->get_scope()->lookup(expr->left->name);
        if (sym && sym->kind == Symbol::Kind::Variable && sym->is_mutable) {
            error_msg = "Cannot modify mutable globals at compile time: " + expr->left->name;
            return false;
        }

        // Store as a local constant (or update if it doesn't exist globally)
        constants[expr->left->name] = rhs_val;
        result = rhs_val;
        return true;
    }

    // For other assignment targets (array indexing, member access),
    // we can't handle them at compile time
    error_msg = "Assignment to non-identifier not supported at compile time";
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
    bool pure = is_expr_pure(func->body, reason);
    purity_stack.erase(func.get());
    return pure;
}

bool CompileTimeEvaluator::is_expr_pure(ExprPtr expr, std::string& reason) {
    if (!expr) return true;

    switch (expr->kind) {
        case Expr::Kind::Assignment:
            // Check if assigning to a global mutable variable
            if (expr->left && expr->left->kind == Expr::Kind::Identifier) {
                Symbol* sym = type_checker->get_scope()->lookup(expr->left->name);
                if (sym && sym->kind == Symbol::Kind::Variable && sym->is_mutable) {
                    reason = "modifies mutable global variable '" + expr->left->name + "'";
                    return false;
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
        case Expr::Kind::Repeat:
            return is_expr_pure(expr->left, reason) && is_expr_pure(expr->right, reason);

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

} // namespace vexel
