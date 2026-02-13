#pragma once
#include "cte_value.h"
#include "symbols.h"
#include <unordered_map>
#include <unordered_set>

namespace vexel {

class TypeChecker;

struct ExprFactKey {
    int instance_id = -1;
    const Expr* expr = nullptr;

    bool operator==(const ExprFactKey& other) const {
        return instance_id == other.instance_id && expr == other.expr;
    }
};

struct ExprFactKeyHash {
    std::size_t operator()(const ExprFactKey& key) const noexcept {
        std::size_t h1 = std::hash<int>{}(key.instance_id);
        std::size_t h2 = std::hash<const Expr*>{}(key.expr);
        return h1 ^ (h2 + 0x9e3779b9ULL + (h1 << 6) + (h1 >> 2));
    }
};

struct StmtFactKey {
    int instance_id = -1;
    const Stmt* stmt = nullptr;

    bool operator==(const StmtFactKey& other) const {
        return instance_id == other.instance_id && stmt == other.stmt;
    }
};

struct StmtFactKeyHash {
    std::size_t operator()(const StmtFactKey& key) const noexcept {
        std::size_t h1 = std::hash<int>{}(key.instance_id);
        std::size_t h2 = std::hash<const Stmt*>{}(key.stmt);
        return h1 ^ (h2 + 0x9e3779b9ULL + (h1 << 6) + (h1 >> 2));
    }
};

inline ExprFactKey expr_fact_key(int instance_id, const Expr* expr) {
    return ExprFactKey{instance_id, expr};
}

inline StmtFactKey stmt_fact_key(int instance_id, const Stmt* stmt) {
    return StmtFactKey{instance_id, stmt};
}

struct OptimizationFacts {
    std::unordered_map<ExprFactKey, CTValue, ExprFactKeyHash> constexpr_values;
    std::unordered_set<StmtFactKey, StmtFactKeyHash> constexpr_inits;
    std::unordered_set<const Symbol*> foldable_functions;
    std::unordered_map<ExprFactKey, bool, ExprFactKeyHash> constexpr_conditions;
    std::unordered_map<const Symbol*, std::string> fold_skip_reasons;
};

class Optimizer {
public:
    explicit Optimizer(TypeChecker* tc) : type_checker(tc) {}
    OptimizationFacts run(const Module& mod);

private:
    TypeChecker* type_checker;
};

} // namespace vexel
