#pragma once

#include <functional>
#include <optional>
#include "analysis.h"
#include "ast.h"

namespace vexel {

struct TypeUseContext {
    std::function<TypePtr(TypePtr)> resolve_type;
    std::function<std::optional<bool>(int, ExprPtr)> constexpr_condition;
    std::function<const Symbol*(int, ExprPtr)> binding;
    int type_strictness = 0;
};

void validate_type_usage(const Module& mod, const AnalysisFacts& facts, const TypeUseContext& ctx);
// Invariant: validate_type_usage runs after Analyzer. Only values that are used
// (reachable functions, used globals, or returns in value-required contexts)
// must have concrete types. Compile-time-dead branches are ignored, and
// expression-parameter arguments are treated as opaque at this stage.

} // namespace vexel
