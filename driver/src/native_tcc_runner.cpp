#include "native_tcc_runner.h"

#include "analysis_report.h"
#include "analyzed_program_builder.h"
#include "backend_registry.h"
#include "frontend_pipeline.h"
#include "io_utils.h"
#include "module_loader.h"
#include "resolver.h"
#include "typechecker.h"

#include <filesystem>
#include <string>

#if VEXEL_HAS_LIBTCC
#include <libtcc.h>
#endif

#if VEXEL_HAS_LIBTCC && defined(VEXEL_HAS_TCC_RUNTIME) && VEXEL_HAS_TCC_RUNTIME
#define VEXEL_HAS_NATIVE_TCC 1
#else
#define VEXEL_HAS_NATIVE_TCC 0
#endif

namespace vexel {

namespace {

#if VEXEL_HAS_NATIVE_TCC
Compiler::OutputPaths resolve_output_paths(const std::string& output_file) {
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

int compile_to_translation_unit(const Compiler::Options& opts,
                                std::string& out,
                                std::ostream& err) {
    try {
        const Backend* backend = find_backend(opts.backend);
        if (!backend) {
            err << "Error: Unknown backend '" << opts.backend << "'\n";
            return 1;
        }
        if (!backend->emit_translation_unit) {
            err << "Error: backend '" << backend->info.name
                << "' does not support native translation-unit output\n";
            return 1;
        }

        BackendAnalysisRequirements backend_reqs{};
        if (backend->analysis_requirements) {
            std::string req_error;
            backend_reqs = backend->analysis_requirements(opts, req_error);
            if (!req_error.empty()) {
                err << "Error: " << req_error << "\n";
                return 1;
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
                        backend->boundary_reentrancy_mode(*sym, boundary, opts, boundary_error);
                    if (!boundary_error.empty()) {
                        SourceLocation loc = sym->declaration ? sym->declaration->location : SourceLocation();
                        throw CompileError(boundary_error, loc);
                    }
                    return mode;
                };
        }

        ModuleLoader loader(opts.project_root);
        Program program = loader.load(opts.input_file);

        Bindings bindings;
        Resolver resolver(program, bindings, opts.project_root);
        TypeChecker checker(opts.project_root, opts.allow_process, &resolver, &bindings, &program);
        FrontendPipelineResult pipeline =
            run_frontend_pipeline(program, resolver, checker, opts.verbose, analysis_config);

        Compiler::OutputPaths paths = resolve_output_paths(opts.output_file);
        if (opts.emit_analysis) {
            std::filesystem::path analysis_path = paths.dir / (paths.stem + ".analysis.txt");
            write_text_file_or_throw(analysis_path.string(),
                                     format_analysis_report(pipeline.merged,
                                                            pipeline.analysis,
                                                            &pipeline.optimization));
        }

        AnalyzedProgram analyzed =
            make_analyzed_program(pipeline.merged, checker, pipeline.analysis, pipeline.optimization);
        BackendInput input{analyzed, opts, paths};
        std::string backend_error;
        if (!backend->emit_translation_unit(input, out, backend_error)) {
            if (!backend_error.empty()) {
                err << "Error: " << backend_error << "\n";
            } else {
                err << "Error: backend '" << backend->info.name
                    << "' failed to emit translation unit\n";
            }
            return 1;
        }
        return 0;
    } catch (const CompileError& e) {
        err << "Error";
        if (!e.location.filename.empty()) {
            err << " at " << e.location.filename
                << ":" << e.location.line
                << ":" << e.location.column;
        }
        err << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        err << "Error: " << e.what() << "\n";
        return 1;
    }
}
#endif

#if VEXEL_HAS_NATIVE_TCC
void tcc_error_callback(void* opaque, const char* msg) {
    auto* err = static_cast<std::ostream*>(opaque);
    if (err && msg) {
        (*err) << "tcc: " << msg << "\n";
    }
}

void configure_tcc_include_paths(TCCState* state) {
    (void)tcc_add_sysinclude_path(state, "/usr/include");
    (void)tcc_add_sysinclude_path(state, "/usr/local/include");
#if defined(VEXEL_GCC_SYS_INCLUDE_DIR)
    (void)tcc_add_sysinclude_path(state, VEXEL_GCC_SYS_INCLUDE_DIR);
#endif
}
#endif

} // namespace

bool native_tcc_supported() {
#if VEXEL_HAS_NATIVE_TCC
    return true;
#else
    return false;
#endif
}

int run_native_with_tcc(const Compiler::Options& opts, NativeTccMode mode, std::ostream& err) {
#if !VEXEL_HAS_LIBTCC
    (void)opts;
    (void)mode;
    err << "Error: this vexel build does not include libtcc support\n";
    return 1;
#elif !VEXEL_HAS_NATIVE_TCC
    (void)opts;
    (void)mode;
    err << "Error: this vexel build is missing tcc runtime support files (libtcc1.a)\n";
    return 1;
#else
    std::string translation_unit;
    int compile_status = compile_to_translation_unit(opts, translation_unit, err);
    if (compile_status != 0) {
        return compile_status;
    }

    TCCState* state = tcc_new();
    if (!state) {
        err << "Error: failed to initialize libtcc state\n";
        return 1;
    }
    tcc_set_error_func(state, &err, tcc_error_callback);
    configure_tcc_include_paths(state);
#if defined(VEXEL_TCC_RUNTIME_DIR)
    tcc_set_lib_path(state, VEXEL_TCC_RUNTIME_DIR);
#endif

    int output_type = (mode == NativeTccMode::Run) ? TCC_OUTPUT_MEMORY : TCC_OUTPUT_EXE;
    if (tcc_set_output_type(state, output_type) < 0) {
        tcc_delete(state);
        return 1;
    }

    if (tcc_compile_string(state, translation_unit.c_str()) < 0) {
        tcc_delete(state);
        return 1;
    }

    // Many generated programs do not need libm, but adding it here keeps math calls portable.
    (void)tcc_add_library(state, "m");

    int status = 0;
    if (mode == NativeTccMode::Run) {
        char arg0[] = "vexel";
        char* argv[] = {arg0, nullptr};
        int run_status = tcc_run(state, 1, argv);
        if (run_status < 0) {
            err << "Error: libtcc failed to run compiled program\n";
            status = 1;
        } else {
            status = run_status;
        }
    } else {
        if (tcc_output_file(state, opts.output_file.c_str()) < 0) {
            status = 1;
        }
    }

    tcc_delete(state);
    return status;
#endif
}

} // namespace vexel
