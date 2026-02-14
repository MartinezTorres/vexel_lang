#pragma once

#include "evaluator.h"
#include "cte_value.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace vexel {

class TypeChecker;
struct Expr;
struct Symbol;
using ExprPtr = std::shared_ptr<Expr>;

// Canonical frontend service for compile-time expression queries.
// Callers seed symbol constants and optional trace observers per query.
class CTEEngine {
public:
    using ExprValueObserver = std::function<void(const Expr*, const CTValue&)>;
    using SymbolReadObserver = std::function<void(const Symbol*)>;

    explicit CTEEngine(TypeChecker* checker);

    CTEQueryResult query(int instance_id,
                         ExprPtr expr,
                         const std::unordered_map<const Symbol*, CTValue>& symbol_constants,
                         ExprValueObserver value_observer = {},
                         SymbolReadObserver symbol_read_observer = {});

    bool try_evaluate(int instance_id,
                      ExprPtr expr,
                      CTValue& out,
                      const std::unordered_map<const Symbol*, CTValue>& symbol_constants,
                      ExprValueObserver value_observer = {},
                      SymbolReadObserver symbol_read_observer = {});

private:
    void prepare_query(const std::unordered_map<const Symbol*, CTValue>& symbol_constants,
                       ExprValueObserver value_observer,
                       SymbolReadObserver symbol_read_observer);

    TypeChecker* type_checker_ = nullptr;
    std::unique_ptr<CompileTimeEvaluator> evaluator_;
};

} // namespace vexel
