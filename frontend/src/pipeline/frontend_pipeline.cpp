#include "frontend_pipeline.h"

#include "analysis.h"
#include "ast_walk.h"
#include "lowerer.h"
#include "monomorphizer.h"
#include "optimizer.h"
#include "pass_invariants.h"
#include "program.h"
#include "residualizer.h"
#include "resolver.h"
#include "typechecker.h"

#include <iostream>
#include <unordered_set>
#include <vector>

namespace vexel {

namespace {

#ifdef VEXEL_DEBUG_PASS_INVARIANTS
void validate_program_stage(const Program& program, const char* stage) {
    validate_program_invariants(program, stage);
}

void validate_module_stage(const Module& mod, const char* stage) {
    validate_module_invariants(mod, stage);
}
#else
void validate_program_stage(const Program&, const char*) {}
void validate_module_stage(const Module&, const char*) {}
#endif

bool keep_top_level_stmt(StmtPtr stmt,
                         Symbol* sym,
                         const AnalysisFacts& analysis) {
    if (!stmt) return false;

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            // Frontend DCE contract: only reachable functions reach backends.
            return sym && analysis.reachable_functions.count(sym);
        case Stmt::Kind::VarDecl:
            if (stmt->is_exported) return true;
            // Frontend DCE contract: only ABI-visible or referenced globals survive.
            return sym && analysis.used_global_vars.count(sym);
        case Stmt::Kind::TypeDecl:
            return analysis.used_type_names.count(stmt->type_decl_name);
        default:
            return true;
    }
}

Module merge_live_program_instances(const Program& program,
                                    TypeChecker& checker,
                                    const AnalysisFacts& analysis) {
    Module merged;
    if (program.modules.empty()) {
        return merged;
    }

    merged.name = program.modules.front().module.name;
    merged.path = program.modules.front().path;

    for (const auto& instance : program.instances) {
        const auto& mod_info = program.modules[static_cast<size_t>(instance.module_id)];
        for (const auto& stmt : mod_info.module.top_level) {
            Symbol* sym = nullptr;
            if (stmt && (stmt->kind == Stmt::Kind::FuncDecl || stmt->kind == Stmt::Kind::VarDecl)) {
                sym = checker.binding_for(instance.id, stmt.get());
                if (!sym) {
                    throw CompileError("Internal error: missing top-level binding during frontend DCE prune",
                                       stmt->location);
                }
            }
            if (keep_top_level_stmt(stmt, sym, analysis)) {
                merged.top_level.push_back(stmt);
            }
        }
    }

    return merged;
}

void collect_internal_calls_in_stmt(const StmtPtr& stmt,
                                    int instance_id,
                                    TypeChecker& checker,
                                    std::unordered_set<const Symbol*>& out);

void collect_internal_calls_in_expr(const ExprPtr& expr,
                                    int instance_id,
                                    TypeChecker& checker,
                                    std::unordered_set<const Symbol*>& out) {
    if (!expr) return;

    if (expr->kind == Expr::Kind::Call &&
        expr->operand &&
        expr->operand->kind == Expr::Kind::Identifier) {
        Symbol* callee = checker.binding_for(instance_id, expr->operand.get());
        if (callee &&
            callee->kind == Symbol::Kind::Function &&
            !callee->is_external &&
            callee->declaration) {
            out.insert(callee);
        }
    }

    for_each_expr_child(
        expr,
        [&](const ExprPtr& child) { collect_internal_calls_in_expr(child, instance_id, checker, out); },
        [&](const StmtPtr& child) { collect_internal_calls_in_stmt(child, instance_id, checker, out); });
}

void collect_internal_calls_in_stmt(const StmtPtr& stmt,
                                    int instance_id,
                                    TypeChecker& checker,
                                    std::unordered_set<const Symbol*>& out) {
    if (!stmt) return;
    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& expr) { collect_internal_calls_in_expr(expr, instance_id, checker, out); },
        [&](const StmtPtr& child) { collect_internal_calls_in_stmt(child, instance_id, checker, out); });
}

