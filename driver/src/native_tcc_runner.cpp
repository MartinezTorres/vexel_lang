#include "native_tcc_runner.h"

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
int compile_to_translation_unit(const Compiler::Options& opts,
                                std::string& out,
                                std::ostream& err) {
    Compiler compiler(opts);
    std::string compile_error;
    if (!compiler.emit_translation_unit(out, compile_error)) {
        err << "Error: " << compile_error << "\n";
        return 1;
    }
    return 0;
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
