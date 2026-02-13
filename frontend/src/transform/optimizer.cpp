#include "optimizer.h"
#include "expr_access.h"
#include "typechecker.h"

#include <optional>

namespace vexel {

OptimizationFacts Optimizer::run(const Module& mod) {
    OptimizationFacts facts;
    if (!type_checker) {
        return facts;
    }

    Program* program = type_checker->get_program();
    int saved_instance = type_checker->current_instance();
    if (program) {
        for (const auto& instance : program->instances) {
            type_checker->set_current_instance(instance.id);
            const Module& module = program->modules[static_cast<size_t>(instance.module_id)].module;

            // Precompute foldable functions for this instance.
            for (const auto& pair : instance.symbols) {
                const Symbol* sym = pair.second;
                if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) continue;
                if (sym->is_external || !sym->declaration->body) {
                    facts.fold_skip_reasons[sym] = "external-or-no-body";
                    continue;
                }
                if (!sym->declaration->params.empty()) {
                    facts.fold_skip_reasons[sym] = "parameterized";
                    continue;
                }
                if (!sym->declaration->ref_params.empty()) {
                    facts.fold_skip_reasons[sym] = "has-receivers";
                    continue;
                }

                CompileTimeEvaluator func_eval(type_checker);
                CTValue result;
                if (!func_eval.try_evaluate(sym->declaration->body, result)) {
                    std::string reason = func_eval.get_error();
                    if (reason.empty()) reason = "unknown";
                    facts.fold_skip_reasons[sym] = "evaluation-failed: " + reason;
                    continue;
                }
                bool scalar = std::holds_alternative<int64_t>(result) ||
                              std::holds_alternative<uint64_t>(result) ||
                              std::holds_alternative<bool>(result) ||
                              std::holds_alternative<double>(result);
                if (!scalar) {
                    facts.fold_skip_reasons[sym] = "non-scalar-result";
                    continue;
                }
                facts.foldable_functions.insert(sym);
                facts.fold_skip_reasons.erase(sym);
            }

            for (const auto& stmt : module.top_level) {
                visit_stmt(stmt, facts);
            }
        }
    } else {
        for (const auto& stmt : mod.top_level) {
            visit_stmt(stmt, facts);
        }
    }
    type_checker->set_current_instance(saved_instance);

    return facts;
}

static std::optional<bool> evaluate_condition(ExprPtr expr, TypeChecker* type_checker) {
    if (!expr || !type_checker) return std::nullopt;
    CompileTimeEvaluator evaluator(type_checker);
    CTValue result;
    if (!evaluator.try_evaluate(expr, result)) {
        return std::nullopt;
    }
    if (std::holds_alternative<int64_t>(result)) {
        return std::get<int64_t>(result) != 0;
    }
    if (std::holds_alternative<uint64_t>(result)) {
        return std::get<uint64_t>(result) != 0;
    }
    if (std::holds_alternative<bool>(result)) {
        return std::get<bool>(result);
    }
    if (std::holds_alternative<double>(result)) {
        return std::get<double>(result) != 0.0;
    }
    return std::nullopt;
}

void Optimizer::mark_constexpr_init(StmtPtr stmt, OptimizationFacts& facts) {
    if (!stmt || stmt->kind != Stmt::Kind::VarDecl || !stmt->var_init) return;
    if (!type_checker) return;
    CompileTimeEvaluator evaluator(type_checker);
    CTValue result;
    if (evaluator.try_evaluate(stmt->var_init, result)) {
        facts.constexpr_inits.insert(stmt.get());
        facts.constexpr_values.emplace(stmt->var_init.get(), result);
    }
}

void Optimizer::visit_stmt(StmtPtr stmt, OptimizationFacts& facts) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            if (!stmt->is_external) {
                visit_expr(stmt->body, facts);
            }
            break;
        case Stmt::Kind::VarDecl:
            mark_constexpr_init(stmt, facts);
            visit_expr(stmt->var_init, facts);
            break;
        case Stmt::Kind::Expr:
            visit_expr(stmt->expr, facts);
            break;
        case Stmt::Kind::Return:
            visit_expr(stmt->return_expr, facts);
            break;
        case Stmt::Kind::ConditionalStmt:
            if (auto cond = evaluate_condition(stmt->condition, type_checker)) {
                facts.constexpr_conditions[stmt->condition.get()] = cond.value();
            }
            visit_expr(stmt->condition, facts);
            visit_stmt(stmt->true_stmt, facts);
            break;
        default:
            break;
    }
}

void Optimizer::visit_expr(ExprPtr expr, OptimizationFacts& facts) {
    if (!expr) return;

    if (type_checker && !facts.constexpr_values.count(expr.get())) {
        CompileTimeEvaluator evaluator(type_checker);
        CTValue value;
        if (evaluator.try_evaluate(expr, value)) {
            facts.constexpr_values.emplace(expr.get(), value);
        }
    }

    switch (expr->kind) {
        case Expr::Kind::Conditional:
            if (auto cond = evaluate_condition(expr->condition, type_checker)) {
                facts.constexpr_conditions[expr->condition.get()] = cond.value();
            }
            visit_expr(expr->condition, facts);
            visit_expr(expr->true_expr, facts);
            visit_expr(expr->false_expr, facts);
            break;
        case Expr::Kind::Call:
            visit_expr(expr->operand, facts);
            for (const auto& rec : expr->receivers) visit_expr(rec, facts);
            for (const auto& arg : expr->args) visit_expr(arg, facts);
            break;
        case Expr::Kind::Binary:
        case Expr::Kind::Assignment:
        case Expr::Kind::Range:
            visit_expr(expr->left, facts);
            visit_expr(expr->right, facts);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
        case Expr::Kind::Member:
            visit_expr(expr->operand, facts);
            break;
        case Expr::Kind::Index:
            visit_expr(expr->operand, facts);
            if (!expr->args.empty()) visit_expr(expr->args[0], facts);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) visit_expr(elem, facts);
            break;
        case Expr::Kind::Block:
            for (const auto& st : expr->statements) visit_stmt(st, facts);
            visit_expr(expr->result_expr, facts);
            break;
        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
            visit_expr(loop_subject(expr), facts);
            visit_expr(loop_body(expr), facts);
            break;
        default:
            break;
    }
}

} // namespace vexel
