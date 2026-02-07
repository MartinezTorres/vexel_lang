#include "path_utils.h"
#include <filesystem>

namespace vexel {

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

    if (!project_root.empty()) {
        std::filesystem::path full = std::filesystem::path(project_root) / rel_path;
        if (std::filesystem::exists(full)) {
            out_path = full.string();
            return true;
        }
    }

    if (!current_file.empty()) {
        std::filesystem::path current_dir = std::filesystem::path(current_file).parent_path();
        if (!current_dir.empty()) {
            std::filesystem::path full = current_dir / rel_path;
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

} // namespace vexel
