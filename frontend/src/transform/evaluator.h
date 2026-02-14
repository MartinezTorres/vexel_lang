#pragma once
#include "ast.h"
#include "cte_value.h"
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

class TypeChecker;

class CompileTimeEvaluator {
public:
    using ExprValueObserver = std::function<void(const Expr*, const CTValue&)>;
    using SymbolReadObserver = std::function<void(const Symbol*)>;

    CompileTimeEvaluator(TypeChecker* tc) : type_checker(tc) {}

    // Try to evaluate expression at compile time
    // Returns true if successful, false if cannot be evaluated at compile time
    bool try_evaluate(ExprPtr expr, CTValue& result);

    // Query compile-time knowledge without collapsing Unknown and Error into one state.
    CTEQueryResult query(ExprPtr expr);

    // Get the last error message
    std::string get_error() const { return error_msg; }

    // Set a compile-time constant value
    void set_constant(const std::string& name, const CTValue& value) {
        constants[name] = value;
    }

    // Seed a compile-time constant value for a specific bound symbol.
    void set_symbol_constant(const Symbol* sym, const CTValue& value) {
        if (!sym) return;
        symbol_constants[sym] = value;
    }

    void set_value_observer(ExprValueObserver observer) {
        value_observer = std::move(observer);
    }

    void set_symbol_read_observer(SymbolReadObserver observer) {
        symbol_read_observer = std::move(observer);
    }

    // Reset all transient evaluation state so one evaluator instance can be safely reused.
    void reset_state();

private:
    TypeChecker* type_checker;
    std::unordered_map<std::string, CTValue> constants;
    std::unordered_map<const Symbol*, CTValue> symbol_constants;
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
    bool eval_block(ExprPtr expr, CTValue& result);
    bool declare_uninitialized_local(const StmtPtr& stmt);
    bool coerce_value_to_type(const CTValue& input, TypePtr target_type, CTValue& output);
    bool coerce_value_to_lvalue_type(ExprPtr lvalue, const CTValue& input, CTValue& output);
    bool evaluate_constant_symbol(Symbol* sym, CTValue& result);

    int64_t to_int(const CTValue& v);
    double to_float(const CTValue& v);

    void push_ref_params(StmtPtr func);
    void pop_ref_params();
    bool is_ref_param(const std::string& name) const;
    std::string base_identifier(ExprPtr expr) const;

    std::unordered_set<const Symbol*> constant_eval_stack;
    std::unordered_map<const Symbol*, CTValue> constant_value_cache;
    std::vector<std::unordered_map<std::string, ExprPtr>> expr_param_stack;
    std::unordered_set<std::string> expanding_expr_params;
    int expr_param_expansion_depth = 0;
    bool hard_error = false;
    ExprValueObserver value_observer;
    SymbolReadObserver symbol_read_observer;
};

} // namespace vexel
