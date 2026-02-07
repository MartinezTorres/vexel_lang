#include "c_backend.h"
#include "backend_registry.h"
#include "codegen.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace vexel {

static bool parse_c_backend_option(int, char**, int&, Compiler::Options&, std::string&) {
    return false;
}

static void print_c_backend_usage(std::ostream& os) {
    os << "  (none)\n";
}

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw CompileError("Cannot write file: " + path, SourceLocation());
    }
    file << content;
}

static void emit_c_backend(const BackendContext& ctx) {
    CodeGenerator codegen;
    CCodegenResult result = codegen.generate(ctx.module, &ctx.checker, &ctx.analysis, &ctx.optimization);

    std::filesystem::path header_path = ctx.outputs.dir / (ctx.outputs.stem + ".h");
    std::filesystem::path source_path = ctx.outputs.dir / (ctx.outputs.stem + ".c");

    if (ctx.options.verbose) {
        std::cout << "Writing header: " << header_path << std::endl;
        std::cout << "Writing source: " << source_path << std::endl;
    }

    write_file(header_path.string(), result.header);
    std::string source_with_include =
        "#include \"" + header_path.filename().string() + "\"\n\n" + result.source;
    write_file(source_path.string(), source_with_include);
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
