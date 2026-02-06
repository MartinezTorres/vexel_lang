#pragma once
#include "ast.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

struct AnalysisFacts;
class Resolver;

// Type signature for generic instantiations
struct TypeSignature {
    std::vector<TypePtr> param_types;

    bool operator==(const TypeSignature& other) const {
        if (param_types.size() != other.param_types.size()) return false;
        for (size_t i = 0; i < param_types.size(); i++) {
            if (!types_equal_static(param_types[i], other.param_types[i])) return false;
        }
        return true;
    }

private:
    static bool types_equal_static(TypePtr a, TypePtr b);
};

// Hash function for TypeSignature
struct TypeSignatureHash {
    size_t operator()(const TypeSignature& sig) const {
        size_t hash = 0;
        for (const auto& t : sig.param_types) {
            hash ^= type_hash(t) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }

private:
    static size_t type_hash(TypePtr t);
};

// Generic instantiation record
struct GenericInstantiation {
    std::string mangled_name;
    StmtPtr declaration;
};

struct Symbol {
    enum class Kind { Variable, Function, Type, Constant };
    Kind kind;
    TypePtr type;
    bool is_mutable;
    bool is_external;
    bool is_exported;
    StmtPtr declaration;
    int scope_instance_id = -1;  // For imported symbols: which scope instance they belong to (-1 = not imported)
};

class Scope {
public:
    Scope* parent;
    std::unordered_map<std::string, Symbol> symbols;
    int id;

    Scope(Scope* p = nullptr, int scope_id = 0) : parent(p), id(scope_id) {}

    Symbol* lookup(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return &it->second;
        if (parent) return parent->lookup(name);
        return nullptr;
    }

    void define(const std::string& name, const Symbol& sym) {
        if (symbols.count(name)) {
            throw CompileError("Name already defined: " + name, SourceLocation());
        }
        symbols[name] = sym;
    }

    bool exists_in_current(const std::string& name) {
        return symbols.count(name) > 0;
    }
};

class TypeChecker {
    friend class Resolver;
    Scope* current_scope;
    std::vector<std::unique_ptr<Scope>> scopes;
    int type_var_counter;
    int scope_counter;
    int loop_depth;
    std::unordered_map<std::string, TypePtr> type_var_bindings;

    // Generic instantiation tracking
    std::unordered_map<std::string,
        std::unordered_map<TypeSignature, GenericInstantiation, TypeSignatureHash>> instantiations;
    std::vector<StmtPtr> pending_instantiations;
    // Raw pointer keys are safe here because the owning Module/AST lives for the duration of type checking.
    // If the checker ever caches across runs or reuses freed nodes, switch to stable IDs or shared_ptr keys.
    std::unordered_set<Stmt*> checked_statements;

    // Module loading for per-scope imports
    std::string project_root;
    bool allow_process;
    // Scope* keys rely on scopes surviving for the lifetime of a single check; use stable handles if that changes.
    std::unordered_map<Scope*, std::unordered_set<std::string>> scope_loaded_modules;
    Module* current_module;
    std::unordered_map<std::string, std::vector<TypePtr>> forced_tuple_types;

public:
    TypeChecker(const std::string& proj_root = ".", bool allow_process_exprs = false);
    void check_module(Module& mod);
    void validate_type_usage(const Module& mod, const AnalysisFacts& facts);
    Scope* get_scope() { return current_scope; }
    TypePtr resolve_type(TypePtr type);
    std::optional<bool> constexpr_condition(ExprPtr expr);

    // Generic monomorphization
    std::string get_or_create_instantiation(const std::string& func_name,
                                            const std::vector<TypePtr>& arg_types,
                                            StmtPtr generic_func);
    std::vector<StmtPtr>& get_pending_instantiations() { return pending_instantiations; }
    const std::unordered_map<std::string, std::vector<TypePtr>>& get_forced_tuple_types() const { return forced_tuple_types; }
    void register_tuple_type(const std::string& name, const std::vector<TypePtr>& elem_types);

private:
    Module load_module_file(const std::string& path);
    std::vector<StmtPtr> clone_module_declarations(const std::vector<StmtPtr>& decls);
    StmtPtr clone_stmt_deep(StmtPtr stmt);
    void rename_identifiers(StmtPtr stmt, const std::unordered_map<std::string, std::string>& name_map);
    void rename_identifiers_in_expr(ExprPtr expr, const std::unordered_map<std::string, std::string>& name_map);
    void tag_scope_instances(StmtPtr stmt, int instance_id);
    void tag_scope_instances_in_expr(ExprPtr expr, int instance_id, const std::unordered_set<std::string>& module_symbols);

