#pragma once
#include "ast.h"
#include <unordered_set>

namespace vexel {

class TypeChecker;

class Importer {
public:
    explicit Importer(TypeChecker* checker);
    void handle_import(StmtPtr stmt);

private:
    TypeChecker* checker;

    bool try_resolve_module_path(const std::vector<std::string>& import_path,
                                 const std::string& current_file,
                                 std::string& out_path);
    Module load_module_file(const std::string& path);

    StmtPtr clone_stmt_deep(StmtPtr stmt);
    std::vector<StmtPtr> clone_module_declarations(const std::vector<StmtPtr>& decls);
    void tag_scope_instances(StmtPtr stmt, int instance_id);
    void tag_scope_instances_in_expr(ExprPtr expr, int instance_id,
                                     const std::unordered_set<std::string>& module_symbols);
};

} // namespace vexel
