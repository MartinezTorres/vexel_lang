#include "c_backend.h"
#include "backend_registry.h"
#include "codegen.h"
#include "io_utils.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace vexel {

using c_backend_codegen::CCodegenResult;
using c_backend_codegen::CodeGenerator;

namespace {

static bool has_annotation(const std::vector<Annotation>& anns, const std::string& name) {
    return std::any_of(anns.begin(), anns.end(),
                       [&](const Annotation& ann) { return ann.name == name; });
}

static bool parse_c_backend_option(int, char**, int&, Compiler::Options&, std::string&) {
    return false;
}

static void validate_c_backend_options(const Compiler::Options& options, std::string& error) {
    if (options.backend_options.empty()) return;
    const auto& entry = *options.backend_options.begin();
    error = "C backend does not accept backend options (unknown key: " + entry.first + ")";
}

static void print_c_backend_usage(std::ostream& os) {
    os << "  (none)\n";
}

static void emit_c_backend(const BackendInput& input) {
    const AnalyzedProgram& program = input.program;
    CodeGenerator codegen;
    CCodegenResult result = codegen.generate(*program.module, program);

    std::filesystem::path header_path = input.outputs.dir / (input.outputs.stem + ".h");
    std::filesystem::path source_path = input.outputs.dir / (input.outputs.stem + ".c");
    if (input.options.verbose) {
        std::cout << "Writing header: " << header_path << std::endl;
        std::cout << "Writing source: " << source_path << std::endl;
    }

    write_text_file_or_throw(header_path.string(), result.header);
    std::string source_with_include =
        "#include \"" + header_path.filename().string() + "\"\n\n" + result.source;
    write_text_file_or_throw(source_path.string(), source_with_include);
}

static bool emit_c_translation_unit(const BackendInput& input,
                                    std::string& out_translation_unit,
                                    std::string& error) {
    try {
        const AnalyzedProgram& program = input.program;
        CodeGenerator codegen;
        CCodegenResult result = codegen.generate(*program.module, program);
        out_translation_unit = result.header + "\n" + result.source;
        return true;
    } catch (const CompileError& e) {
        error = e.what();
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

static BackendAnalysisRequirements c_analysis_requirements(const Compiler::Options&,
                                                           std::string&) {
    BackendAnalysisRequirements req;
    req.required_passes = kAllAnalysisPasses;
    req.default_entry_reentrancy = 'R';
    req.default_exit_reentrancy = 'R';
    return req;
}

static ReentrancyMode c_boundary_reentrancy_mode(const Symbol& sym,
                                                 ReentrancyBoundaryKind boundary,
                                                 const Compiler::Options&,
                                                 std::string& error) {
    (void)boundary;
    (void)error;
    if (!sym.declaration) {
        return ReentrancyMode::Default;
    }
    bool is_nonreentrant = has_annotation(sym.declaration->annotations, "nonreentrant");
    if (is_nonreentrant) return ReentrancyMode::NonReentrant;
    return ReentrancyMode::Default;
}

} // namespace

void register_backend_c() {
    Backend backend;
    backend.info.name = "c";
    backend.info.description = "Portable C11 backend";
    backend.info.version = "v0.2.1";
    backend.emit = emit_c_backend;
    backend.emit_translation_unit = emit_c_translation_unit;
    backend.analysis_requirements = c_analysis_requirements;
    backend.boundary_reentrancy_mode = c_boundary_reentrancy_mode;
    backend.validate_options = validate_c_backend_options;
    backend.parse_option = parse_c_backend_option;
    backend.print_usage = print_c_backend_usage;
    (void)register_backend(backend);
}

}
