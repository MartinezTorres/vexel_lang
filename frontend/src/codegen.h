#pragma once
#include "ast.h"
#include "evaluator.h"
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <stack>
#include <optional>

namespace vexel {

class TypeChecker;

struct CCodegenResult {
    std::string header;
    std::string source;
};

struct GeneratedFunctionInfo {
    StmtPtr declaration;
    std::string qualified_name;  // e.g., Vec::push
    std::string c_name;          // mangled C symbol
    std::string storage;         // "" or "static "
    std::string code;            // complete function definition text
};

struct GeneratedVarInfo {
    StmtPtr declaration;
    std::string code;
};

// CodeGenerator translates the type-checked AST into C code.
// Generates both header (.h) and source (.c) files with:
// - Type declarations and forward declarations
// - Function definitions with name mangling
// - Compile-time constant evaluation and dead branch elimination
// - Temporary variable management and reuse optimization
class CodeGenerator {
    std::ostringstream header;
    std::ostringstream body;
    std::vector<GeneratedFunctionInfo> generated_functions;
    std::vector<GeneratedVarInfo> generated_vars;
    int temp_counter;
    std::stack<std::string> available_temps;
    std::unordered_set<std::string> live_temps;
    std::unordered_set<std::string> declared_temps;
    std::unordered_map<std::string, std::string> type_map;
    TypeChecker* type_checker;
    std::stack<std::ostringstream*> output_stack;
    std::unordered_map<std::string, std::string> comparator_cache;
    std::vector<std::string> comparator_definitions;
    bool in_function = false;
    enum class VarMutability { Mutable, NonMutableRuntime, Constexpr };
    std::unordered_map<const Stmt*, VarMutability> var_mutability;
    std::unordered_map<std::string, std::vector<bool>> receiver_mutates;
    std::unordered_map<std::string, std::unordered_set<std::string>> ref_variants;
    std::unordered_map<std::string, bool> function_writes_global;
    std::unordered_map<std::string, bool> function_is_pure;
    std::unordered_set<const Stmt*> used_global_vars;
    std::unordered_set<std::string> used_type_names;
    std::unordered_map<std::string, std::unordered_set<char>> reentrancy_variants;
    char current_reentrancy_key = 'N';

public:
    CodeGenerator();
    CCodegenResult generate(const Module& mod, TypeChecker* tc = nullptr);
    const std::unordered_set<std::string>& reachable() const { return reachable_functions; }
    const std::vector<GeneratedFunctionInfo>& functions() const { return generated_functions; }
    const std::vector<GeneratedVarInfo>& variables() const { return generated_vars; }
    std::string type_to_c(TypePtr type) { return gen_type(type); }
    std::string mangle(const std::string& name) { return mangle_name(name); }
    void set_non_reentrant(const std::unordered_set<std::string>& names) { non_reentrant = names; }

private:
    std::unordered_set<std::string> reachable_functions;
    std::unordered_set<std::string> current_ref_params;  // Track reference parameters in current function
    std::unordered_map<std::string, std::vector<TypePtr>> tuple_types;  // Track tuple types: name -> element types
    std::unordered_map<std::string, ExprPtr> expr_param_substitutions;  // Maps $param names to their expressions
    std::unordered_map<std::string, std::string> value_param_replacements;  // Maps value params when inlining
    std::string underscore_var;  // Current loop underscore variable name (empty when not in iteration)
    std::unordered_set<std::string> non_reentrant;
    bool current_function_non_reentrant = false;

    void analyze_reachability(const Module& mod);
    void analyze_reentrancy(const Module& mod);
    void analyze_mutability(const Module& mod);
    void analyze_ref_variants(const Module& mod);
    void analyze_effects(const Module& mod);
    void analyze_usage(const Module& mod);
    void mark_reachable(const std::string& func_name, int scope_id, const Module& mod);
    void collect_calls(ExprPtr expr, std::unordered_set<std::string>& calls);

    void gen_module(const Module& mod);
    void gen_stmt(StmtPtr stmt);
    void gen_func_decl(StmtPtr stmt, const std::string& ref_key, char reent_key);
    void gen_type_decl(StmtPtr stmt);
    void gen_var_decl(StmtPtr stmt);

    std::string gen_expr(ExprPtr expr);
    std::string gen_binary(ExprPtr expr);
    std::string gen_unary(ExprPtr expr);
    std::string gen_call(ExprPtr expr);
    std::string gen_index(ExprPtr expr);
    std::string gen_member(ExprPtr expr);
    std::string gen_array_literal(ExprPtr expr);
    std::string gen_tuple_literal(ExprPtr expr);
    std::string gen_block(ExprPtr expr);
    std::string gen_block_optimized(ExprPtr expr);
    std::string gen_call_optimized_with_evaluator(ExprPtr expr, CompileTimeEvaluator& evaluator);
    std::string gen_conditional(ExprPtr expr);
    std::string gen_cast(ExprPtr expr);
    std::string gen_assignment(ExprPtr expr);
    std::string gen_range(ExprPtr expr);
    std::string gen_length(ExprPtr expr);
    std::string gen_iteration(ExprPtr expr);
    std::string gen_repeat(ExprPtr expr);

    bool is_compile_time_init(StmtPtr stmt) const;
    std::string mutability_prefix(StmtPtr stmt) const;
    std::string ref_variant_key(const ExprPtr& call, size_t ref_count) const;
    std::vector<std::string> ref_variant_keys_for(StmtPtr stmt) const;
    std::string ref_variant_name(const std::string& func_name, const std::string& ref_key) const;
    std::vector<char> reentrancy_keys_for(const std::string& func_key) const;
    std::string reentrancy_variant_name(const std::string& func_name, const std::string& func_key, char reent_key) const;
    std::string variant_name(const std::string& func_name, const std::string& func_key, char reent_key, const std::string& ref_key) const;
    bool receiver_is_mutable_arg(ExprPtr expr) const;
    std::string reachability_key(const std::string& func_name, int scope_id) const;
    void split_reachability_key(const std::string& key, std::string& func_name, int& scope_id) const;
    bool is_addressable_lvalue(ExprPtr expr) const;
    bool is_mutable_lvalue(ExprPtr expr) const;
    std::string gen_type(TypePtr type);
    std::string mangle_name(const std::string& name);
    std::string fresh_temp();
    void release_temp(const std::string& temp);

    std::optional<std::pair<int64_t, int64_t>> evaluate_range(ExprPtr range_expr);
    std::string storage_prefix() const;
    std::string ensure_comparator(TypePtr type);
    int64_t resolve_array_length(TypePtr type, const SourceLocation& loc);

    void emit(const std::string& code);
    void emit_header(const std::string& code);
};

}
