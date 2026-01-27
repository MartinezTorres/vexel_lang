#pragma once
#include "analysis.h"
#include "optimizer.h"
#include <string>

namespace vexel {

std::string format_analysis_report(const Module& mod, const AnalysisFacts& analysis,
                                   const OptimizationFacts* optimization = nullptr);

} // namespace vexel
