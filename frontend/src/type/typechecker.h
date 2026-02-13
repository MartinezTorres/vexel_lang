#pragma once
#include "ast.h"
#include "bindings.h"
#include "evaluator.h"
#include "program.h"
#include "symbols.h"
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

class Resolver;

class TypeChecker {
    Resolver* resolver;
    Bindings* bindings;
    Program* program;
    Scope* global_scope;
    int type_var_counter;
    int loop_depth;
    std::unordered_map<std::string, TypePtr> type_var_bindings;

    // Generic instantiation tracking
    std::unordered_map<std::string,
        std::unordered_map<TypeSignature, GenericInstantiation, TypeSignatureHash>> instantiations;
    std::vector<StmtPtr> pending_instantiations;
    // Raw pointer keys are safe here because the owning Module/AST lives for the duration of type checking.
    // If the checker ever caches across runs or reuses freed nodes, switch to stable IDs or shared_ptr keys.
    std::unordered_set<unsigned long long> checked_statements;

    std::string project_root;
    bool allow_process;
    std::unordered_map<std::string, std::vector<TypePtr>> forced_tuple_types;
    int current_instance_id = -1;
    std::unordered_map<const Symbol*, CTValue> known_constexpr_values;
    std::unordered_map<unsigned long long, bool> constexpr_condition_cache;

public:
    class InstanceScope {
    public:
        InstanceScope(TypeChecker& tc, int instance_id)
            : checker_(&tc), saved_instance_(tc.current_instance()) {
            checker_->set_current_instance(instance_id);
        }

        ~InstanceScope() {
            if (checker_) {
                checker_->set_current_instance(saved_instance_);
            }
        }

        InstanceScope(const InstanceScope&) = delete;
        InstanceScope& operator=(const InstanceScope&) = delete;

        InstanceScope(InstanceScope&& other) noexcept
            : checker_(other.checker_), saved_instance_(other.saved_instance_) {
            other.checker_ = nullptr;
        }

    private:
        TypeChecker* checker_;
        int saved_instance_;
    };

    TypeChecker(const std::string& proj_root = ".", bool allow_process_exprs = false,
                Resolver* resolver = nullptr, Bindings* bindings = nullptr, Program* program = nullptr);
    void check_program(Program& program);
    void check_module(Module& mod);
    void validate_type_usage(const Module& mod, const AnalysisFacts& facts);
    Scope* get_scope() { return global_scope; }
    void set_resolver(Resolver* resolver);
    void set_bindings(Bindings* bindings_in);
    void set_program(Program* program_in);
    Symbol* binding_for(const void* node) const { return lookup_binding(node); }
    Symbol* binding_for(int instance_id, const void* node) const;
    int current_instance() const { return current_instance_id; }
    void set_current_instance(int instance_id);
    InstanceScope scoped_instance(int instance_id) { return InstanceScope(*this, instance_id); }
    Program* get_program() const { return program; }
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
    // TypeChecker invariants (enforced after check_module):
    // - All non-generic declarations have bindings and non-null type slots (TypeVar allowed).
    // - Non-external functions have bodies; external params must be typed.
    // - Value-producing expressions have expr->type set; only Iteration/Repeat and
    //   block expressions without a result are untyped.
    // - TypeVars are permitted to remain unresolved, but only when their values are
    //   never used in a concrete context (enforced later by type-use validation).
    Symbol* lookup_global(const std::string& name) const;
    Symbol* lookup_binding(const void* node) const;
    unsigned long long stmt_key(const Stmt* stmt) const;
    unsigned long long expr_key(int instance_id, const Expr* expr) const;
    unsigned long long expr_key(const Expr* expr) const;
    void validate_invariants(const Module& mod);

    void check_stmt(StmtPtr stmt);
    void check_func_decl(StmtPtr stmt);
    void check_type_decl(StmtPtr stmt);
    void check_var_decl(StmtPtr stmt);

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

    void warn_annotation(const Annotation& ann, const std::string& msg);

    bool types_equal(TypePtr a, TypePtr b);
    bool types_compatible(TypePtr a, TypePtr b);
    TypePtr unify_types(TypePtr a, TypePtr b);
    TypePtr bind_typevar(TypePtr var, TypePtr target);
    TypePtr infer_literal_type(ExprPtr expr);
    bool literal_assignable_to(TypePtr target, ExprPtr expr);
    TypePtr make_fresh_typevar();

    TypePtr parse_type_from_string(const std::string& type_str, const SourceLocation& loc);
    void validate_type(TypePtr type, const SourceLocation& loc);
    void check_recursive_type(const std::string& type_name, StmtPtr type_decl, const SourceLocation& loc);
    bool is_primitive_type(TypePtr type);

    enum class TypeFamily { Signed, Unsigned, Float, Other };
    TypeFamily get_type_family(TypePtr type);
    bool types_in_same_family(TypePtr a, TypePtr b);
    bool is_generic_function(StmtPtr func);

    // Path helpers moved to path_utils.

    // Generic monomorphization helpers
    StmtPtr clone_function(StmtPtr func);
    StmtPtr clone_stmt(StmtPtr stmt);
    ExprPtr clone_expr(ExprPtr expr);
    void substitute_types(StmtPtr func, const std::vector<TypePtr>& concrete_types);
    void substitute_types_in_stmt(StmtPtr stmt, const std::unordered_map<std::string, TypePtr>& type_map);
    void substitute_types_in_expr(ExprPtr expr, const std::unordered_map<std::string, TypePtr>& type_map);
    TypePtr substitute_type_with_map(TypePtr type, const std::unordered_map<std::string, TypePtr>& type_map);
    std::string mangle_generic_name(const std::string& base_name, const std::vector<TypePtr>& types);
    void require_boolean(TypePtr type, const SourceLocation& loc, const std::string& context);
    void require_boolean_expr(ExprPtr expr, TypePtr type, const SourceLocation& loc, const std::string& context);
    void require_unsigned_integer(TypePtr type, const SourceLocation& loc, const std::string& context);
    bool try_evaluate_constexpr(ExprPtr expr, CTValue& out);
    CTEQueryResult query_constexpr(ExprPtr expr);
    void remember_constexpr_value(Symbol* sym, const CTValue& value);
    void forget_constexpr_value(Symbol* sym);
    void forget_all_constexpr_values();
};

}
