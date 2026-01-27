#pragma once
#include "ast.h"
#include "analysis.h"
#include "evaluator.h"
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <stack>
#include <optional>

namespace vexel {

class TypeChecker;
struct OptimizationFacts;

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
    AnalysisFacts facts;
    const OptimizationFacts* optimization = nullptr;
    char current_reentrancy_key = 'N';

public:
    CodeGenerator();
    CCodegenResult generate(const Module& mod, TypeChecker* tc = nullptr,
                            const AnalysisFacts* analysis = nullptr,
                            const OptimizationFacts* optimization_facts = nullptr);
    const std::unordered_set<std::string>& reachable() const { return facts.reachable_functions; }
    const std::vector<GeneratedFunctionInfo>& functions() const { return generated_functions; }
    const std::vector<GeneratedVarInfo>& variables() const { return generated_vars; }
    std::string type_to_c(TypePtr type) { return gen_type(type); }
    std::string mangle(const std::string& name) { return mangle_name(name); }
private:
    std::unordered_set<std::string> current_ref_params;  // Track reference parameters in current function
    std::unordered_map<std::string, std::vector<TypePtr>> tuple_types;  // Track tuple types: name -> element types
    std::unordered_map<std::string, ExprPtr> expr_param_substitutions;  // Maps $param names to their expressions
    std::unordered_map<std::string, std::string> value_param_replacements;  // Maps value params when inlining
    std::string underscore_var;  // Current loop underscore variable name (empty when not in iteration)
    bool current_function_non_reentrant = false;

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
    bool try_evaluate(ExprPtr expr, CTValue& out) const;
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
