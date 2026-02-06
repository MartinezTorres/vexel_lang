#include "lexer.h"
#include "parser.h"
#include "typechecker.h"
#include "lowered_printer.h"
#include "lowerer.h"
#include "monomorphizer.h"
#include <iostream>
#include <cstring>
#include <fstream>

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
        if (verbose) std::cout << "Lexing...\n";
        std::string source;
        {
            std::ifstream file(input_file);
            if (!file) {
                throw vexel::CompileError("Cannot open file: " + input_file, vexel::SourceLocation());
            }
            source.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        vexel::Lexer lexer(source, input_file);
        std::vector<vexel::Token> tokens = lexer.tokenize();
        if (verbose) std::cout << "Parsing...\n";
        vexel::Parser parser(tokens);
        vexel::Module mod = parser.parse_module(input_file, input_file);

        if (verbose) std::cout << "Type checking...\n";
        vexel::TypeChecker checker(".", allow_process);
        checker.check_module(mod);

        vexel::Monomorphizer monomorphizer(&checker);
        monomorphizer.run(mod);

        vexel::Lowerer lowerer(&checker);
        lowerer.run(mod);

        std::cout << vexel::print_lowered_module(mod);
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
