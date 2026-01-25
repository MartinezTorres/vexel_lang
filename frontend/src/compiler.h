#pragma once
#include "ast.h"
#include "codegen.h"
#include "typechecker.h"
#include <string>
#include <unordered_set>
#include <filesystem>

namespace vexel {

// Compiler orchestrates the complete compilation pipeline:
// 1. Lexing and parsing
// 2. Type checking and semantic analysis
// 3. Generic monomorphization
// 4. Compile-time evaluation
// 5. Dead code elimination
// 6. Backend-specific code generation (C x86 or Banked)
class Compiler {
public:
    struct Options {
        std::string input_file;      // Source file to compile
        std::string output_file;      // Base name for output files
        bool verbose;                 // Enable verbose output
        std::string project_root;     // Root directory for module resolution
        bool emit_lowered;            // Emit lowered Vexel subset alongside backend output
        bool allow_process = false;   // Process expressions execute host commands; keep disabled by default
        enum class BackendKind {
            C,       // Portable C11 code generator
            Banked   // MSX-style banked memory code generator for SDCC
        };
        BackendKind backend;

        Options() : verbose(false), project_root("."), emit_lowered(false), allow_process(false), backend(BackendKind::C) {}
    };

    struct OutputPaths {
        std::filesystem::path dir;
        std::string stem;
    };

    Compiler(const Options& opts);
    OutputPaths compile();

private:
    Options options;

    std::string read_file(const std::string& path);
    void write_file(const std::string& path, const std::string& content);
    Module load_module(const std::string& path);

    OutputPaths resolve_output_paths(const std::string& output_file);

    void emit_banked_backend(const Module& mod, TypeChecker& checker,
                             CodeGenerator& codegen, const CCodegenResult& result,
                             const OutputPaths& paths);
    std::string build_param_list(CodeGenerator& codegen, StmtPtr func, bool with_types);
    std::string build_arg_list(CodeGenerator& codegen, StmtPtr func);
    std::string build_return_type(CodeGenerator& codegen, StmtPtr func);
};

}
