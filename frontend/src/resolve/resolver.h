#pragma once
#include "ast.h"
#include "bindings.h"
#include "program.h"
#include "symbols.h"
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

class Resolver {
public:
    Resolver(Program& program, Bindings& bindings, const std::string& project_root = ".");

    void resolve();
    void resolve_generated_function(StmtPtr func, int instance_id);

    Scope* instance_scope(int instance_id) const;
    Symbol* lookup_in_instance(int instance_id, const std::string& name) const;

private:
    // Resolver invariants (after resolve()):
    // - Each ModuleInstance has a top-level scope stored in instance_scopes.
    // - Declarations are bound in Bindings; identifiers are bound when resolvable
    //   in the current scope (unresolved identifiers may be fixed by the typechecker).
    // - Assignment expressions that introduce new variables are flagged in Bindings.
    Program& program;
    Bindings& bindings;
    Scope* current_scope;
    std::vector<std::unique_ptr<Scope>> scopes;
    std::unordered_map<Scope*, std::unordered_set<int>> scope_loaded_modules;
    std::unordered_map<int, Scope*> instance_scopes;
    std::unordered_set<unsigned long long> resolved_statements;
    std::unordered_map<long long, int> instance_by_scope_module;
    std::unordered_set<int> resolved_instances;
    std::unordered_map<int, std::vector<int>> pending_imports;
    std::unordered_map<int, std::vector<int>> module_imports;
    std::unordered_set<const Symbol*> defined_globals;
    int scope_counter;
    std::string project_root;
    Module* current_module;
    int current_instance_id;
    int current_module_id;

    void push_scope(int forced_id = -1);
    void pop_scope();
    void verify_no_shadowing(const std::string& name, const SourceLocation& loc);

    void resolve_instance(int instance_id);
    void predeclare_instance_symbols(ModuleInstance& instance);
    void resolve_stmt(StmtPtr stmt);
    void resolve_expr(ExprPtr expr);
    void resolve_type(TypePtr type);

    void resolve_func_decl(StmtPtr stmt, bool define_symbol);
    void resolve_type_decl(StmtPtr stmt);
    void resolve_var_decl(StmtPtr stmt);

    void handle_import(StmtPtr stmt);
    void build_module_imports();
    void collect_imports(StmtPtr stmt, std::vector<std::vector<std::string>>& out) const;
    void collect_imports_expr(ExprPtr expr, std::vector<std::vector<std::string>>& out) const;
    bool module_depends_on(int module_id, int target_module_id) const;

    Symbol* create_symbol(Symbol::Kind kind, const std::string& name, StmtPtr decl, bool is_mutable, bool is_local);
    ModuleInstance& get_or_create_instance(int module_id, int scope_id, const SourceLocation& loc);

    bool try_resolve_module_path(const std::vector<std::string>& import_path,
                                 const std::string& current_file,
                                 std::string& out_path) const;
};

} // namespace vexel
