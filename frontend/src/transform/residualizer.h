#pragma once

#include "ast.h"
#include "optimizer.h"
#include <optional>

namespace vexel {

struct Program;

// Rewrites expression/statement trees in place using optimizer facts.
// This pass performs sub-expression residualization:
// - replace compile-time-known expressions with literals
// - prune compile-time-known conditional branches
// - drop dead pure expression statements
class Residualizer {
public:
    explicit Residualizer(const OptimizationFacts& facts) : facts_(facts) {}

    bool run(Module& mod, const Program* program = nullptr);

private:
    const OptimizationFacts& facts_;
    bool changed_ = false;
    int current_instance_id_ = -1;

    StmtPtr rewrite_stmt(StmtPtr stmt, bool top_level);
    ExprPtr rewrite_expr(ExprPtr expr, bool allow_fold = true);
    void rewrite_stmt_list(std::vector<StmtPtr>& stmts, bool top_level);

    bool should_drop_expr_stmt(const ExprPtr& expr) const;
    bool is_pure_expr(const ExprPtr& expr) const;
    static bool is_terminal_stmt(const StmtPtr& stmt);

    static void copy_expr_meta(const ExprPtr& from, const ExprPtr& to);
    bool can_fold_expr(const ExprPtr& expr) const;
    ExprPtr ctvalue_to_expr(const CTValue& value, const ExprPtr& origin) const;
    std::optional<bool> constexpr_condition(const ExprPtr& cond, const Expr* original) const;
};

} // namespace vexel
