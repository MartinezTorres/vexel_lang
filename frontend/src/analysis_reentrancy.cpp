#include "analysis.h"

#include "evaluator.h"
#include "typechecker.h"

#include <algorithm>
#include <deque>

namespace vexel {

void Analyzer::analyze_reentrancy(const Module& /*mod*/, AnalysisFacts& facts) {
    Program* program = type_checker ? type_checker->get_program() : nullptr;
    if (!program) return;

    auto has_ann = [](const std::vector<Annotation>& anns, const std::string& name) {
        return std::any_of(anns.begin(), anns.end(), [&](const Annotation& a) { return a.name == name; });
    };

    std::unordered_map<const Symbol*, StmtPtr> function_map;
    std::unordered_set<const Symbol*> external_reentrant;
    std::unordered_set<const Symbol*> external_nonreentrant;

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (sym->is_external) {
                bool is_reentrant = has_ann(sym->declaration->annotations, "reentrant");
                bool is_nonreentrant = has_ann(sym->declaration->annotations, "nonreentrant");
                if (is_reentrant && is_nonreentrant) {
                    throw CompileError("Conflicting annotations: [[reentrant]] and [[nonreentrant]] on external function '" +
                                       sym->name + "'", sym->declaration->location);
                }
                if (is_reentrant) {
                    external_reentrant.insert(sym);
                } else {
                    external_nonreentrant.insert(sym);
                }
                continue;
            }
            if (!facts.reachable_functions.count(sym)) continue;
            function_map[sym] = sym->declaration;
        }
    }

    std::deque<std::pair<const Symbol*, char>> work;

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || sym->kind != Symbol::Kind::Function) continue;
            if (!sym->is_exported) continue;
            if (!facts.reachable_functions.count(sym)) continue;

            bool is_reentrant = has_ann(sym->declaration->annotations, "reentrant");
            bool is_nonreentrant = has_ann(sym->declaration->annotations, "nonreentrant");
            if (is_reentrant && is_nonreentrant) {
                throw CompileError("Conflicting annotations: [[reentrant]] and [[nonreentrant]] on entry function '" +
                                   sym->name + "'", sym->declaration->location);
            }

            char ctx = (is_reentrant ? 'R' : 'N');
            if (facts.reentrancy_variants[sym].insert(ctx).second) {
                work.emplace_back(sym, ctx);
            }
        }
    }

    for (const auto& instance : program->instances) {
        current_instance_id = instance.id;
        for (const auto& pair : instance.symbols) {
            const Symbol* sym = pair.second;
            if (!sym || (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Constant)) continue;
            if (!sym->declaration || !sym->declaration->var_init) continue;

            bool evaluated_at_compile_time = false;
            if (type_checker) {
                CompileTimeEvaluator evaluator(type_checker);
                CTValue result;
                if (evaluator.try_evaluate(sym->declaration->var_init, result)) {
                    evaluated_at_compile_time = true;
                }
            }
            if (evaluated_at_compile_time) continue;
            std::unordered_set<const Symbol*> calls;
            collect_calls(sym->declaration->var_init, calls);
            for (const auto& callee_sym : calls) {
                if (!callee_sym) continue;
                if (facts.reentrancy_variants[callee_sym].insert('N').second) {
                    work.emplace_back(callee_sym, 'N');
                }
            }
        }
    }

    while (!work.empty()) {
        auto [func_sym, ctx] = work.front();
        work.pop_front();
        auto it = function_map.find(func_sym);
        if (it == function_map.end()) {
            if (ctx == 'R' && external_nonreentrant.count(func_sym)) {
                throw CompileError("Reentrant path calls non-reentrant external function '" + func_sym->name + "'",
                                   func_sym->declaration ? func_sym->declaration->location : SourceLocation());
            }
            continue;
        }
        StmtPtr func = it->second;
        if (!func || !func->body) continue;
        if (is_foldable(func_sym)) continue;

        int saved_instance = current_instance_id;
        current_instance_id = func_sym->instance_id;

        std::unordered_set<const Symbol*> calls;
        collect_calls(func->body, calls);
        for (const auto& callee_sym : calls) {
            if (!callee_sym) continue;
            if (ctx == 'R' && external_nonreentrant.count(callee_sym)) {
                throw CompileError("Reentrant path calls non-reentrant external function '" + callee_sym->name + "'",
                                   func->location);
            }
            if (facts.reentrancy_variants[callee_sym].insert(ctx).second) {
                work.emplace_back(callee_sym, ctx);
            }
        }

        current_instance_id = saved_instance;
    }

    for (const auto& entry : function_map) {
        const Symbol* func_sym = entry.first;
        if (facts.reentrancy_variants[func_sym].empty()) {
            facts.reentrancy_variants[func_sym].insert('N');
        }
    }
}

} // namespace vexel
