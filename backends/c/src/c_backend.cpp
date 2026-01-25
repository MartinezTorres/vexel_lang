#include "c_backend.h"
#include "backend_registry.h"
#include "codegen.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace vexel {

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw CompileError("Cannot write file: " + path, SourceLocation());
    }
    file << content;
}

static void emit_c_backend(const BackendContext& ctx) {
    CodeGenerator codegen;
    codegen.set_non_reentrant(ctx.non_reentrant_funcs);
    CCodegenResult result = codegen.generate(ctx.module, &ctx.checker);

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
    (void)register_backend(backend);
}

}
