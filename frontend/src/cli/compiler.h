#pragma once
#include "ast.h"
#include "typechecker.h"
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>

namespace vexel {

// Compiler orchestrates the complete compilation pipeline:
// 1. Lexing and parsing
// 2. Type checking and semantic analysis
// 3. Generic monomorphization
// 4. Compile-time evaluation
// 5. Dead code elimination
// 6. Backend-specific code generation (registered backend)
class Compiler {
public:
    struct Options {
        std::string input_file;      // Source file to compile
        std::string output_file;      // Base name for output files
        bool verbose;                 // Enable verbose output
        std::string project_root;     // Root directory for module resolution
        bool emit_analysis;           // Emit analysis report alongside backend output
        bool allow_process = false;   // Process expressions execute host commands; keep disabled by default
        std::string backend; // Backend name (registered via backend registry)
        std::unordered_map<std::string, std::string> backend_options; // Backend-specific key=value options

        Options() : verbose(false), project_root("."), emit_analysis(false),
                    allow_process(false), backend("") {}
    };

    struct OutputPaths {
        std::filesystem::path dir;
        std::string stem;
    };

    Compiler(const Options& opts);
    OutputPaths compile();

private:
    Options options;

    OutputPaths resolve_output_paths(const std::string& output_file);
};

}
