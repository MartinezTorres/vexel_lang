#include "path_utils.h"
#include <filesystem>
#include <cstdlib>

namespace vexel {

namespace {
bool has_std_prefix(const std::string& relative) {
    return relative.rfind("std/", 0) == 0;
}

std::filesystem::path builtin_std_root_from_source_tree() {
    // __FILE__ points to frontend/src/support/path_utils.cpp in the source tree.
    std::filesystem::path p = std::filesystem::path(__FILE__).lexically_normal();
    return (p.parent_path() / ".." / ".." / ".." / "std").lexically_normal();
}

bool try_builtin_std_root_from_env(std::filesystem::path& out) {
    if (const char* root = std::getenv("VEXEL_ROOT_DIR")) {
        std::filesystem::path p = std::filesystem::path(root) / "std";
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            out = p.lexically_normal();
            return true;
        }
    }
    return false;
}

bool try_builtin_std_root_from_macro(std::filesystem::path& out) {
#ifdef VEXEL_BUILTIN_STD_REPO_ROOT
    std::filesystem::path p = std::filesystem::path(VEXEL_BUILTIN_STD_REPO_ROOT) / "std";
    if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
        out = p.lexically_normal();
        return true;
    }
#endif
    return false;
}
}

std::string join_import_path(const std::vector<std::string>& import_path) {
    std::string path;
    for (size_t i = 0; i < import_path.size(); ++i) {
        if (i > 0) path += "/";
        path += import_path[i];
    }
    return path;
}

bool try_resolve_relative_path(const std::string& relative,
                               const std::string& current_file,
                               const std::string& project_root,
                               std::string& out_path) {
    std::filesystem::path rel_path(relative);
    const bool std_path = has_std_prefix(relative);

    if (!project_root.empty()) {
        std::filesystem::path full = std::filesystem::path(project_root) / rel_path;
        if (std::filesystem::exists(full)) {
            out_path = full.string();
            return true;
        }
    }

    // ::std::* is intentionally project-root-or-bundled. Do not search relative
    // to the importing file for std modules/resources.
    if (!std_path && !current_file.empty()) {
        std::filesystem::path current_dir = std::filesystem::path(current_file).parent_path();
        if (!current_dir.empty()) {
            std::filesystem::path full = current_dir / rel_path;
            if (std::filesystem::exists(full)) {
                out_path = full.string();
                return true;
            }
        }
    }

    if (std_path) {
        std::string builtin_std_root;
        if (try_get_builtin_std_root(builtin_std_root)) {
            std::filesystem::path subpath = rel_path.lexically_relative("std");
            std::filesystem::path full = std::filesystem::path(builtin_std_root) / subpath;
            if (std::filesystem::exists(full)) {
                out_path = full.string();
                return true;
            }
        }
    }

    return false;
}

bool try_resolve_resource_path(const std::vector<std::string>& import_path,
                               const std::string& current_file,
                               const std::string& project_root,
                               std::string& out_path) {
    std::string relative = join_import_path(import_path);
    return try_resolve_relative_path(relative, current_file, project_root, out_path);
}

bool try_get_builtin_std_root(std::string& out_path) {
    std::filesystem::path std_root;
    if (try_builtin_std_root_from_env(std_root) ||
        try_builtin_std_root_from_macro(std_root)) {
        out_path = std_root.string();
        return true;
    }
    std_root = builtin_std_root_from_source_tree();
    if (std::filesystem::exists(std_root) && std::filesystem::is_directory(std_root)) {
        out_path = std_root.string();
        return true;
    }
    return false;
}

} // namespace vexel
