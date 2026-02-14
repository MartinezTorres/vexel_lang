#include "cte_engine.h"

#include "evaluator.h"
#include "typechecker.h"

namespace vexel {

CTEEngine::CTEEngine(TypeChecker* checker)
    : type_checker_(checker),
      evaluator_(std::make_unique<CompileTimeEvaluator>(checker)) {}

void CTEEngine::prepare_query(const std::unordered_map<const Symbol*, CTValue>& symbol_constants,
                              ExprValueObserver value_observer,
                              SymbolReadObserver symbol_read_observer) {
    evaluator_->reset_state();
    for (const auto& entry : symbol_constants) {
        evaluator_->set_symbol_constant(entry.first, entry.second);
    }
    evaluator_->set_value_observer(std::move(value_observer));
    evaluator_->set_symbol_read_observer(std::move(symbol_read_observer));
}

CTEQueryResult CTEEngine::query(int instance_id,
                                ExprPtr expr,
                                const std::unordered_map<const Symbol*, CTValue>& symbol_constants,
                                ExprValueObserver value_observer,
                                SymbolReadObserver symbol_read_observer) {
    if (!evaluator_) {
        return CTEQueryResult{};
    }

    if (type_checker_) {
        auto scope = type_checker_->scoped_instance(instance_id);
        (void)scope;
        prepare_query(symbol_constants, std::move(value_observer), std::move(symbol_read_observer));
        return evaluator_->query(expr);
    }

    prepare_query(symbol_constants, std::move(value_observer), std::move(symbol_read_observer));
    return evaluator_->query(expr);
}

bool CTEEngine::try_evaluate(int instance_id,
                             ExprPtr expr,
                             CTValue& out,
                             const std::unordered_map<const Symbol*, CTValue>& symbol_constants,
                             ExprValueObserver value_observer,
                             SymbolReadObserver symbol_read_observer) {
    if (!evaluator_) {
        return false;
    }

    if (type_checker_) {
        auto scope = type_checker_->scoped_instance(instance_id);
        (void)scope;
        prepare_query(symbol_constants, std::move(value_observer), std::move(symbol_read_observer));
        return evaluator_->try_evaluate(expr, out);
    }

    prepare_query(symbol_constants, std::move(value_observer), std::move(symbol_read_observer));
    return evaluator_->try_evaluate(expr, out);
}

} // namespace vexel
