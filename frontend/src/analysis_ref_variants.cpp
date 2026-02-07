#include "analysis.h"
#include "program.h"

namespace vexel {

void Analyzer::analyze_ref_variants(const Module& /*mod*/, AnalysisFacts& facts) {
    facts.ref_variants.clear();

    const AnalysisRunSummary& summary = run_summary();
    Program* program = summary.program;
    if (!program) return;

    std::unordered_map<const Symbol*, StmtPtr> function_map;
    for (const auto& instance : program->instances) {
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) continue;
            function_map[sym] = sym->declaration;
        }
    }

    auto ref_variant_key = [&](const ExprPtr& call, size_t ref_count) {
        std::string key;
        key.reserve(ref_count);
        for (size_t i = 0; i < ref_count; i++) {
            bool is_mut = false;
            if (call && i < call->receivers.size()) {
                is_mut = receiver_is_mutable_arg(call->receivers[i]);
            }
            key.push_back(is_mut ? 'M' : 'N');
        }
        return key;
    };

    auto record_call = [&](ExprPtr expr) {
        if (!expr || expr->kind != Expr::Kind::Call) return;
        if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) return;
        Symbol* callee = binding_for(expr->operand);
        if (!callee) return;
        auto fit = function_map.find(callee);
        if (fit == function_map.end()) return;
        size_t ref_count = fit->second->ref_params.size();
        if (ref_count == 0) return;
        std::string key = ref_variant_key(expr, ref_count);
        facts.ref_variants[callee].insert(key);
    };

    for (const auto& entry : summary.reachable_function_decls) {
        const Symbol* func_sym = entry.first;
        const StmtPtr& func_decl = entry.second;
        if (!func_sym || !func_decl) continue;
        if (is_foldable(func_sym)) continue;
        auto func_scope = scoped_instance(func_sym->instance_id);
        (void)func_scope;
        if (func_decl->body) {
            walk_pruned_expr(
                func_decl->body,
                [&](ExprPtr expr) { record_call(expr); },
                [&](StmtPtr) {});
        }
    }

    for (const Symbol* sym : summary.runtime_initialized_globals) {
        if (!sym || !sym->declaration || !sym->declaration->var_init) continue;
        auto global_scope = scoped_instance(sym->instance_id);
        (void)global_scope;
        walk_pruned_expr(
            sym->declaration->var_init,
            [&](ExprPtr expr) { record_call(expr); },
            [&](StmtPtr) {});
    }
}

} // namespace vexel
