#include "frontend_pipeline.h"

#include "analysis.h"
#include "lowerer.h"
#include "monomorphizer.h"
#include "optimizer.h"
#include "pass_invariants.h"
#include "program.h"
#include "resolver.h"
#include "typechecker.h"

#include <iostream>

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
                                             bool verbose) {
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
    OptimizationFacts optimization = optimizer.run(merged);
    validate_module_stage(merged, "post-optimize");

    Analyzer analyzer(&checker, &optimization);
    AnalysisFacts analysis = analyzer.run(merged);
    validate_module_stage(merged, "post-analysis");

    checker.validate_type_usage(merged, analysis);
    validate_module_stage(merged, "post-type-use");

    FrontendPipelineResult result;
    result.merged = std::move(merged);
    result.optimization = std::move(optimization);
    result.analysis = std::move(analysis);
    return result;
}

} // namespace vexel
