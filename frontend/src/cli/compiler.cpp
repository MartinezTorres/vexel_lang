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

namespace {

bool valid_reentrancy_default(char key) {
    return key == 'R' || key == 'N';
}

} // namespace

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

    const Backend* backend = find_backend(options.backend);
    if (!backend) {
        throw CompileError("Unknown backend: " + options.backend, SourceLocation());
    }

    BackendAnalysisRequirements backend_reqs{};
    if (backend->analysis_requirements) {
        std::string req_error;
        backend_reqs = backend->analysis_requirements(options, req_error);
        if (!req_error.empty()) {
            throw CompileError(req_error, SourceLocation());
        }
    }
    if (!valid_reentrancy_default(backend_reqs.default_entry_reentrancy) ||
        !valid_reentrancy_default(backend_reqs.default_exit_reentrancy)) {
        throw CompileError("Backend '" + backend->info.name +
                               "' returned invalid default reentrancy (expected 'R' or 'N')",
                           SourceLocation());
    }
    if (backend->validate_options) {
        std::string opt_error;
        backend->validate_options(options, opt_error);
        if (!opt_error.empty()) {
            throw CompileError(opt_error, SourceLocation());
        }
    }

    AnalysisConfig analysis_config;
    analysis_config.enabled_passes = backend_reqs.required_passes;
    analysis_config.default_entry_context = backend_reqs.default_entry_reentrancy;
    analysis_config.default_exit_context = backend_reqs.default_exit_reentrancy;
    if (backend->boundary_reentrancy_mode) {
        analysis_config.reentrancy_mode_for_boundary =
            [&](const Symbol* sym, ReentrancyBoundaryKind boundary) -> ReentrancyMode {
                if (!sym) {
                    return ReentrancyMode::Default;
                }
                std::string boundary_error;
                ReentrancyMode mode =
                    backend->boundary_reentrancy_mode(*sym, boundary, options, boundary_error);
                if (!boundary_error.empty()) {
                    SourceLocation loc = sym->declaration ? sym->declaration->location : SourceLocation();
                    throw CompileError(boundary_error, loc);
                }
                return mode;
            };
    }

    ModuleLoader loader(options.project_root);
    Program program = loader.load(options.input_file);

    Bindings bindings;
    Resolver resolver(program, bindings, options.project_root);
    TypeChecker checker(options.project_root, options.allow_process, &resolver, &bindings, &program);
    FrontendPipelineResult pipeline =
        run_frontend_pipeline(program, resolver, checker, options.verbose, analysis_config);

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

bool Compiler::emit_translation_unit(std::string& out_translation_unit, std::string& error) {
    out_translation_unit.clear();
    error.clear();

    try {
        const Backend* backend = find_backend(options.backend);
        if (!backend) {
            error = "Unknown backend: " + options.backend;
            return false;
        }
        if (!backend->emit_translation_unit) {
            error = "Backend '" + backend->info.name + "' does not support translation-unit emission";
            return false;
        }

        BackendAnalysisRequirements backend_reqs{};
        if (backend->analysis_requirements) {
            std::string req_error;
            backend_reqs = backend->analysis_requirements(options, req_error);
            if (!req_error.empty()) {
                error = req_error;
                return false;
            }
        }
        if (!valid_reentrancy_default(backend_reqs.default_entry_reentrancy) ||
            !valid_reentrancy_default(backend_reqs.default_exit_reentrancy)) {
            error = "Backend '" + backend->info.name +
                    "' returned invalid default reentrancy (expected 'R' or 'N')";
            return false;
        }
        if (backend->validate_options) {
            std::string opt_error;
            backend->validate_options(options, opt_error);
            if (!opt_error.empty()) {
                error = opt_error;
                return false;
            }
        }

        AnalysisConfig analysis_config;
        analysis_config.enabled_passes = backend_reqs.required_passes;
        analysis_config.default_entry_context = backend_reqs.default_entry_reentrancy;
        analysis_config.default_exit_context = backend_reqs.default_exit_reentrancy;
        if (backend->boundary_reentrancy_mode) {
            analysis_config.reentrancy_mode_for_boundary =
                [&](const Symbol* sym, ReentrancyBoundaryKind boundary) -> ReentrancyMode {
                    if (!sym) {
                        return ReentrancyMode::Default;
                    }
                    std::string boundary_error;
                    ReentrancyMode mode =
                        backend->boundary_reentrancy_mode(*sym, boundary, options, boundary_error);
                    if (!boundary_error.empty()) {
                        SourceLocation loc = sym->declaration ? sym->declaration->location : SourceLocation();
                        throw CompileError(boundary_error, loc);
                    }
                    return mode;
                };
        }

        ModuleLoader loader(options.project_root);
        Program program = loader.load(options.input_file);

        Bindings bindings;
        Resolver resolver(program, bindings, options.project_root);
        TypeChecker checker(options.project_root, options.allow_process, &resolver, &bindings, &program);
        FrontendPipelineResult pipeline =
            run_frontend_pipeline(program, resolver, checker, options.verbose, analysis_config);

        OutputPaths paths = resolve_output_paths(options.output_file);
        if (options.emit_analysis) {
            std::filesystem::path analysis_path = paths.dir / (paths.stem + ".analysis.txt");
            write_text_file_or_throw(analysis_path.string(),
                                     format_analysis_report(pipeline.merged,
                                                            pipeline.analysis,
                                                            &pipeline.optimization));
        }

        AnalyzedProgram analyzed =
            make_analyzed_program(pipeline.merged, checker, pipeline.analysis, pipeline.optimization);
        BackendInput input{analyzed, options, paths};
        std::string backend_error;
        if (!backend->emit_translation_unit(input, out_translation_unit, backend_error)) {
            error = backend_error.empty()
                        ? "Backend '" + backend->info.name + "' failed to emit translation unit"
                        : backend_error;
            return false;
        }

        return true;
    } catch (const CompileError& e) {
        error = e.what();
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}
