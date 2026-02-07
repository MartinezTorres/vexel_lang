#include "analysis.h"

#include "evaluator.h"
#include "expr_access.h"
#include "typechecker.h"

#include <functional>

namespace vexel {

void Analyzer::analyze_ref_variants(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.ref_variants.clear();

    Program* program = type_checker ? type_checker->get_program() : nullptr;
    if (!program) return;

    std::unordered_map<const Symbol*, StmtPtr> function_map;
    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) continue;
            function_map[sym] = sym->declaration;
        }
    }

    auto ref_variant_key = [&](const ExprPtr& call, size_t ref_count) {
        std::string key;
        key.reserve(ref_count);
        for (size_t i = 0; i < ref_count; i++) {
            bool is_mut = false;
            if (call && i < call->receivers.size()) {
                is_mut = receiver_is_mutable_arg(call->receivers[i]);
            }
            key.push_back(is_mut ? 'M' : 'N');
        }
        return key;
    };

    auto record_call = [&](ExprPtr expr) {
        if (!expr || expr->kind != Expr::Kind::Call) return;
        if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) return;
        Symbol* callee = binding_for(expr->operand);
        if (!callee) return;
        auto fit = function_map.find(callee);
        if (fit == function_map.end()) return;
        size_t ref_count = fit->second->ref_params.size();
        if (ref_count == 0) return;
        std::string key = ref_variant_key(expr, ref_count);
        facts.ref_variants[callee].insert(key);
    };

    std::function<void(ExprPtr)> visit_expr;
    std::function<void(StmtPtr)> visit_stmt;

    visit_expr = [&](ExprPtr expr) {
        if (!expr) return;
        switch (expr->kind) {
            case Expr::Kind::Call:
                record_call(expr);
                for (const auto& rec : expr->receivers) visit_expr(rec);
                for (const auto& arg : expr->args) visit_expr(arg);
                visit_expr(expr->operand);
                break;
            case Expr::Kind::Binary:
                visit_expr(expr->left);
                visit_expr(expr->right);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                visit_expr(expr->operand);
                break;
            case Expr::Kind::Index:
                visit_expr(expr->operand);
                if (!expr->args.empty()) visit_expr(expr->args[0]);
                break;
            case Expr::Kind::Member:
                visit_expr(expr->operand);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) visit_expr(elem);
                break;
            case Expr::Kind::Block:
                for (const auto& stmt : expr->statements) visit_stmt(stmt);
                visit_expr(expr->result_expr);
                break;
            case Expr::Kind::Conditional:
                if (auto cond = constexpr_condition(expr->condition)) {
                    if (cond.value()) {
                        visit_expr(expr->true_expr);
                    } else if (expr->false_expr) {
                        visit_expr(expr->false_expr);
                    }
                } else {
                    visit_expr(expr->condition);
                    visit_expr(expr->true_expr);
                    visit_expr(expr->false_expr);
                }
                break;
            case Expr::Kind::Range:
                visit_expr(expr->left);
                visit_expr(expr->right);
                break;
            case Expr::Kind::Iteration:
            case Expr::Kind::Repeat:
                visit_expr(loop_subject(expr));
                visit_expr(loop_body(expr));
                break;
            case Expr::Kind::Assignment:
                visit_expr(expr->left);
                visit_expr(expr->right);
                break;
            default:
                break;
        }
    };

    visit_stmt = [&](StmtPtr stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::Expr:
                visit_expr(stmt->expr);
                break;
            case Stmt::Kind::Return:
                visit_expr(stmt->return_expr);
                break;
            case Stmt::Kind::VarDecl:
                visit_expr(stmt->var_init);
                break;
            case Stmt::Kind::ConditionalStmt:
                if (auto cond = constexpr_condition(stmt->condition)) {
                    if (cond.value()) {
                        visit_stmt(stmt->true_stmt);
                    }
                } else {
                    visit_expr(stmt->condition);
                    visit_stmt(stmt->true_stmt);
                }
                break;
            default:
                break;
        }
    };

    for (const auto& func_sym : facts.reachable_functions) {
        if (!func_sym || !func_sym->declaration) continue;
        if (is_foldable(func_sym)) continue;
        current_instance_id = func_sym->instance_id;
        if (func_sym->declaration->body) {
            visit_expr(func_sym->declaration->body);
        }
    }

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Constant)) continue;
            if (!sym->declaration || !sym->declaration->var_init) continue;
            bool evaluated_at_compile_time = false;
            if (type_checker) {
                CompileTimeEvaluator evaluator(type_checker);
                CTValue result;
                if (evaluator.try_evaluate(sym->declaration->var_init, result)) {
                    evaluated_at_compile_time = true;
                }
            }
            if (!evaluated_at_compile_time) {
                visit_expr(sym->declaration->var_init);
            }
        }
    }
}

} // namespace vexel
