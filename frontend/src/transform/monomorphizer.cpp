#include "monomorphizer.h"
#include "common.h"
#include "program.h"
#include "typechecker.h"

namespace vexel {

Monomorphizer::Monomorphizer(TypeChecker* checker)
    : checker(checker) {}

namespace {

void append_unique_stmt(std::vector<StmtPtr>& stmts, const StmtPtr& stmt) {
    if (!stmt) return;
    for (const auto& existing : stmts) {
        if (existing.get() == stmt.get()) {
            return;
        }
    }
    stmts.push_back(stmt);
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
            append_unique_stmt(mod.top_level, inst);

            if (!program) continue;

            const Symbol* generated_sym = nullptr;
            for (const auto& instance : program->instances) {
                Symbol* sym = checker->binding_for(instance.id, inst.get());
                if (sym) {
                    generated_sym = sym;
                    break;
                }
            }
            if (!generated_sym) {
                throw CompileError("Internal error: missing symbol for monomorphized function",
                                   inst ? inst->location : SourceLocation());
            }
            ModuleInfo* mod_info = program->module(generated_sym->module_id);
            if (!mod_info) {
                throw CompileError("Internal error: missing module for monomorphized function",
                                   inst ? inst->location : SourceLocation());
            }
            append_unique_stmt(mod_info->module.top_level, inst);
        }
    }
}

} // namespace vexel
