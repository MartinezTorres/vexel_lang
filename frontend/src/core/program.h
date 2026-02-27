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

enum class ModuleOrigin {
    Project,
    BundledStd,
};

struct ModuleInfo {
    ModuleId id = -1;
    std::string path;
    ModuleOrigin origin = ModuleOrigin::Project;
    Module module;
};

struct ModuleInstance {
    ModuleInstanceId id = -1;
    ModuleId module_id = -1;
    int scope_id = -1;
    std::unordered_map<std::string, Symbol*> symbols;
    std::unordered_map<std::string, Symbol*> value_symbols;
    std::unordered_map<std::string, Symbol*> type_symbols;
    std::unordered_map<std::string, std::vector<Symbol*>> function_overloads;
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
