#pragma once
#include "ast.h"
#include "symbols.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

class TypeChecker;
struct OptimizationFacts;

enum class VarMutability { Mutable, NonMutableRuntime, Constexpr };

struct AnalysisFacts {
    std::unordered_set<const Symbol*> reachable_functions;
    std::unordered_map<const Symbol*, VarMutability> var_mutability;
    std::unordered_map<const Symbol*, std::vector<bool>> receiver_mutates;
    std::unordered_map<const Symbol*, std::unordered_set<std::string>> ref_variants;
    std::unordered_map<const Symbol*, bool> function_writes_global;
    std::unordered_map<const Symbol*, bool> function_is_pure;
    std::unordered_set<const Symbol*> used_global_vars;
    std::unordered_set<std::string> used_type_names;
    std::unordered_map<const Symbol*, std::unordered_set<char>> reentrancy_variants;
};

class Analyzer {
public:
    explicit Analyzer(TypeChecker* tc, const OptimizationFacts* opt = nullptr)
        : type_checker(tc), optimization(opt) {}
    AnalysisFacts run(const Module& mod);

private:
    TypeChecker* type_checker;
    const OptimizationFacts* optimization;
    int current_instance_id = -1;

    bool is_foldable(const Symbol* func_sym) const;
    std::optional<bool> constexpr_condition(ExprPtr expr) const;
    void analyze_reachability(const Module& mod, AnalysisFacts& facts);
    void analyze_reentrancy(const Module& mod, AnalysisFacts& facts);
    void analyze_mutability(const Module& mod, AnalysisFacts& facts);
    void analyze_ref_variants(const Module& mod, AnalysisFacts& facts);
    void analyze_effects(const Module& mod, AnalysisFacts& facts);
    void analyze_usage(const Module& mod, AnalysisFacts& facts);

    void mark_reachable(const Symbol* func_sym,
                        const Module& mod, AnalysisFacts& facts);
    void collect_calls(ExprPtr expr, std::unordered_set<const Symbol*>& calls);

    Symbol* binding_for(ExprPtr expr) const;
    std::optional<const Symbol*> base_identifier_symbol(ExprPtr expr) const;
    bool is_addressable_lvalue(ExprPtr expr) const;
    bool is_mutable_lvalue(ExprPtr expr) const;
    bool receiver_is_mutable_arg(ExprPtr expr) const;
};

} // namespace vexel
