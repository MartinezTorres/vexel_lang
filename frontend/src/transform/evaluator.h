#pragma once
#include "ast.h"
#include "typechecker.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace vexel {

struct CTComposite;
struct CTArray;
struct CTUninitialized {};

// Compile-time value - can be scalar, composite (struct), or array
using CTValue = std::variant<int64_t, uint64_t, double, bool, std::string,
                             CTUninitialized,
                             std::shared_ptr<CTComposite>, std::shared_ptr<CTArray>>;

struct CTComposite {
    std::string type_name;
    std::unordered_map<std::string, CTValue> fields;
};

struct CTArray {
    std::vector<CTValue> elements;
};

class CompileTimeEvaluator {
public:
    CompileTimeEvaluator(TypeChecker* tc) : type_checker(tc) {}

    // Try to evaluate expression at compile time
    // Returns true if successful, false if cannot be evaluated at compile time
    bool try_evaluate(ExprPtr expr, CTValue& result);

    // Get the last error message
    std::string get_error() const { return error_msg; }

    // Set a compile-time constant value
    void set_constant(const std::string& name, const CTValue& value) {
        constants[name] = value;
    }

private:
    TypeChecker* type_checker;
    std::unordered_map<std::string, CTValue> constants;
    // Tracks call stack during purity analysis; raw pointers are fine while AST nodes stay alive for one evaluator run.
    std::unordered_set<const Stmt*> purity_stack;
    std::unordered_set<std::string> uninitialized_locals;
    std::vector<std::unordered_set<std::string>> ref_param_stack;
    std::string error_msg;
    int recursion_depth = 0;
    int loop_depth = 0;
    int return_depth = 0;
    static const int MAX_RECURSION_DEPTH = 1000;
    static const int MAX_LOOP_ITERATIONS = 1000000;

    bool eval_literal(ExprPtr expr, CTValue& result);
    bool eval_binary(ExprPtr expr, CTValue& result);
    bool eval_unary(ExprPtr expr, CTValue& result);
    bool eval_call(ExprPtr expr, CTValue& result);
    bool eval_identifier(ExprPtr expr, CTValue& result);
    bool eval_type_constructor(ExprPtr expr, CTValue& result);
    bool eval_member_access(ExprPtr expr, CTValue& result);
    bool eval_conditional(ExprPtr expr, CTValue& result);
    bool eval_cast(ExprPtr expr, CTValue& result);
    bool eval_assignment(ExprPtr expr, CTValue& result);
    bool eval_array_literal(ExprPtr expr, CTValue& result);
    bool eval_tuple_literal(ExprPtr expr, CTValue& result);
    bool eval_range(ExprPtr expr, CTValue& result);
    bool eval_index(ExprPtr expr, CTValue& result);
    bool eval_iteration(ExprPtr expr, CTValue& result);
    bool eval_repeat(ExprPtr expr, CTValue& result);
    bool eval_length(ExprPtr expr, CTValue& result);
    bool eval_block_vm(ExprPtr expr, CTValue& result, bool& handled);
    bool eval_block_fallback(ExprPtr expr, CTValue& result);
    bool declare_uninitialized_local(const StmtPtr& stmt);
    bool coerce_value_to_type(const CTValue& input, TypePtr target_type, CTValue& output);
    bool coerce_value_to_lvalue_type(ExprPtr lvalue, const CTValue& input, CTValue& output);

    // Purity analysis
    bool is_pure_for_compile_time(StmtPtr func, std::string& reason);
    bool is_expr_pure(ExprPtr expr, std::string& reason);
    bool is_stmt_pure(StmtPtr stmt, std::string& reason);

    int64_t to_int(const CTValue& v);
    double to_float(const CTValue& v);

    void push_ref_params(StmtPtr func);
    void pop_ref_params();
    bool is_ref_param(const std::string& name) const;
    std::string base_identifier(ExprPtr expr) const;
};

} // namespace vexel
