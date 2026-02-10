#pragma once

#include "analysis.h"
#include "ast.h"
#include "optimizer.h"

namespace vexel {

class Resolver;
class TypeChecker;
struct Program;

struct FrontendPipelineResult {
    Module merged;
    OptimizationFacts optimization;
    AnalysisFacts analysis;
};

Module merge_program_instances(const Program& program);

FrontendPipelineResult run_frontend_pipeline(Program& program,
                                             Resolver& resolver,
                                             TypeChecker& checker,
                                             bool verbose,
                                             const AnalysisConfig& analysis_config = AnalysisConfig{});

} // namespace vexel
