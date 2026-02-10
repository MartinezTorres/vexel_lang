#include "compiler.h"
#include "analysis_report.h"
#include "backend_registry.h"
#include "constants.h"
#include "frontend_pipeline.h"
#include "io_utils.h"
#include "module_loader.h"
#include "analyzed_program_builder.h"
#include "resolver.h"
#include "typechecker.h"

#include <iostream>
#include <filesystem>
#include <algorithm>

namespace vexel {

Compiler::Compiler(const Options& opts) : options(opts) {}

Compiler::OutputPaths Compiler::resolve_output_paths(const std::string& output_file) {
    std::filesystem::path base_path(output_file);
    std::filesystem::path dir = base_path.parent_path();
    if (dir.empty()) dir = ".";
    std::string stem = base_path.has_extension()
        ? base_path.stem().string()
        : base_path.filename().string();
    if (stem.empty()) stem = "out";
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
    return {dir, stem};
}

Compiler::OutputPaths Compiler::compile() {
    if (options.verbose) {
        std::cout << "Compiling: " << options.input_file << std::endl;
    }

    ModuleLoader loader(options.project_root);
    Program program = loader.load(options.input_file);

    Bindings bindings;
    Resolver resolver(program, bindings, options.project_root);
    TypeChecker checker(options.project_root, options.allow_process, &resolver, &bindings, &program);
    FrontendPipelineResult pipeline = run_frontend_pipeline(program, resolver, checker, options.verbose);

    OutputPaths paths = resolve_output_paths(options.output_file);
    if (options.emit_analysis) {
        std::filesystem::path analysis_path = paths.dir / (paths.stem + ".analysis.txt");
        if (options.verbose) {
            std::cout << "Writing analysis report: " << analysis_path << std::endl;
        }
        write_text_file_or_throw(analysis_path.string(),
                                 format_analysis_report(pipeline.merged,
                                                        pipeline.analysis,
                                                        &pipeline.optimization));
    }

    const Backend* backend = find_backend(options.backend);
    if (!backend) {
        throw CompileError("Unknown backend: " + options.backend, SourceLocation());
    }
    if (options.verbose) {
        std::cout << "Generating backend: " << backend->info.name << std::endl;
    }
    AnalyzedProgram analyzed =
        make_analyzed_program(pipeline.merged, checker, pipeline.analysis, pipeline.optimization);
    BackendInput input{analyzed, options, paths};
    backend->emit(input);

    if (options.verbose) {
        std::cout << "Compilation successful!" << std::endl;
    }

    return paths;
}

}
