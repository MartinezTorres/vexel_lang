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

CTValue clone_value(const CTValue& value) {
    if (std::holds_alternative<CTUninitialized>(value)) {
        return CTUninitialized{};
    }
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

CTEQueryResult CompileTimeEvaluator::query(ExprPtr expr) {
    error_msg.clear();
    hard_error = false;

    CTEQueryResult out;
    CTValue value;
    if (try_evaluate(expr, value)) {
        out.status = CTEQueryStatus::Known;
        out.value = clone_value(value);
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
            success = eval_block_vm(expr, result);
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
        result = clone_value(cached->second);
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
        result = clone_value(coerced);
    }

    constant_value_cache[sym] = clone_value(result);
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

bool CompileTimeEvaluator::eval_block_vm(ExprPtr expr, CTValue& result) {
    if (!expr || expr->kind != Expr::Kind::Block) {
        return false;
    }

    enum class VmOpKind {
        EvalExpr,
        EvalDeclAssignment,
        DeclareVar,
        JumpIfFalse,
        Jump,
        ExitScope,
        ReturnExpr,
        BreakSignal,
        ContinueSignal,
    };

    struct VmOp {
        VmOpKind kind = VmOpKind::EvalExpr;
        ExprPtr expr;
        StmtPtr stmt;
        int target = -1;
        std::vector<std::string> scope_names;
    };

    struct VmLoopCtx {
        int cond_pc = -1;
        std::vector<int> break_jumps;
        std::vector<int> continue_jumps;
    };

    std::vector<VmOp> code;
    std::vector<std::vector<std::string>> scope_stack;
    std::vector<VmLoopCtx> loop_stack;

    auto begin_scope = [&]() {
        scope_stack.emplace_back();
    };

    auto register_decl = [&](const std::string& name) {
        if (!scope_stack.empty()) {
            scope_stack.back().push_back(name);
        }
    };

    auto end_scope = [&](std::vector<std::string>& names) {
        if (scope_stack.empty()) {
            names.clear();
            return;
        }
        names = std::move(scope_stack.back());
        scope_stack.pop_back();
    };

    auto add_op = [&](VmOp op) -> int {
        code.push_back(std::move(op));
        return static_cast<int>(code.size()) - 1;
    };

    std::function<bool(const ExprPtr&)> compile_expr_stmt;
    std::function<bool(const StmtPtr&)> compile_stmt;

    compile_expr_stmt = [&](const ExprPtr& e) -> bool {
        if (!e) return true;
        if (e->kind == Expr::Kind::Block) {
            begin_scope();
            for (const auto& st : e->statements) {
                if (!compile_stmt(st)) {
                    return false;
                }
            }
            if (e->result_expr) {
                VmOp result_op;
                result_op.kind = VmOpKind::EvalExpr;
                result_op.expr = e->result_expr;
                add_op(std::move(result_op));
            }
            VmOp exit_op;
            exit_op.kind = VmOpKind::ExitScope;
            end_scope(exit_op.scope_names);
            add_op(std::move(exit_op));
            return true;
        }

        if (e->kind == Expr::Kind::Repeat) {
            if (!e->condition || !e->right) {
                return false;
            }
            VmOp cond_op;
            cond_op.kind = VmOpKind::JumpIfFalse;
            cond_op.expr = e->condition;
            int cond_pc = add_op(std::move(cond_op));

            loop_stack.push_back(VmLoopCtx{cond_pc, {}, {}});
            if (!compile_expr_stmt(e->right)) {
                return false;
            }

            for (int pc : loop_stack.back().continue_jumps) {
                code[static_cast<size_t>(pc)].target = cond_pc;
            }

            VmOp back_op;
            back_op.kind = VmOpKind::Jump;
            back_op.target = cond_pc;
            add_op(std::move(back_op));

            int end_pc = static_cast<int>(code.size());
            code[static_cast<size_t>(cond_pc)].target = end_pc;
            for (int pc : loop_stack.back().break_jumps) {
                code[static_cast<size_t>(pc)].target = end_pc;
            }
            loop_stack.pop_back();
            return true;
        }

        if (e->kind == Expr::Kind::Assignment &&
            e->creates_new_variable &&
            e->left &&
            e->left->kind == Expr::Kind::Identifier) {
            register_decl(e->left->name);
            VmOp op;
            op.kind = VmOpKind::EvalDeclAssignment;
            op.expr = e;
            add_op(std::move(op));
            return true;
        }

        VmOp op;
        op.kind = VmOpKind::EvalExpr;
        op.expr = e;
        add_op(std::move(op));
        return true;
    };

    compile_stmt = [&](const StmtPtr& stmt) -> bool {
        if (!stmt) return true;
        switch (stmt->kind) {
            case Stmt::Kind::Expr:
                return compile_expr_stmt(stmt->expr);
            case Stmt::Kind::VarDecl: {
                register_decl(stmt->var_name);
                VmOp op;
                op.kind = VmOpKind::DeclareVar;
                op.stmt = stmt;
                add_op(std::move(op));
                return true;
            }
            case Stmt::Kind::ConditionalStmt: {
                VmOp cond_op;
                cond_op.kind = VmOpKind::JumpIfFalse;
                cond_op.expr = stmt->condition;
                int cond_pc = add_op(std::move(cond_op));
                if (!compile_stmt(stmt->true_stmt)) {
                    return false;
                }
                code[static_cast<size_t>(cond_pc)].target = static_cast<int>(code.size());
                return true;
            }
            case Stmt::Kind::Return: {
                if (!stmt->return_expr) {
                    error_msg = "Return statement requires an expression at compile time";
                    return false;
                }
                VmOp op;
                op.kind = VmOpKind::ReturnExpr;
                op.expr = stmt->return_expr;
                add_op(std::move(op));
                return true;
            }
            case Stmt::Kind::Break: {
                if (loop_stack.empty()) {
                    if (loop_depth > 0) {
                        VmOp op;
                        op.kind = VmOpKind::BreakSignal;
                        add_op(std::move(op));
                        return true;
                    }
                    error_msg = "Break used outside of loop in compile-time evaluation";
                    return false;
                }
                VmOp op;
                op.kind = VmOpKind::Jump;
                int pc = add_op(std::move(op));
                loop_stack.back().break_jumps.push_back(pc);
                return true;
            }
            case Stmt::Kind::Continue: {
                if (loop_stack.empty()) {
                    if (loop_depth > 0) {
                        VmOp op;
                        op.kind = VmOpKind::ContinueSignal;
                        add_op(std::move(op));
                        return true;
                    }
                    error_msg = "Continue used outside of loop in compile-time evaluation";
                    return false;
                }
                VmOp op;
                op.kind = VmOpKind::Jump;
                int pc = add_op(std::move(op));
                loop_stack.back().continue_jumps.push_back(pc);
                return true;
            }
            default:
                return false;
        }
    };

    begin_scope();
    for (const auto& stmt : expr->statements) {
        if (!compile_stmt(stmt)) {
            return false;
        }
    }
    std::vector<std::string> top_scope_names;
    end_scope(top_scope_names);
    auto saved_constants = constants;
    auto saved_uninitialized = uninitialized_locals;

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

    int pc = 0;
    int64_t vm_steps = 0;
    std::unordered_map<int, int> back_edge_counts;
    while (pc >= 0 && static_cast<size_t>(pc) < code.size()) {
        if (++vm_steps > static_cast<int64_t>(MAX_LOOP_ITERATIONS) * 64) {
            error_msg = "VM step limit exceeded in compile-time evaluation";
            constants = saved_constants;
            uninitialized_locals = saved_uninitialized;
            return false;
        }
        const VmOp& op = code[static_cast<size_t>(pc)];
        switch (op.kind) {
            case VmOpKind::EvalExpr: {
                CTValue stmt_val;
                if (!try_evaluate(op.expr, stmt_val)) {
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                    return false;
                }
                pc++;
                break;
            }
            case VmOpKind::EvalDeclAssignment: {
                CTValue stmt_val;
                if (!try_evaluate(op.expr, stmt_val)) {
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                    return false;
                }
                pc++;
                break;
            }
            case VmOpKind::DeclareVar: {
                if (op.stmt && op.stmt->var_init) {
                    CTValue init_val;
                    if (!try_evaluate(op.stmt->var_init, init_val)) {
                        constants = saved_constants;
                        uninitialized_locals = saved_uninitialized;
                        return false;
                    }
                    CTValue stored_val = clone_value(init_val);
                    if (op.stmt->var_type &&
                        !coerce_value_to_type(stored_val, op.stmt->var_type, stored_val)) {
                        constants = saved_constants;
                        uninitialized_locals = saved_uninitialized;
                        return false;
                    }
                    constants[op.stmt->var_name] = clone_value(stored_val);
                    uninitialized_locals.erase(op.stmt->var_name);
                } else if (!declare_uninitialized_local(op.stmt)) {
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                    return false;
                }
                pc++;
                break;
            }
            case VmOpKind::JumpIfFalse: {
                CTValue cond_val;
                if (!try_evaluate(op.expr, cond_val)) {
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                    return false;
                }
                bool is_true = false;
                if (!scalar_to_bool(cond_val, is_true)) {
                    error_msg = "Conditional expression condition must be a scalar value";
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                    return false;
                }
                pc = is_true ? (pc + 1) : op.target;
                break;
            }
            case VmOpKind::Jump:
                if (op.target <= pc) {
                    int& iter_count = back_edge_counts[op.target];
                    iter_count++;
                    if (iter_count > MAX_LOOP_ITERATIONS) {
                        error_msg = "Loop iteration limit exceeded in compile-time evaluation";
                        constants = saved_constants;
                        uninitialized_locals = saved_uninitialized;
                        return false;
                    }
                }
                pc = op.target;
                break;
            case VmOpKind::ExitScope:
                for (const auto& name : op.scope_names) {
                    constants.erase(name);
                    uninitialized_locals.erase(name);
                }
                pc++;
                break;
            case VmOpKind::ReturnExpr: {
                CTValue ret_val;
                if (!try_evaluate(op.expr, ret_val)) {
                    constants = saved_constants;
                    uninitialized_locals = saved_uninitialized;
                    return false;
                }
                throw EvalReturn{clone_value(ret_val)};
            }
            case VmOpKind::BreakSignal:
                throw EvalBreak{};
            case VmOpKind::ContinueSignal:
                throw EvalContinue{};
        }
    }

    if (expr->result_expr) {
        if (!try_evaluate(expr->result_expr, result)) {
            constants = saved_constants;
            uninitialized_locals = saved_uninitialized;
            return false;
        }
    } else {
        result = static_cast<int64_t>(0);
    }

    for (const auto& name : top_scope_names) {
        constants.erase(name);
        uninitialized_locals.erase(name);
    }
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
            if (i < func->ref_param_types.size() && func->ref_param_types[i]) {
                CTValue coerced;
                if (!coerce_value_to_type(rec_val, func->ref_param_types[i], coerced)) {
                    constants = saved_constants;
                    return false;
                }
                constants[func->ref_params[i]] = clone_value(coerced);
            } else {
                constants[func->ref_params[i]] = clone_value(rec_val);
            }
        }
    }
    for (size_t i = 0; i < expr->args.size(); i++) {
        CTValue arg_val;
        if (!try_evaluate(expr->args[i], arg_val)) {
            constants = saved_constants;
            return false;
        }
        if (i < func->params.size() && func->params[i].type) {
            CTValue coerced;
            if (!coerce_value_to_type(arg_val, func->params[i].type, coerced)) {
                constants = saved_constants;
                return false;
            }
            constants[func->params[i].name] = clone_value(coerced);
        } else {
            constants[func->params[i].name] = clone_value(arg_val);
        }
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
                        CTValue coerced_field = clone_value(it->second);
                        if (func->return_types[i] &&
                            !coerce_value_to_type(it->second, func->return_types[i], coerced_field)) {
                            success = false;
                            break;
                        }
                        out_comp->fields[field_name] = clone_value(coerced_field);
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
                result = clone_value(coerced_result);
            }
        }
    }
    pop_ref_params();
    constants = saved_constants;
    return success;
}

