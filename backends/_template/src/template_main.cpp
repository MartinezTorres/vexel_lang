#include "template_backend.h"
#include "cli_utils.h"
#include <cstring>
#include <iostream>

static void print_usage(const char* prog) {
    std::cout << "Vexel Compiler (template backend)\n";
    std::cout << "Usage: " << prog << " [options] <input.vx>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o <path>    Output path (base name for generated files, default: out)\n";
    std::cout << "  -b <name>    Backend (optional compatibility flag: accepts template only)\n";
    std::cout << "  -L           Emit lowered Vexel subset alongside backend output\n";
    std::cout << "  --emit-analysis Emit analysis report alongside backend output\n";
    std::cout << "  --allow-process Enable process expressions (executes host commands; disabled by default)\n";
    std::cout << "  --backend-opt <k=v> Backend-specific option (repeatable)\n";
    std::cout << "  -v           Verbose output\n";
    std::cout << "  -h           Show this help\n";
}

int main(int argc, char** argv) {
    vexel::register_backend_template();

    vexel::Compiler::Options opts;
    opts.output_file = "out";
    opts.backend = "template";

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        std::string parse_error;
        if (vexel::try_parse_common_compiler_option(argc, argv, i, opts, parse_error)) {
            if (!parse_error.empty()) {
                std::cerr << "Error: " << parse_error << "\n";
                return 1;
            }
        } else if (vexel::try_parse_backend_opt_arg(argc, argv, i, opts, parse_error)) {
            if (!parse_error.empty()) {
                std::cerr << "Error: " << parse_error << "\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "-b") == 0 || std::strcmp(argv[i], "--backend") == 0) {
            std::string backend_name;
            if (!vexel::try_read_backend_arg(argc, argv, i, backend_name, parse_error)) {
                std::cerr << "Error: Failed to parse backend argument\n";
                return 1;
            }
            if (!parse_error.empty()) {
                std::cerr << "Error: " << parse_error << "\n";
                return 1;
            }
            if (backend_name != "template") {
                std::cerr << "Error: template CLI only supports backend=template\n";
                return 1;
            }
        } else if (argv[i][0] == '-') {
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            return 1;
        } else {
            if (!opts.input_file.empty()) {
                std::cerr << "Error: Multiple input files specified ('" << opts.input_file
                          << "' and '" << argv[i] << "')\n";
                return 1;
            }
            opts.input_file = argv[i];
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: No input file specified\n";
        print_usage(argv[0]);
        return 1;
    }

    return vexel::run_compiler_with_diagnostics(opts, std::cerr);
}
