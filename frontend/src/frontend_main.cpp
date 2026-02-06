#include "typechecker.h"
#include "resolver.h"
#include "module_loader.h"
#include "optimizer.h"
#include "analysis.h"
#include "lowered_printer.h"
#include "lowerer.h"
#include "monomorphizer.h"
#include <iostream>
#include <cstring>

// Minimal front-end CLI: lex/parse/type-check and emit lowered IR.
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
        resolver.resolve();

        if (verbose) std::cout << "Type checking...\n";
        vexel::TypeChecker checker(project_root, allow_process, &resolver, &bindings, &program);
        checker.check_program(program);

        vexel::Module merged;
        if (!program.modules.empty()) {
            merged.name = program.modules.front().module.name;
            merged.path = program.modules.front().path;
            for (const auto& instance : program.instances) {
                const auto& mod_info = program.modules[static_cast<size_t>(instance.module_id)];
                for (const auto& stmt : mod_info.module.top_level) {
                    merged.top_level.push_back(stmt);
                }
            }
        }

        vexel::Monomorphizer monomorphizer(&checker);
        monomorphizer.run(merged);

        vexel::Lowerer lowerer(&checker);
        lowerer.run(merged);

        vexel::Optimizer optimizer(&checker);
        vexel::OptimizationFacts optimization = optimizer.run(merged);
        vexel::Analyzer analyzer(&checker, &optimization);
        vexel::AnalysisFacts analysis = analyzer.run(merged);
        checker.validate_type_usage(merged, analysis);

        std::cout << vexel::print_lowered_module(merged);
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