bool CompileTimeEvaluator::eval_identifier(ExprPtr expr, CTValue& result) {
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
    Symbol* sym = type_checker ? type_checker->binding_for(expr.get()) : nullptr;
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
            result = clone_value(known->second);
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

        // Type constructors are only constexpr when every field argument is constexpr.
        if (!try_evaluate(expr->args[i], arg_val)) {
            return false;
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
        output = clone_value(input);
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
                coerced_elem = clone_value(elem);
            }
            out_array->elements.push_back(clone_value(coerced_elem));
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
                out_comp->fields[field.name] = clone_value(coerced_field);
            }
            output = out_comp;
            return true;
        }

        const bool is_internal_tuple_type =
            target_type->type_name.rfind("__Tuple", 0) == 0;
        if (is_internal_tuple_type && in_comp->type_name == target_type->type_name) {
            // Lowered tuple temporaries are compiler-internal named composites.
            // Keep strict behavior for user named types, but allow exact tuple passthrough.
            output = clone_value(input);
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
        Symbol* sym = type_checker ? type_checker->binding_for(lvalue.get()) : nullptr;
        if (!sym && type_checker && type_checker->get_scope()) {
            sym = type_checker->get_scope()->lookup(lvalue->name);
        }
        if (sym) {
            target_type = sym->type;
        }
    }
    if (!target_type) {
        output = clone_value(input);
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
            dst->fields[entry.first] = clone_value(entry.second);
        }
        return dst;
    };
    auto clone_array = [&](const std::shared_ptr<CTArray>& src) {
        if (!src) return std::shared_ptr<CTArray>();
        auto dst = std::make_shared<CTArray>();
        dst->elements.reserve(src->elements.size());
        for (const auto& entry : src->elements) {
            dst->elements.push_back(clone_value(entry));
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
                auto comp = std::get<std::shared_ptr<CTComposite>>(*base_slot);
                if (!comp) {
                    error_msg = "Member access on null composite value";
                    return false;
                }
                if (comp.use_count() > 1) {
                    auto unique_comp = clone_composite(comp);
                    *base_slot = unique_comp;
                    comp = unique_comp;
                }
                auto it = comp->fields.find(target->name);
                if (it == comp->fields.end()) {
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
                auto array = std::get<std::shared_ptr<CTArray>>(*base_slot);
                if (!array) {
                    error_msg = "Indexing null array";
                    return false;
                }
                if (array.use_count() > 1) {
                    auto unique_array = clone_array(array);
                    *base_slot = unique_array;
                    array = unique_array;
                }
                int64_t idx = 0;
                if (!parse_index(target->args[0], idx)) return false;
                if (static_cast<size_t>(idx) >= array->elements.size()) {
                    error_msg = "Index out of bounds in compile-time evaluation";
                    return false;
                }
                out = &array->elements[static_cast<size_t>(idx)];
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
    *slot = clone_value(stored_val);
    if (expr->left && expr->left->kind == Expr::Kind::Identifier) {
        uninitialized_locals.erase(expr->left->name);
    }
    result = clone_value(stored_val);
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
