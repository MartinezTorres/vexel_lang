#pragma once
#include <string>
#include <vector>

namespace vexel {

std::string join_import_path(const std::vector<std::string>& import_path);

bool try_resolve_relative_path(const std::string& relative,
                               const std::string& current_file,
                               const std::string& project_root,
                               std::string& out_path);

bool try_resolve_resource_path(const std::vector<std::string>& import_path,
                               const std::string& current_file,
                               const std::string& project_root,
                               std::string& out_path);

// Return the bundled std/ root directory if available in this build context.
// This is used as a fallback for ::std::* imports when no project-local module
// overrides the requested std module.
bool try_get_builtin_std_root(std::string& out_path);

// Returns true when `path` resolves under the bundled std/ tree used by the
// compiler runtime, and optionally writes the module-relative suffix
// (e.g. "math.vx").
bool is_bundled_std_path(const std::string& path, std::string* std_relative_path = nullptr);

} // namespace vexel
