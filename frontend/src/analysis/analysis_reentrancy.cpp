#include "analysis.h"
#include "program.h"

#include <algorithm>
#include <deque>

namespace vexel {

void Analyzer::analyze_reentrancy(const Module& /*mod*/, AnalysisFacts& facts) {
    const AnalysisRunSummary& summary = run_summary();
    Program* program = summary.program;
    if (!program) return;

    auto normalize_ctx = [](char ctx, char fallback) {
        if (ctx == 'R' || ctx == 'N') return ctx;
        return (fallback == 'R' || fallback == 'N') ? fallback : 'N';
    };

    auto boundary_ctx = [&](const Symbol* sym, ReentrancyBoundaryKind kind) -> char {
        ReentrancyMode mode = ReentrancyMode::Default;
        if (analysis_config.reentrancy_mode_for_boundary) {
            mode = analysis_config.reentrancy_mode_for_boundary(sym, kind);
        }
        char fallback = (kind == ReentrancyBoundaryKind::EntryPoint)
                            ? analysis_config.default_entry_context
                            : analysis_config.default_exit_context;
        switch (mode) {
            case ReentrancyMode::Reentrant:
                return 'R';
            case ReentrancyMode::NonReentrant:
                return 'N';
            case ReentrancyMode::Default:
            default:
                return normalize_ctx(fallback, 'N');
        }
    };

    std::unordered_map<const Symbol*, StmtPtr> function_map = summary.reachable_function_decls;
    std::unordered_set<const Symbol*> external_nonreentrant;

    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function || !sym->is_external) continue;
            if (boundary_ctx(sym, ReentrancyBoundaryKind::ExitPoint) == 'N') {
                external_nonreentrant.insert(sym);
            }
        }
    }

    std::deque<std::pair<const Symbol*, char>> work;

    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (!sym->is_exported) continue;
            if (!facts.reachable_functions.count(sym)) continue;

            char ctx = boundary_ctx(sym, ReentrancyBoundaryKind::EntryPoint);
            if (facts.reentrancy_variants[sym].insert(ctx).second) {
                work.emplace_back(sym, ctx);
            }
        }
    }

    for (const Symbol* sym : summary.runtime_initialized_globals) {
        auto calls_it = summary.global_initializer_calls.find(sym);
        if (calls_it == summary.global_initializer_calls.end()) continue;
        for (const auto& callee_sym : calls_it->second) {
            if (!callee_sym) continue;
            if (facts.reentrancy_variants[callee_sym].insert('N').second) {
                work.emplace_back(callee_sym, 'N');
            }
        }
    }

    while (!work.empty()) {
        auto [func_sym, ctx] = work.front();
        work.pop_front();
        auto it = function_map.find(func_sym);
        if (it == function_map.end()) {
            if (ctx == 'R' && external_nonreentrant.count(func_sym)) {
                throw CompileError("Reentrant path calls non-reentrant external function '" + func_sym->name + "'",
                                   func_sym->declaration ? func_sym->declaration->location : SourceLocation());
            }
            continue;
        }
        StmtPtr func = it->second;
        if (!func || !func->body) continue;
        if (is_foldable(func_sym)) continue;

        auto calls_it = summary.reachable_calls.find(func_sym);
        if (calls_it == summary.reachable_calls.end()) continue;
        for (const auto& callee_sym : calls_it->second) {
            if (!callee_sym) continue;
            if (ctx == 'R' && external_nonreentrant.count(callee_sym)) {
                throw CompileError("Reentrant path calls non-reentrant external function '" + callee_sym->name + "'",
                                   func->location);
            }
            if (facts.reentrancy_variants[callee_sym].insert(ctx).second) {
                work.emplace_back(callee_sym, ctx);
            }
        }
    }

    char fallback_ctx = normalize_ctx(analysis_config.default_entry_context, 'N');
    for (const auto& entry : function_map) {
        const Symbol* func_sym = entry.first;
        if (facts.reentrancy_variants[func_sym].empty()) {
            facts.reentrancy_variants[func_sym].insert(fallback_ctx);
        }
    }
}

} // namespace vexel
