#include "analyzed_program_builder.h"

#include "evaluator.h"
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

    out.constexpr_condition = [&checker](int instance_id, ExprPtr expr) -> std::optional<bool> {
        if (!expr) return std::nullopt;
        auto scope = checker.scoped_instance(instance_id);
        (void)scope;
        return checker.constexpr_condition(expr);
    };

    out.try_evaluate = [&checker](int instance_id, ExprPtr expr, CTValue& out_value) -> bool {
        auto scope = checker.scoped_instance(instance_id);
        (void)scope;
        CompileTimeEvaluator evaluator(&checker);
        return evaluator.try_evaluate(expr, out_value);
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
