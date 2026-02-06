#include "monomorphizer.h"
#include "typechecker.h"

namespace vexel {

Monomorphizer::Monomorphizer(TypeChecker* checker)
    : checker(checker) {}

void Monomorphizer::run(Module& mod) {
    if (!checker) return;

    // Invariant: monomorphization only materializes instantiations recorded by the type checker.
    auto& pending = checker->get_pending_instantiations();
    while (!pending.empty()) {
        std::vector<StmtPtr> batch;
        batch.swap(pending);
        for (auto& inst : batch) {
            mod.top_level.push_back(inst);
        }
    }
}

} // namespace vexel
