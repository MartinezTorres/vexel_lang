#include "typechecker.h"
#include "resolver.h"
#include "module_loader.h"
#include "frontend_pipeline.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace {

bool parse_type_strictness_value(const char* value, int& out_level) {
    if (!value || *value == '\0') return false;
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "relaxed") == 0) {
        out_level = 0;
        return true;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "annotated-locals") == 0) {
        out_level = 1;
        return true;
    }
    if (std::strcmp(value, "2") == 0 || std::strcmp(value, "full") == 0) {
        out_level = 2;
        return true;
    }
    return false;
}

} // namespace

// Minimal front-end CLI: lex/parse/type-check and report diagnostics only.
static void print_usage(const char* prog) {
    std::cout << "Vexel Frontend\n";
    std::cout << "Usage: " << prog << " [options] <input.vx>\n\n";
    std::cout << "Options:\n";
    std::cout << "  --allow-process Enable process expressions (executes host commands; disabled by default)\n";
    std::cout << "  --type-strictness <0|1|2> Literal/type strictness (0 relaxed, 1 annotated-locals, 2 full)\n";
    std::cout << "  --strict-types[=full] Alias for --type-strictness=1 (or 2 with '=full')\n";
    std::cout << "  -v           Verbose output\n";
    std::cout << "  -h           Show this help\n";
}

int main(int argc, char** argv) {
    bool allow_process = false;
    bool verbose = false;
    int type_strictness = 0;
    std::string input_file;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (std::strcmp(argv[i], "--allow-process") == 0) {
            allow_process = true;
        } else if (std::strcmp(argv[i], "--strict-types") == 0) {
            type_strictness = std::max(type_strictness, 1);
        } else if (std::strncmp(argv[i], "--strict-types=", std::strlen("--strict-types=")) == 0) {
            const char* value = argv[i] + std::strlen("--strict-types=");
            if (std::strcmp(value, "full") == 0) {
                type_strictness = std::max(type_strictness, 2);
            } else {
                int parsed = 0;
                if (!parse_type_strictness_value(value, parsed)) {
                    std::cerr << "Error: --strict-types expects no value, '=full', or '=2'\n";
                    return 1;
                }
                type_strictness = std::max(type_strictness, parsed);
            }
        } else if (std::strcmp(argv[i], "--type-strictness") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --type-strictness requires an argument\n";
                return 1;
            }
            int parsed = 0;
            if (!parse_type_strictness_value(argv[++i], parsed)) {
                std::cerr << "Error: --type-strictness expects one of: 0,1,2 (or relaxed,annotated-locals,full)\n";
                return 1;
            }
            type_strictness = parsed;
        } else if (std::strncmp(argv[i], "--type-strictness=", std::strlen("--type-strictness=")) == 0) {
            int parsed = 0;
            if (!parse_type_strictness_value(argv[i] + std::strlen("--type-strictness="), parsed)) {
                std::cerr << "Error: --type-strictness expects one of: 0,1,2 (or relaxed,annotated-locals,full)\n";
                return 1;
            }
            type_strictness = parsed;
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

        vexel::TypeChecker checker(project_root,
                                   allow_process,
                                   &resolver,
                                   &bindings,
                                   &program,
                                   type_strictness);
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
