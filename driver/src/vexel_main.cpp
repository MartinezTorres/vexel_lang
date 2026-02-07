#include "backend_registry.h"
#include "compiler.h"
#include <iostream>
#include <cstring>
#include <vector>

// Unified CLI contract (source of truth):
// - Backend selection is mandatory (`-b/--backend`); there is no default backend.
// - Frontend-owned flags are parsed here.
// - Unknown flags are delegated to the selected backend via Backend::parse_option.
// - When parsing fails, show frontend usage plus backend-specific usage lines.
namespace {

bool parse_backend_opt(const char* opt, vexel::Compiler::Options& opts, std::string& error) {
    const char* eq = std::strchr(opt, '=');
    if (!eq || eq == opt || *(eq + 1) == '\0') {
        error = "--backend-opt expects key=value";
        return false;
    }
    std::string key(opt, eq - opt);
    std::string value(eq + 1);
    opts.backend_options[key] = value;
    return true;
}

void print_usage(const char* prog,
                 const std::vector<vexel::BackendInfo>& backends,
                 const vexel::Backend* selected_backend = nullptr) {
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
    std::cout << "  -L           Emit lowered Vexel subset alongside backend output\n";
    std::cout << "  --emit-analysis Emit analysis report alongside backend output\n";
    std::cout << "  --allow-process Enable process expressions (executes host commands; disabled by default)\n";
    std::cout << "  --backend-opt <k=v> Backend-specific option (repeatable)\n";
    std::cout << "  -v           Verbose output\n";
    std::cout << "  -h           Show this help\n";
    if (selected_backend && selected_backend->print_usage) {
        std::cout << "\nBackend-specific options (" << selected_backend->info.name << "):\n";
        selected_backend->print_usage(std::cout);
    }
}

bool read_backend_from_arg(int argc, char** argv, int& i, std::string& out_backend, std::string& error) {
    if (std::strcmp(argv[i], "-b") == 0 || std::strcmp(argv[i], "--backend") == 0) {
        if (i + 1 >= argc) {
            error = "-b/--backend requires an argument";
            return false;
        }
        out_backend = argv[++i];
        return true;
    }
    constexpr const char* kPrefix = "--backend=";
    if (std::strncmp(argv[i], kPrefix, std::strlen(kPrefix)) == 0) {
        const char* value = argv[i] + std::strlen(kPrefix);
        if (*value == '\0') {
            error = "--backend requires a non-empty value";
            return false;
        }
        out_backend = value;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<vexel::BackendInfo> available_backends = vexel::list_backends();
    if (available_backends.empty()) {
        std::cerr << "No backends available\n";
        return 1;
    }

    bool help_requested = false;
    std::string selected_backend_name;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            help_requested = true;
            continue;
        }
        std::string parsed_backend;
        std::string parse_error;
        if (read_backend_from_arg(argc, argv, i, parsed_backend, parse_error)) {
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
        print_usage(argv[0], available_backends, selected_backend);
        return 0;
    }

    vexel::Compiler::Options opts;
    opts.output_file = "out";
    opts.backend = selected_backend_name;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            continue;
        }
        if (std::strcmp(argv[i], "-v") == 0) {
            opts.verbose = true;
        } else if (std::strcmp(argv[i], "-L") == 0 || std::strcmp(argv[i], "--emit-lowered") == 0) {
            opts.emit_lowered = true;
        } else if (std::strcmp(argv[i], "--emit-analysis") == 0) {
            opts.emit_analysis = true;
        } else if (std::strcmp(argv[i], "--allow-process") == 0) {
            opts.allow_process = true;
        } else if (std::strcmp(argv[i], "--backend-opt") == 0 || std::strncmp(argv[i], "--backend-opt=", 14) == 0) {
            const char* opt = nullptr;
            if (std::strncmp(argv[i], "--backend-opt=", 14) == 0) {
                opt = argv[i] + 14;
            } else if (i + 1 < argc) {
                opt = argv[++i];
            } else {
                std::cerr << "Error: --backend-opt requires an argument\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
            std::string parse_error;
            if (!parse_backend_opt(opt, opts, parse_error)) {
                std::cerr << "Error: " << parse_error << "\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
        } else if (std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            } else {
                std::cerr << "Error: -o requires an argument\n";
                print_usage(argv[0], available_backends, selected_backend);
                return 1;
            }
        } else if (std::strcmp(argv[i], "-b") == 0 || std::strcmp(argv[i], "--backend") == 0 ||
                   std::strncmp(argv[i], "--backend=", std::strlen("--backend=")) == 0) {
            std::string backend_name;
            std::string parse_error;
            if (!read_backend_from_arg(argc, argv, i, backend_name, parse_error)) {
                std::cerr << "Error: " << parse_error << "\n";
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

    try {
        vexel::Compiler compiler(opts);
        (void)compiler.compile();
        return 0;
    } catch (const vexel::CompileError& e) {
        std::cerr << "Error";
        if (!e.location.filename.empty()) {
            std::cerr << " at " << e.location.filename
                     << ":" << e.location.line
                     << ":" << e.location.column;
        }
        std::cerr << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
