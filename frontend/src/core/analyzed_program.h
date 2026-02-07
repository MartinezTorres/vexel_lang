#pragma once

#include "analysis.h"
#include "ast.h"
#include "optimizer.h"
#include "program.h"

#include <functional>
#include <optional>
#include <unordered_map>

namespace vexel {

// Strict frontend->backend handoff contract.
// Backends receive only fully analyzed program state plus pure query hooks.
struct AnalyzedProgram {
    const Module* module = nullptr;
    const Program* program = nullptr;
    const AnalysisFacts* analysis = nullptr;
    const OptimizationFacts* optimization = nullptr;
    int entry_instance_id = 0;

    const std::unordered_map<std::string, std::vector<TypePtr>>* forced_tuple_types = nullptr;

    std::function<Symbol*(int instance_id, const void* node)> binding_for;
    std::function<TypePtr(TypePtr)> resolve_type;
    std::function<std::optional<bool>(ExprPtr)> constexpr_condition;
    std::function<bool(int instance_id, ExprPtr, CTValue&)> try_evaluate;
    std::function<Symbol*(int instance_id, const std::string& type_name)> lookup_type_symbol;
};

} // namespace vexel
