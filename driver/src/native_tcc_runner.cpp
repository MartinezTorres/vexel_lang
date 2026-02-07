#include "native_tcc_runner.h"

#include "analysis_report.h"
#include "codegen.h"
#include "frontend_pipeline.h"
#include "io_utils.h"
#include "lowered_printer.h"
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

int compile_to_c(const Compiler::Options& opts, CCodegenResult& out, std::ostream& err) {
    try {
        ModuleLoader loader(opts.project_root);
        Program program = loader.load(opts.input_file);

        Bindings bindings;
        Resolver resolver(program, bindings, opts.project_root);
        TypeChecker checker(opts.project_root, opts.allow_process, &resolver, &bindings, &program);
        FrontendPipelineResult pipeline = run_frontend_pipeline(program, resolver, checker, opts.verbose);

        Compiler::OutputPaths paths = resolve_output_paths(opts.output_file);
        if (opts.emit_lowered) {
            std::filesystem::path lowered_path = paths.dir / (paths.stem + ".lowered.vx");
            write_text_file_or_throw(lowered_path.string(), print_lowered_module(pipeline.merged));
        }
        if (opts.emit_analysis) {
            std::filesystem::path analysis_path = paths.dir / (paths.stem + ".analysis.txt");
            write_text_file_or_throw(analysis_path.string(),
                                     format_analysis_report(pipeline.merged,
                                                            pipeline.analysis,
                                                            &pipeline.optimization));
        }

        CodeGenerator codegen;
        out = codegen.generate(pipeline.merged, &checker, &pipeline.analysis, &pipeline.optimization);
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
    CCodegenResult c_result;
    int compile_status = compile_to_c(opts, c_result, err);
    if (compile_status != 0) {
        return compile_status;
    }

    std::string full_c_unit = c_result.header + "\n" + c_result.source;

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

    if (tcc_compile_string(state, full_c_unit.c_str()) < 0) {
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
