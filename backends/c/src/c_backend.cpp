#include "c_backend.h"
#include "backend_registry.h"
#include "codegen.h"
#include "io_utils.h"
#include <filesystem>
#include <iostream>

namespace vexel {

using c_backend_codegen::CCodegenResult;
using c_backend_codegen::CodeGenerator;

static bool parse_c_backend_option(int, char**, int&, Compiler::Options&, std::string&) {
    return false;
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

void register_backend_c() {
    Backend backend;
    backend.info.name = "c";
    backend.info.description = "Portable C11 backend";
    backend.info.version = "v0.2.1";
    backend.emit = emit_c_backend;
    backend.parse_option = parse_c_backend_option;
    backend.print_usage = print_c_backend_usage;
    (void)register_backend(backend);
}

}
