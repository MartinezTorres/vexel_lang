#include "typechecker.h"
#include "resolver.h"
#include "module_loader.h"
#include "frontend_pipeline.h"
#include <iostream>
#include <cstring>

// Minimal front-end CLI: lex/parse/type-check and report diagnostics only.
static void print_usage(const char* prog) {
    std::cout << "Vexel Frontend\n";
    std::cout << "Usage: " << prog << " [options] <input.vx>\n\n";
    std::cout << "Options:\n";
    std::cout << "  --allow-process Enable process expressions (executes host commands; disabled by default)\n";
    std::cout << "  -v           Verbose output\n";
    std::cout << "  -h           Show this help\n";
}

int main(int argc, char** argv) {
    bool allow_process = false;
    bool verbose = false;
    std::string input_file;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (std::strcmp(argv[i], "--allow-process") == 0) {
            allow_process = true;
        } else if (argv[i][0] == '-') {
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            return 1;
        } else {
            if (!input_file.empty()) {
                std::cerr << "Error: Multiple input files specified ('" << input_file
                          << "' and '" << argv[i] << "')\n";
                return 1;
            }
            input_file = argv[i];
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: No input file specified\n";
        print_usage(argv[0]);
        return 1;
    }

    try {
        std::string project_root = ".";

        if (verbose) std::cout << "Loading modules...\n";
        vexel::ModuleLoader loader(project_root);
        vexel::Program program = loader.load(input_file);

        vexel::Bindings bindings;
        if (verbose) std::cout << "Resolving...\n";
        vexel::Resolver resolver(program, bindings, project_root);

        vexel::TypeChecker checker(project_root, allow_process, &resolver, &bindings, &program);
        (void)vexel::run_frontend_pipeline(program, resolver, checker, verbose);
        return 0;
    } catch (const vexel::CompileError& e) {
        std::cerr << "Error";
        if (!e.location.filename.empty()) {
            std::cerr << " at " << e.location.filename << ":" << e.location.line << ":" << e.location.column;
        }
        std::cerr << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
