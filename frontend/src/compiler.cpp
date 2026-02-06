#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include "typechecker.h"
#include "codegen.h"
#include "analysis.h"
#include "optimizer.h"
#include "analysis_report.h"
#include "pass_invariants.h"
#include "backend_registry.h"
#include "constants.h"
#include "lowered_printer.h"
#include "lowerer.h"
#include "monomorphizer.h"
#include "resolver.h"
#include "module_loader.h"
#include <functional>
#include <queue>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace vexel {

Compiler::Compiler(const Options& opts) : options(opts) {}

namespace {
#ifdef VEXEL_DEBUG_PASS_INVARIANTS
void validate_program_stage(const Program& program, const char* stage) {
    validate_program_invariants(program, stage);
}
void validate_module_stage(const Module& mod, const char* stage) {
    validate_module_invariants(mod, stage);
}
#else
void validate_program_stage(const Program&, const char*) {}
void validate_module_stage(const Module&, const char*) {}
#endif
}

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
    if (options.verbose) {
        std::cout << "Compiling: " << options.input_file << std::endl;
    }

    ModuleLoader loader(options.project_root);
    Program program = loader.load(options.input_file);
    validate_program_stage(program, "post-load");

    Bindings bindings;
    Resolver resolver(program, bindings, options.project_root);
    resolver.resolve();
    validate_program_stage(program, "post-resolve");

    if (options.verbose) {
        std::cout << "Type checking..." << std::endl;
    }

    TypeChecker checker(options.project_root, options.allow_process, &resolver, &bindings, &program);
    checker.check_program(program);
    validate_program_stage(program, "post-typecheck");

    Module merged;
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
    validate_module_stage(merged, "post-merge");

    Monomorphizer monomorphizer(&checker);
    monomorphizer.run(merged);
    validate_module_stage(merged, "post-monomorphize");

    Lowerer lowerer(&checker);
    lowerer.run(merged);
    validate_module_stage(merged, "post-lower");

    Optimizer optimizer(&checker);
    OptimizationFacts optimization = optimizer.run(merged);
    validate_module_stage(merged, "post-optimize");
    Analyzer analyzer(&checker, &optimization);
    AnalysisFacts analysis = analyzer.run(merged);
    validate_module_stage(merged, "post-analysis");
    checker.validate_type_usage(merged, analysis);
    validate_module_stage(merged, "post-type-use");

    OutputPaths paths = resolve_output_paths(options.output_file);
    if (options.emit_lowered) {
        std::filesystem::path lowered_path = paths.dir / (paths.stem + ".lowered.vx");
        std::string lowered = print_lowered_module(merged);
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
        write_file(analysis_path.string(), format_analysis_report(merged, analysis, &optimization));
    }

    const Backend* backend = find_backend(options.backend);
    if (!backend) {
        throw CompileError("Unknown backend: " + options.backend, SourceLocation());
    }
    if (options.verbose) {
        std::cout << "Generating backend: " << backend->info.name << std::endl;
    }
    BackendContext ctx{merged, checker, options, paths, analysis, optimization};
    backend->emit(ctx);

    if (options.verbose) {
        std::cout << "Compilation successful!" << std::endl;
    }

    return paths;
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
            if (!param.type) {
                throw CompileError("Missing type for parameter '" + param.name +
                                   "' when generating C signature", param.location);
            }
            std::string type = codegen.type_to_c(param.type);
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
