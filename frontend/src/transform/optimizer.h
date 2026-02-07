#pragma once
#include "evaluator.h"
#include "symbols.h"
#include <unordered_map>
#include <unordered_set>

namespace vexel {

class TypeChecker;

struct OptimizationFacts {
    std::unordered_map<const Expr*, CTValue> constexpr_values;
    std::unordered_set<const Stmt*> constexpr_inits;
    std::unordered_set<const Symbol*> foldable_functions;
    std::unordered_map<const Expr*, bool> constexpr_conditions;
    std::unordered_map<const Symbol*, std::string> fold_skip_reasons;
};

class Optimizer {
public:
    explicit Optimizer(TypeChecker* tc) : type_checker(tc) {}
    OptimizationFacts run(const Module& mod);

private:
    TypeChecker* type_checker;
    CompileTimeEvaluator* evaluator = nullptr;

    void visit_expr(ExprPtr expr, OptimizationFacts& facts);
    void visit_stmt(StmtPtr stmt, OptimizationFacts& facts);
    void mark_constexpr_init(StmtPtr stmt, OptimizationFacts& facts);
};

} // namespace vexel
