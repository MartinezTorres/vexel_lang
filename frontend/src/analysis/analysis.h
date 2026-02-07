#pragma once
#include "ast.h"
#include "symbols.h"
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

struct Program;
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

// Shared data computed once per analysis run and reused across passes.
struct AnalysisRunSummary {
    Program* program = nullptr;
    std::unordered_map<const Symbol*, StmtPtr> reachable_function_decls;
    std::unordered_map<const Symbol*, std::unordered_set<const Symbol*>> reachable_calls;
    std::unordered_set<const Symbol*> runtime_initialized_globals;
    std::unordered_map<const Symbol*, std::unordered_set<const Symbol*>> global_initializer_calls;
};

class Analyzer {
public:
    explicit Analyzer(TypeChecker* tc, const OptimizationFacts* opt = nullptr)
        : type_checker(tc), optimization(opt) {}
    AnalysisFacts run(const Module& mod);

private:
    struct AnalysisContext {
        Program* program = nullptr;
    };

    using ExprVisitor = std::function<void(ExprPtr)>;
    using StmtVisitor = std::function<void(StmtPtr)>;

    class InstanceScope {
    public:
        InstanceScope(Analyzer& analyzer, int instance_id)
            : analyzer_(&analyzer), saved_instance_(analyzer.current_instance_id) {
            analyzer_->current_instance_id = instance_id;
        }

        ~InstanceScope() {
            if (analyzer_) {
                analyzer_->current_instance_id = saved_instance_;
            }
        }

        InstanceScope(const InstanceScope&) = delete;
        InstanceScope& operator=(const InstanceScope&) = delete;

        InstanceScope(InstanceScope&& other) noexcept
            : analyzer_(other.analyzer_), saved_instance_(other.saved_instance_) {
            other.analyzer_ = nullptr;
        }

    private:
        Analyzer* analyzer_;
        int saved_instance_;
    };

    TypeChecker* type_checker;
    const OptimizationFacts* optimization;
    int current_instance_id = -1;
    AnalysisRunSummary run_summary_;

    AnalysisContext context() const;
    const AnalysisRunSummary& run_summary() const { return run_summary_; }
    InstanceScope scoped_instance(int instance_id);
    bool is_foldable(const Symbol* func_sym) const;
    bool global_initializer_runs_at_runtime(const Symbol* sym) const;
    void build_run_summary(const AnalysisFacts& facts);
    std::optional<bool> constexpr_condition(ExprPtr expr) const;
    void walk_pruned_expr(ExprPtr expr, const ExprVisitor& on_expr, const StmtVisitor& on_stmt);
    void walk_pruned_stmt(StmtPtr stmt, const ExprVisitor& on_expr, const StmtVisitor& on_stmt);
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
