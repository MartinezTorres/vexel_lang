#pragma once
#include "ast.h"
#include "symbols.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vexel {

struct Program;
class TypeChecker;
struct OptimizationFacts;

enum class AnalysisPass : uint32_t {
    Reachability = 1u << 0,
    Reentrancy = 1u << 1,
    Mutability = 1u << 2,
    RefVariants = 1u << 3,
    Effects = 1u << 4,
    Usage = 1u << 5,
};

constexpr uint32_t kAllAnalysisPasses =
    static_cast<uint32_t>(AnalysisPass::Reachability) |
    static_cast<uint32_t>(AnalysisPass::Reentrancy) |
    static_cast<uint32_t>(AnalysisPass::Mutability) |
    static_cast<uint32_t>(AnalysisPass::RefVariants) |
    static_cast<uint32_t>(AnalysisPass::Effects) |
    static_cast<uint32_t>(AnalysisPass::Usage);

enum class ReentrancyBoundaryKind {
    EntryPoint,
    ExitPoint,
};

enum class ReentrancyMode {
    Default,
    Reentrant,
    NonReentrant,
};

struct AnalysisConfig {
    uint32_t enabled_passes = kAllAnalysisPasses;
    char default_entry_context = 'R';
    char default_exit_context = 'R';
    std::function<ReentrancyMode(const Symbol*, ReentrancyBoundaryKind)> reentrancy_mode_for_boundary;
};

enum class VarMutability { Mutable, Constexpr };

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
    explicit Analyzer(TypeChecker* tc,
                      const OptimizationFacts* opt = nullptr,
                      AnalysisConfig config = {})
        : type_checker(tc), optimization(opt), analysis_config(std::move(config)) {}
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
    AnalysisConfig analysis_config;
    int current_instance_id = -1;
    AnalysisRunSummary run_summary_;

    AnalysisContext context() const;
    const AnalysisRunSummary& run_summary() const { return run_summary_; }
    InstanceScope scoped_instance(int instance_id);
    bool pass_enabled(AnalysisPass pass) const;
    bool global_initializer_runs_at_runtime(const Symbol* sym) const;
    void build_run_summary(const AnalysisFacts& facts);
    std::optional<bool> constexpr_condition(ExprPtr expr) const;
    void walk_pruned_expr(ExprPtr expr, const ExprVisitor& on_expr, const StmtVisitor& on_stmt);
    void walk_pruned_stmt(StmtPtr stmt, const ExprVisitor& on_expr, const StmtVisitor& on_stmt);
    void walk_runtime_expr(ExprPtr expr, const ExprVisitor& on_expr, const StmtVisitor& on_stmt);
    void walk_runtime_stmt(StmtPtr stmt, const ExprVisitor& on_expr, const StmtVisitor& on_stmt);
    void analyze_reachability(const Module& mod, AnalysisFacts& facts);
    void analyze_reentrancy(const Module& mod, AnalysisFacts& facts);
    void analyze_mutability(const Module& mod, AnalysisFacts& facts);
    void analyze_ref_variants(const Module& mod, AnalysisFacts& facts);
    void analyze_effects(const Module& mod, AnalysisFacts& facts);
    void analyze_usage(const Module& mod, AnalysisFacts& facts);

    void mark_reachable(const Symbol* func_sym, AnalysisFacts& facts);
    void collect_calls(ExprPtr expr, std::unordered_set<const Symbol*>& calls);

    Symbol* binding_for(ExprPtr expr) const;
    std::optional<const Symbol*> base_identifier_symbol(ExprPtr expr) const;
    bool is_addressable_lvalue(ExprPtr expr) const;
    bool is_mutable_lvalue(ExprPtr expr) const;
    bool receiver_is_mutable_arg(ExprPtr expr) const;
};

} // namespace vexel
