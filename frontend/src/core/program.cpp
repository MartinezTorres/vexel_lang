#include "program.h"

namespace vexel {

ModuleInfo* Program::module(ModuleId id) {
    if (id < 0 || static_cast<size_t>(id) >= modules.size()) return nullptr;
    return &modules[static_cast<size_t>(id)];
}

const ModuleInfo* Program::module(ModuleId id) const {
    if (id < 0 || static_cast<size_t>(id) >= modules.size()) return nullptr;
    return &modules[static_cast<size_t>(id)];
}

} // namespace vexel
