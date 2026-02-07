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

} // namespace vexel
