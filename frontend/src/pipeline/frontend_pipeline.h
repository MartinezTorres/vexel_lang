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

FrontendPipelineResult run_frontend_pipeline(Program& program,
                                             Resolver& resolver,
                                             TypeChecker& checker,
                                             bool verbose,
                                             const AnalysisConfig& analysis_config = AnalysisConfig{});

} // namespace vexel
