#pragma once

#include "analysis.h"
#include "analyzed_program.h"
#include "ast.h"
#include "optimizer.h"

namespace vexel {

class TypeChecker;

AnalyzedProgram make_analyzed_program(const Module& merged,
                                      TypeChecker& checker,
                                      const AnalysisFacts& analysis,
                                      const OptimizationFacts& optimization);

} // namespace vexel
