#include "backend_registry.h"
#include "cli_utils.h"
#include "native_tcc_runner.h"
#include <iostream>
#include <cstring>
#include <vector>

// Unified CLI contract (source of truth):
// - Backend selection is mandatory (`-b/--backend`); there is no default backend.
// - Frontend-owned flags are parsed here.
// - Unknown flags are delegated to the selected backend via Backend::parse_option.
// - When parsing fails, show frontend usage plus backend-specific usage lines.
namespace {

void print_usage(const char* prog,
                 const std::vector<vexel::BackendInfo>& backends,
                 const vexel::Backend* selected_backend = nullptr,
                 bool show_all_backend_usage = false) {
    const bool has_native_tcc = vexel::native_tcc_supported();
    std::cout << "Vexel Compiler (multi-backend)\n";
    std::cout << "Usage: " << prog << " [options] <input.vx>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o <path>    Output path (base name for generated files, default: out)\n";
    std::cout << "  -b <name>    Backend (required): ";
    for (size_t i = 0; i < backends.size(); ++i) {
        std::cout << backends[i].name;
        if (i + 1 < backends.size()) std::cout << ", ";
    }
    std::cout << "\n";
    std::cout << "  --emit-analysis Emit analysis report alongside backend output\n";
    std::cout << "  --allow-process Enable process expressions (executes host commands; disabled by default)\n";
    std::cout << "  --backend-opt <k=v> Backend-specific option (repeatable)\n";
    if (has_native_tcc) {
        std::cout << "  --run         Compile with backend c and run in-process via libtcc (no .c/.h output)\n";
        std::cout << "  --emit-exe    Compile with backend c and emit native executable via libtcc\n";
    }
    std::cout << "  -v           Verbose output\n";
    std::cout << "  -h           Show this help\n";
    if (selected_backend && selected_backend->print_usage) {
        std::cout << "\nBackend-specific options (" << selected_backend->info.name << "):\n";
        selected_backend->print_usage(std::cout);
    } else if (show_all_backend_usage) {
        std::cout << "\nBackend-specific options:\n";
        for (const auto& info : backends) {
            const vexel::Backend* backend = vexel::find_backend(info.name);
            if (!backend || !backend->print_usage) continue;
            std::cout << "\n  [" << info.name << "]";
            if (!info.description.empty()) {
                std::cout << " " << info.description;
            }
            std::cout << "\n";
            backend->print_usage(std::cout);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<vexel::BackendInfo> available_backends = vexel::list_backends();
    if (available_backends.empty()) {
        std::cerr << "No backends available\n";
        return 1;
    }

    bool help_requested = false;
    bool run_requested = false;
    bool emit_exe_requested = false;
    std::string selected_backend_name;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            help_requested = true;
            continue;
        }
        if (std::strcmp(argv[i], "--run") == 0) {
            run_requested = true;
            continue;
        }
        if (std::strcmp(argv[i], "--emit-exe") == 0) {
            emit_exe_requested = true;
            continue;
        }
        std::string parsed_backend;
        std::string parse_error;
        if (vexel::try_read_backend_arg(argc, argv, i, parsed_backend, parse_error)) {
            if (!parse_error.empty()) {
                std::cerr << "Error: " << parse_error << "\n";
                print_usage(argv[0], available_backends);
                return 1;
            }
            if (!selected_backend_name.empty() && selected_backend_name != parsed_backend) {
                std::cerr << "Error: Conflicting backend selections: '" << selected_backend_name
                          << "' and '" << parsed_backend << "'\n";
                print_usage(argv[0], available_backends);
                return 1;
            }
            selected_backend_name = parsed_backend;
        }
    }

    if (run_requested && emit_exe_requested) {
        std::cerr << "Error: --run and --emit-exe cannot be used together\n";
        print_usage(argv[0], available_backends);
        return 1;
    }
    bool native_mode = run_requested || emit_exe_requested;

    const vexel::Backend* selected_backend = nullptr;
    if (!selected_backend_name.empty()) {
        selected_backend = vexel::find_backend(selected_backend_name);
        if (!selected_backend) {
            std::cerr << "Error: Unknown backend '" << selected_backend_name << "'\n";
            print_usage(argv[0], available_backends);
            return 1;
        }
    } else if (!help_requested) {
        std::cerr << "Error: Backend must be specified with -b/--backend\n";
        print_usage(argv[0], available_backends);
        return 1;
    }

    if (help_requested) {
        print_usage(argv[0], available_backends, selected_backend, selected_backend == nullptr);
        return 0;
    }
    if (native_mode) {
        if (selected_backend_name != "c") {
            std::cerr << "Error: --run/--emit-exe require backend 'c'\n";
            print_usage(argv[0], available_backends, selected_backend);
            return 1;
        }
        if (!vexel::native_tcc_supported()) {
            std::cerr << "Error: --run/--emit-exe are unavailable in this build (libtcc+tcc runtime not detected)\n";
            return 1;
        }
    }

    vexel::Compiler::Options opts;
    opts.output_file = "out";
    opts.backend = selected_backend_name;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            continue;
        }
        if (std::strcmp(argv[i], "--run") == 0 || std::strcmp(argv[i], "--emit-exe") == 0) {
            continue;
        }
        std::string parse_error;
        if (vexel::try_parse_common_compiler_option(argc, argv, i, opts, parse_error)) {
            if (!parse_error.empty()) {
                std::cerr << "Error: " << parse_error << "\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
        } else if (vexel::try_parse_backend_opt_arg(argc, argv, i, opts, parse_error)) {
            if (!parse_error.empty()) {
                std::cerr << "Error: " << parse_error << "\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
        } else if (std::strcmp(argv[i], "-b") == 0 || std::strcmp(argv[i], "--backend") == 0 ||
                   std::strncmp(argv[i], "--backend=", std::strlen("--backend=")) == 0) {
            std::string backend_name;
            std::string backend_parse_error;
            if (!vexel::try_read_backend_arg(argc, argv, i, backend_name, backend_parse_error)) {
                std::cerr << "Error: Failed to parse backend argument\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
            if (!backend_parse_error.empty()) {
                std::cerr << "Error: " << backend_parse_error << "\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
            if (backend_name != selected_backend_name) {
                std::cerr << "Error: Conflicting backend selection '" << backend_name
                          << "' (expected '" << selected_backend_name << "')\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
        } else if (argv[i][0] == '-') {
            if (selected_backend && selected_backend->parse_option) {
                int backend_idx = i;
                std::string backend_error;
                bool handled = selected_backend->parse_option(argc, argv, backend_idx, opts, backend_error);
                if (handled) {
                    if (!backend_error.empty()) {
                        std::cerr << "Error: " << backend_error << "\n";
                        print_usage(argv[0], available_backends, selected_backend);
                        return 1;
                    }
                    i = backend_idx;
                    continue;
                }
            }
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            print_usage(argv[0], available_backends, selected_backend);
            return 1;
        } else {
            if (!opts.input_file.empty()) {
                std::cerr << "Error: Multiple input files specified ('" << opts.input_file
                          << "' and '" << argv[i] << "')\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
            opts.input_file = argv[i];
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: No input file specified\n";
        print_usage(argv[0], available_backends, selected_backend);
        return 1;
    }

    if (native_mode) {
        vexel::NativeTccMode mode = run_requested ? vexel::NativeTccMode::Run
                                                  : vexel::NativeTccMode::EmitExe;
        return vexel::run_native_with_tcc(opts, mode, std::cerr);
    }

    return vexel::run_compiler_with_diagnostics(opts, std::cerr);
}
