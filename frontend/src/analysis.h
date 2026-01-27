#pragma once
#include "ast.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

class TypeChecker;

enum class VarMutability { Mutable, NonMutableRuntime, Constexpr };

struct AnalysisFacts {
    std::unordered_set<std::string> reachable_functions;
    std::unordered_map<const Stmt*, VarMutability> var_mutability;
    std::unordered_map<std::string, std::vector<bool>> receiver_mutates;
    std::unordered_map<std::string, std::unordered_set<std::string>> ref_variants;
    std::unordered_map<std::string, bool> function_writes_global;
    std::unordered_map<std::string, bool> function_is_pure;
    std::unordered_set<const Stmt*> used_global_vars;
    std::unordered_set<std::string> used_type_names;
    std::unordered_map<std::string, std::unordered_set<char>> reentrancy_variants;
};

class Analyzer {
public:
    explicit Analyzer(TypeChecker* tc) : type_checker(tc) {}
    AnalysisFacts run(const Module& mod);

private:
    TypeChecker* type_checker;

    void analyze_reachability(const Module& mod, AnalysisFacts& facts);
    void analyze_reentrancy(const Module& mod, AnalysisFacts& facts);
    void analyze_mutability(const Module& mod, AnalysisFacts& facts);
    void analyze_ref_variants(const Module& mod, AnalysisFacts& facts);
    void analyze_effects(const Module& mod, AnalysisFacts& facts);
    void analyze_usage(const Module& mod, AnalysisFacts& facts);

    void mark_reachable(const std::string& func_name, int scope_id,
                        const Module& mod, AnalysisFacts& facts);
    void collect_calls(ExprPtr expr, std::unordered_set<std::string>& calls);
};

} // namespace vexel
