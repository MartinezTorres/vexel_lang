#pragma once
#include "compiler.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace vexel {

struct BackendInfo {
    std::string name;
    std::string description;
    std::string version;
};

struct BackendContext {
    const Module& module;
    TypeChecker& checker;
    const Compiler::Options& options;
    const Compiler::OutputPaths& outputs;
    const std::unordered_set<std::string>& non_reentrant_funcs;
};

using BackendEmitFn = void (*)(const BackendContext& ctx);

struct Backend {
    BackendInfo info;
    BackendEmitFn emit = nullptr;
};

bool register_backend(Backend backend);
const Backend* find_backend(const std::string& name);
std::vector<BackendInfo> list_backends();

}
