#include "template_backend.h"
#include "backend_registry.h"
#include "io_utils.h"
#include <filesystem>

namespace vexel {

static void emit_template(const BackendContext& ctx) {
    std::filesystem::path out_path = ctx.outputs.dir / (ctx.outputs.stem + ".txt");
    write_text_file_or_throw(out_path.string(), "backend=" + ctx.options.backend + "\n");
}

void register_backend_template() {
    Backend backend;
    backend.info.name = "template";
    backend.info.description = "Template backend";
    backend.info.version = "v0.2.1";
    backend.emit = emit_template;
    (void)register_backend(backend);
}

}
