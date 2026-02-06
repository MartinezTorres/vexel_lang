#pragma once
#include "ast.h"
#include "symbols.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vexel {

using ModuleId = int;
using ModuleInstanceId = int;

struct ModuleInfo {
    ModuleId id = -1;
    std::string path;
    Module module;
};

struct ModuleInstance {
    ModuleInstanceId id = -1;
    ModuleId module_id = -1;
    int scope_id = -1;
    std::unordered_map<std::string, Symbol*> symbols;
};

struct Program {
    std::vector<ModuleInfo> modules;
    std::unordered_map<std::string, ModuleId> path_to_id;
    std::vector<ModuleInstance> instances;
    std::vector<std::unique_ptr<Symbol>> symbols;

    ModuleInfo* module(ModuleId id);
    const ModuleInfo* module(ModuleId id) const;
};

} // namespace vexel