    void push_scope();
    void pop_scope();

    void check_stmt(StmtPtr stmt);
    void check_func_decl(StmtPtr stmt);
    void check_type_decl(StmtPtr stmt);
    void check_var_decl(StmtPtr stmt);
    void check_import(StmtPtr stmt);

    TypePtr check_expr(ExprPtr expr);
    TypePtr check_binary(ExprPtr expr);
    TypePtr check_unary(ExprPtr expr);
    TypePtr check_call(ExprPtr expr);
    TypePtr check_index(ExprPtr expr);
    TypePtr check_member(ExprPtr expr);
    TypePtr check_array_literal(ExprPtr expr);
    TypePtr check_tuple_literal(ExprPtr expr);
    TypePtr check_block(ExprPtr expr);
    TypePtr check_conditional(ExprPtr expr);
    TypePtr check_cast(ExprPtr expr);
    TypePtr check_assignment(ExprPtr expr);
    TypePtr check_range(ExprPtr expr);
    TypePtr check_length(ExprPtr expr);
    TypePtr check_iteration(ExprPtr expr);
    TypePtr check_repeat(ExprPtr expr);
    TypePtr check_resource_expr(ExprPtr expr);
    TypePtr check_process_expr(ExprPtr expr);
    TypePtr try_operator_overload(ExprPtr expr, const std::string& op, TypePtr left_type);
    bool try_custom_iteration(ExprPtr expr, TypePtr iterable_type);

    void validate_annotations(const Module& mod);
    void validate_stmt_annotations(StmtPtr stmt);
    void validate_expr_annotations(ExprPtr expr);
    void warn_annotation(const Annotation& ann, const std::string& msg);

    bool types_equal(TypePtr a, TypePtr b);
    bool types_compatible(TypePtr a, TypePtr b);
    TypePtr unify_types(TypePtr a, TypePtr b);
    TypePtr bind_typevar(TypePtr var, TypePtr target);
    TypePtr infer_literal_type(ExprPtr expr);
    bool literal_assignable_to(TypePtr target, ExprPtr expr);
    TypePtr make_fresh_typevar();

    void verify_no_shadowing(const std::string& name, const SourceLocation& loc);
    TypePtr parse_type_from_string(const std::string& type_str, const SourceLocation& loc);
    void validate_type(TypePtr type, const SourceLocation& loc);
    void check_recursive_type(const std::string& type_name, StmtPtr type_decl, const SourceLocation& loc);
    bool is_primitive_type(TypePtr type);
    std::optional<bool> evaluate_static_condition(ExprPtr expr);

    enum class TypeFamily { Signed, Unsigned, Float, Other };
    TypeFamily get_type_family(TypePtr type);
    bool types_in_same_family(TypePtr a, TypePtr b);
    bool is_generic_function(StmtPtr func);

    bool try_resolve_relative_path(const std::string& relative, const std::string& current_file, std::string& out_path);
    bool try_resolve_module_path(const std::vector<std::string>& import_path, const std::string& current_file, std::string& out_path);
    bool try_resolve_resource_path(const std::vector<std::string>& import_path, const std::string& current_file, std::string& out_path);
    std::string join_import_path(const std::vector<std::string>& import_path);

    // Generic monomorphization helpers
    StmtPtr clone_function(StmtPtr func);
    StmtPtr clone_stmt(StmtPtr stmt);
    ExprPtr clone_expr(ExprPtr expr);
    void substitute_types(StmtPtr func, const std::vector<TypePtr>& concrete_types);
    void substitute_types_in_expr(ExprPtr expr, const std::unordered_map<std::string, TypePtr>& type_map);
    std::string mangle_generic_name(const std::string& base_name, const std::vector<TypePtr>& types);
    void require_boolean(TypePtr type, const SourceLocation& loc, const std::string& context);
    void require_unsigned_integer(TypePtr type, const SourceLocation& loc, const std::string& context);
};

}
