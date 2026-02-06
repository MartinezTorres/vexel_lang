#pragma once
#include "program.h"
#include <string>

namespace vexel {

class ModuleLoader {
public:
    explicit ModuleLoader(const std::string& root) : project_root(root) {}
    Program load(const std::string& entry_path);

private:
    std::string project_root;

    ModuleId load_module(const std::string& path, Program& program);
    void collect_imports(StmtPtr stmt, std::vector<std::vector<std::string>>& out) const;
    void collect_imports_expr(ExprPtr expr, std::vector<std::vector<std::string>>& out) const;
    bool resolve_module_path(const std::vector<std::string>& import_path,
                             const std::string& current_file,
                             std::string& out_path) const;
    Module parse_module_file(const std::string& path) const;
};

} // namespace vexel
