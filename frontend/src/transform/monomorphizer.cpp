#include "monomorphizer.h"
#include "common.h"
#include "program.h"
#include "typechecker.h"

namespace vexel {

Monomorphizer::Monomorphizer(TypeChecker* checker)
    : checker(checker) {}

namespace {

void append_unique_stmt(std::vector<StmtPtr>& stmts,
                        std::vector<int>* instance_ids,
                        const StmtPtr& stmt,
                        int instance_id) {
    if (!stmt) return;
    for (size_t i = 0; i < stmts.size(); ++i) {
        const auto& existing = stmts[i];
        if (existing.get() == stmt.get()) {
            return;
        }
    }
    stmts.push_back(stmt);
    if (instance_ids) {
        instance_ids->push_back(instance_id);
    }
}

} // namespace

void Monomorphizer::run(Module& mod) {
    if (!checker) return;
    Program* program = checker->get_program();

    // Invariant: monomorphization only materializes instantiations recorded by the type checker.
    auto& pending = checker->get_pending_instantiations();
    while (!pending.empty()) {
        std::vector<StmtPtr> batch;
        batch.swap(pending);
        for (auto& inst : batch) {
            const Symbol* generated_sym = nullptr;
            int generated_instance_id = -1;
            if (program) {
                for (const auto& instance : program->instances) {
                    Symbol* sym = checker->binding_for(instance.id, inst.get());
                    if (sym) {
                        generated_sym = sym;
                        generated_instance_id = sym->instance_id;
                        break;
                    }
                }

                if (!generated_sym) {
                    throw CompileError("Internal error: missing symbol for monomorphized function",
                                       inst ? inst->location : SourceLocation());
                }
            } else if (!mod.top_level_instance_ids.empty()) {
                throw CompileError("Internal error: monomorphizer requires Program context to append instance IDs",
                                   inst ? inst->location : SourceLocation());
            }

            append_unique_stmt(
                mod.top_level,
                mod.top_level_instance_ids.empty() ? nullptr : &mod.top_level_instance_ids,
                inst,
                generated_instance_id);

            if (!program) continue;

            ModuleInfo* mod_info = program->module(generated_sym->module_id);
            if (!mod_info) {
                throw CompileError("Internal error: missing module for monomorphized function",
                                   inst ? inst->location : SourceLocation());
            }
            append_unique_stmt(mod_info->module.top_level, nullptr, inst, -1);
        }
    }
}

} // namespace vexel
