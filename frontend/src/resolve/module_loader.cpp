#include "ast_walk.h"
#include "module_loader.h"
#include "expr_access.h"
#include "lexer.h"
#include "parser.h"
#include "path_utils.h"
#include <filesystem>
#include <fstream>

namespace vexel {

namespace {
std::string normalize_path(const std::string& path) {
    if (path.empty()) return path;
    return std::filesystem::path(path).lexically_normal().string();
}
}

Program ModuleLoader::load(const std::string& entry_path) {
    Program program;
    load_module(normalize_path(entry_path), program);
    return program;
}

ModuleId ModuleLoader::load_module(const std::string& path, Program& program) {
    std::string normalized = normalize_path(path);
    auto it = program.path_to_id.find(normalized);
    if (it != program.path_to_id.end()) {
        return it->second;
    }

    ModuleInfo info;
    info.id = static_cast<ModuleId>(program.modules.size());
    info.path = normalized;
    info.module = parse_module_file(normalized);
    program.modules.push_back(std::move(info));
    program.path_to_id[normalized] = program.modules.back().id;

    // Collect imports from this module to load recursively.
    std::vector<std::vector<std::string>> imports;
    for (const auto& stmt : program.modules.back().module.top_level) {
        collect_imports(stmt, imports);
    }

    for (const auto& import_path : imports) {
        std::string resolved;
        if (!resolve_module_path(import_path, normalized, resolved)) {
            continue; // Resolver will report missing imports later.
        }
        load_module(normalize_path(resolved), program);
    }

    return program.modules.back().id;
}

void ModuleLoader::collect_imports(StmtPtr stmt, std::vector<std::vector<std::string>>& out) const {
    if (!stmt) return;
    if (stmt->kind == Stmt::Kind::Import) {
        out.push_back(stmt->import_path);
    }
    for_each_stmt_child(
        stmt,
        [&](const ExprPtr& child) { collect_imports_expr(child, out); },
        [&](const StmtPtr& child) { collect_imports(child, out); });
}

void ModuleLoader::collect_imports_expr(ExprPtr expr, std::vector<std::vector<std::string>>& out) const {
    if (!expr) return;
    for_each_expr_child(
        expr,
        [&](const ExprPtr& child) { collect_imports_expr(child, out); },
        [&](const StmtPtr& child) { collect_imports(child, out); });
}

bool ModuleLoader::resolve_module_path(const std::vector<std::string>& import_path,
                                       const std::string& current_file,
                                       std::string& out_path) const {
    std::string relative = join_import_path(import_path) + ".vx";
    return try_resolve_relative_path(relative, current_file, project_root, out_path);
}

Module ModuleLoader::parse_module_file(const std::string& path) const {
    std::ifstream file(path);
    if (!file) {
        throw CompileError("Cannot open file: " + path, SourceLocation());
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Lexer lexer(source, path);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse_module(path, path);
}

} // namespace vexel
