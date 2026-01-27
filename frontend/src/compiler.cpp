#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include "typechecker.h"
#include "codegen.h"
#include "analysis.h"
#include "optimizer.h"
#include "analysis_report.h"
#include "backend_registry.h"
#include "constants.h"
#include "lowered_printer.h"
#include <functional>
#include <queue>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace vexel {

Compiler::Compiler(const Options& opts) : options(opts) {}

Compiler::OutputPaths Compiler::resolve_output_paths(const std::string& output_file) {
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

Compiler::OutputPaths Compiler::compile() {
    try {
        if (options.verbose) {
            std::cout << "Compiling: " << options.input_file << std::endl;
        }

        // Load main module
        Module mod = load_module(options.input_file);

        // Import processing now handled per-scope by type checker

        if (options.verbose) {
            std::cout << "Type checking..." << std::endl;
        }

        // Type check
        TypeChecker checker(options.project_root, options.allow_process);
        checker.check_module(mod);

        Analyzer analyzer(&checker);
        AnalysisFacts analysis = analyzer.run(mod);
        Optimizer optimizer(&checker);
        OptimizationFacts optimization = optimizer.run(mod);

        OutputPaths paths = resolve_output_paths(options.output_file);
        if (options.emit_lowered) {
            std::filesystem::path lowered_path = paths.dir / (paths.stem + ".lowered.vx");
            std::string lowered = print_lowered_module(mod);
            if (options.verbose) {
                std::cout << "Writing lowered module: " << lowered_path << std::endl;
            }
            write_file(lowered_path.string(), lowered);
        }
        if (options.emit_analysis) {
            std::filesystem::path analysis_path = paths.dir / (paths.stem + ".analysis.txt");
            if (options.verbose) {
                std::cout << "Writing analysis report: " << analysis_path << std::endl;
            }
            write_file(analysis_path.string(), format_analysis_report(mod, analysis, &optimization));
        }

        const Backend* backend = find_backend(options.backend);
        if (!backend) {
            throw CompileError("Unknown backend: " + options.backend, SourceLocation());
        }
        if (options.verbose) {
            std::cout << "Generating backend: " << backend->info.name << std::endl;
        }
        BackendContext ctx{mod, checker, options, paths, analysis, optimization};
        backend->emit(ctx);

        if (options.verbose) {
            std::cout << "Compilation successful!" << std::endl;
        }

        return paths;

    } catch (const CompileError& e) {
        std::cerr << "Error";
        if (!e.location.filename.empty()) {
            std::cerr << " at " << e.location.filename
                     << ":" << e.location.line
                     << ":" << e.location.column;
        }
        std::cerr << ": " << e.what() << std::endl;
        throw;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        throw;
    }
}

std::string Compiler::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw CompileError("Cannot open file: " + path, SourceLocation());
    }
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    );
}

void Compiler::write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw CompileError("Cannot write file: " + path, SourceLocation());
    }
    file << content;
}

Module Compiler::load_module(const std::string& path) {
    std::string source = read_file(path);

    if (options.verbose) {
        std::cout << "Lexing..." << std::endl;
    }

    Lexer lexer(source, path);
    std::vector<Token> tokens = lexer.tokenize();

    if (options.verbose) {
        std::cout << "Parsing..." << std::endl;
    }

    Parser parser(tokens);
    return parser.parse_module(path, path);
}

std::string Compiler::build_return_type(CodeGenerator& codegen, StmtPtr func) {
    if (!func) return "void";
    if (!func->return_types.empty()) {
        std::string tuple_name = std::string(TUPLE_TYPE_PREFIX) + std::to_string(func->return_types.size());
        for (const auto& t : func->return_types) {
            tuple_name += "_";
            if (t) {
                tuple_name += t->to_string();
            } else {
                tuple_name += "unknown";
            }
        }
        return codegen.mangle(tuple_name);
    }
    if (func->return_type) {
        return codegen.type_to_c(func->return_type);
    }
    return "void";
}

std::string Compiler::build_param_list(CodeGenerator& codegen, StmtPtr func, bool with_types) {
    if (!func) return "";
    std::ostringstream oss;
    bool first = true;

    for (size_t i = 0; i < func->ref_params.size(); ++i) {
        if (!first) oss << ", ";
        first = false;
        std::string name = codegen.mangle(func->ref_params[i]);
        if (with_types) {
            std::string ref_type = "void*";
            if (!func->type_namespace.empty() && i == 0) {
                ref_type = codegen.mangle(func->type_namespace) + "*";
            }
            oss << ref_type << " " << name;
        } else {
            oss << name;
        }
    }

    for (const auto& param : func->params) {
        if (param.is_expression_param) continue;
        if (!first) oss << ", ";
        first = false;
        std::string name = codegen.mangle(param.name);
        if (with_types) {
            std::string type = param.type ? codegen.type_to_c(param.type) : "int";
            oss << type << " " << name;
        } else {
            oss << name;
        }
    }

    if (first && with_types) {
        oss << "void";
    }
    return oss.str();
}

std::string Compiler::build_arg_list(CodeGenerator& codegen, StmtPtr func) {
    return build_param_list(codegen, func, false);
}

}
