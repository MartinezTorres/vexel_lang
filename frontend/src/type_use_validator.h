#pragma once

#include <functional>
#include <optional>
#include "analysis.h"
#include "ast.h"

namespace vexel {

struct TypeUseContext {
    std::function<TypePtr(TypePtr)> resolve_type;
    std::function<std::optional<bool>(ExprPtr)> constexpr_condition;
};

void validate_type_usage(const Module& mod, const AnalysisFacts& facts, const TypeUseContext& ctx);

} // namespace vexel
