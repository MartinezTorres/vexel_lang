#include "analyzed_program_builder.h"

#include "cte_value_utils.h"
#include "typechecker.h"

namespace vexel {

AnalyzedProgram make_analyzed_program(const Module& merged,
                                      TypeChecker& checker,
                                      const AnalysisFacts& analysis,
                                      const OptimizationFacts& optimization) {
    AnalyzedProgram out;
    out.module = &merged;
    out.program = checker.get_program();
    out.analysis = &analysis;
    out.optimization = &optimization;
    out.entry_instance_id = 0;
    if (out.program && !out.program->instances.empty()) {
        out.entry_instance_id = out.program->instances.front().id;
    }

    out.forced_tuple_types = &checker.get_forced_tuple_types();

    out.binding_for = [&checker](int instance_id, const void* node) -> Symbol* {
        if (!node) return nullptr;
        return checker.binding_for(instance_id, node);
    };

    out.resolve_type = [&checker](TypePtr type) -> TypePtr {
        return checker.resolve_type(type);
    };

    out.constexpr_condition = [&optimization](int instance_id, ExprPtr expr) -> std::optional<bool> {
        if (!expr) return std::nullopt;
        auto cond_it = optimization.constexpr_conditions.find(expr_fact_key(instance_id, expr.get()));
        if (cond_it != optimization.constexpr_conditions.end()) {
            return cond_it->second;
        }
        auto value_it = optimization.constexpr_values.find(expr_fact_key(instance_id, expr.get()));
        if (value_it == optimization.constexpr_values.end()) {
            return std::nullopt;
        }
        return cte_scalar_to_bool(value_it->second);
    };

    out.lookup_type_symbol = [&checker](int instance_id, const std::string& type_name) -> Symbol* {
        auto scope = checker.scoped_instance(instance_id);
        (void)scope;
        Scope* global = checker.get_scope();
        if (!global) return nullptr;
        return global->lookup(type_name);
    };

    return out;
}

} // namespace vexel
