#pragma once
#include "compiler.h"
#include "analysis.h"
#include "optimizer.h"
#include <ostream>
#include <string>
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
    const AnalysisFacts& analysis;
    const OptimizationFacts& optimization;
};

using BackendEmitFn = void (*)(const BackendContext& ctx);
// Driver option delegation contract:
// - Called only for options unknown to the frontend driver.
// - `index` points at argv[index]; backends may advance it if they consume extra args.
// - Return true when the option belongs to this backend (success or parse error).
// - On parse error, set `error`; driver prints combined frontend/backend usage.
using BackendParseOptionFn = bool (*)(int argc, char** argv, int& index,
                                      Compiler::Options& options, std::string& error);
// Print backend-specific usage lines for `vexel -h` and parse errors.
using BackendPrintUsageFn = void (*)(std::ostream& os);

// This registry API is the source of truth for backend integration points.
struct Backend {
    BackendInfo info;
    BackendEmitFn emit = nullptr;
    BackendParseOptionFn parse_option = nullptr;
    BackendPrintUsageFn print_usage = nullptr;
};

bool register_backend(Backend backend);
const Backend* find_backend(const std::string& name);
std::vector<BackendInfo> list_backends();

}
