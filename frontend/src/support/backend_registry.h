#pragma once
#include "analyzed_program.h"
#include "compiler.h"
#include <ostream>
#include <string>
#include <vector>

namespace vexel {

struct BackendInfo {
    std::string name;
    std::string description;
    std::string version;
};

struct BackendInput {
    const AnalyzedProgram& program;
    const Compiler::Options& options;
    const Compiler::OutputPaths& outputs;
};

using BackendEmitFn = void (*)(const BackendInput& input);
using BackendNativeEmitTranslationUnitFn =
    bool (*)(const BackendInput& input, std::string& out_translation_unit, std::string& error);

struct BackendAnalysisRequirements {
    uint32_t required_passes = kAllAnalysisPasses;
    char default_entry_reentrancy = 'R';
    char default_exit_reentrancy = 'R';
};

using BackendAnalysisRequirementsFn =
    BackendAnalysisRequirements (*)(const Compiler::Options& options, std::string& error);
using BackendBoundaryReentrancyModeFn =
    ReentrancyMode (*)(const Symbol& sym,
                       ReentrancyBoundaryKind boundary,
                       const Compiler::Options& options,
                       std::string& error);
using BackendValidateOptionsFn = void (*)(const Compiler::Options& options, std::string& error);

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
    BackendNativeEmitTranslationUnitFn emit_translation_unit = nullptr;
    BackendAnalysisRequirementsFn analysis_requirements = nullptr;
    BackendBoundaryReentrancyModeFn boundary_reentrancy_mode = nullptr;
    BackendValidateOptionsFn validate_options = nullptr;
    BackendParseOptionFn parse_option = nullptr;
    BackendPrintUsageFn print_usage = nullptr;
};

bool register_backend(Backend backend);
const Backend* find_backend(const std::string& name);
std::vector<BackendInfo> list_backends();

}