void validate_prune_linkage(const Program& program,
                            TypeChecker& checker,
                            const AnalysisFacts& analysis) {
    std::unordered_set<const Symbol*> kept_functions;
    std::unordered_set<const Symbol*> top_level_functions;
    std::vector<std::pair<int, StmtPtr>> kept_roots;

    for (const auto& instance : program.instances) {
        const auto& mod_info = program.modules[static_cast<size_t>(instance.module_id)];
        for (const auto& stmt : mod_info.module.top_level) {
            Symbol* sym = nullptr;
            if (stmt && (stmt->kind == Stmt::Kind::FuncDecl || stmt->kind == Stmt::Kind::VarDecl)) {
                sym = checker.binding_for(instance.id, stmt.get());
                if (!sym) {
                    throw CompileError("Internal error: missing top-level binding during frontend prune linkage validation",
                                       stmt->location);
                }
            }
            if (stmt && stmt->kind == Stmt::Kind::FuncDecl && sym) {
                top_level_functions.insert(sym);
            }

            if (!keep_top_level_stmt(stmt, sym, analysis)) {
                continue;
            }

            kept_roots.emplace_back(instance.id, stmt);
            if (stmt && stmt->kind == Stmt::Kind::FuncDecl && sym) {
                kept_functions.insert(sym);
            }
        }
    }

    std::unordered_set<const Symbol*> required_internal_calls;
    for (const auto& root : kept_roots) {
        collect_internal_calls_in_stmt(root.second, root.first, checker, required_internal_calls);
    }

    for (const Symbol* callee : required_internal_calls) {
        if (!callee || callee->is_external) continue;
        if (callee->kind != Symbol::Kind::Function) continue;
        if (!top_level_functions.count(callee)) continue;
        if (!kept_functions.count(callee)) {
            SourceLocation loc = callee->declaration ? callee->declaration->location : SourceLocation();
            throw CompileError("Internal error: frontend prune dropped referenced function '" + callee->name + "'",
                               loc);
        }
    }
}

} // namespace

Module merge_program_instances(const Program& program) {
    Module merged;
    if (program.modules.empty()) {
        return merged;
    }

    merged.name = program.modules.front().module.name;
    merged.path = program.modules.front().path;
    for (const auto& instance : program.instances) {
        const auto& mod_info = program.modules[static_cast<size_t>(instance.module_id)];
        for (const auto& stmt : mod_info.module.top_level) {
            merged.top_level.push_back(stmt);
        }
    }
    return merged;
}

FrontendPipelineResult run_frontend_pipeline(Program& program,
                                             Resolver& resolver,
                                             TypeChecker& checker,
                                             bool verbose,
                                             const AnalysisConfig& analysis_config) {
    validate_program_stage(program, "post-load");

    resolver.resolve();
    validate_program_stage(program, "post-resolve");

    if (verbose) {
        std::cout << "Type checking..." << std::endl;
    }
    checker.check_program(program);
    validate_program_stage(program, "post-typecheck");

    Module merged = merge_program_instances(program);
    validate_module_stage(merged, "post-merge");

    Monomorphizer monomorphizer(&checker);
    monomorphizer.run(merged);
    validate_module_stage(merged, "post-monomorphize");

    Lowerer lowerer(&checker);
    lowerer.run(merged);
    validate_module_stage(merged, "post-lower");

    Optimizer optimizer(&checker);
    OptimizationFacts optimization;
    static constexpr int kMaxResidualFixpointIterations = 64;
    int residual_iters = 0;
    while (true) {
        optimization = optimizer.run(merged);
        Residualizer residualizer(optimization);
        if (!residualizer.run(merged, checker.get_program())) {
            break;
        }
        residual_iters++;
        if (residual_iters >= kMaxResidualFixpointIterations) {
            throw CompileError("Internal error: residualization did not converge",
                               merged.location);
        }
    }
    optimization = optimizer.run(merged);
    validate_module_stage(merged, "post-optimize");

    Analyzer analyzer(&checker, &optimization, analysis_config);
    AnalysisFacts analysis = analyzer.run(merged);
    validate_module_stage(merged, "post-analysis");

    checker.validate_type_usage(merged, analysis);
    validate_module_stage(merged, "post-type-use");

    validate_prune_linkage(program, checker, analysis);
    merged = merge_live_program_instances(program, checker, analysis);
    validate_module_stage(merged, "post-dce-prune");

    FrontendPipelineResult result;
    result.merged = std::move(merged);
    result.optimization = std::move(optimization);
    result.analysis = std::move(analysis);
    return result;
}

} // namespace vexel
