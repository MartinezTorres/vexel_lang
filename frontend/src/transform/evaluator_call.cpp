#include "evaluator.h"
#include "constants.h"
#include "evaluator_internal.h"
#include "typechecker.h"
#include <cstring>
#include <utility>

namespace vexel {

namespace {

void append_u64_hex(std::string& out, uint64_t v) {
    static const char* kHex = "0123456789abcdef";
    out += "0x";
    for (int shift = 60; shift >= 0; shift -= 4) {
        out.push_back(kHex[(v >> shift) & 0xFu]);
    }
}

bool append_memo_key_value(std::string& out, const CTValue& value) {
    if (std::holds_alternative<int64_t>(value)) {
        out += "i:";
        out += std::to_string(std::get<int64_t>(value));
        return true;
    }
    if (std::holds_alternative<uint64_t>(value)) {
        out += "u:";
        out += std::to_string(std::get<uint64_t>(value));
        return true;
    }
    if (std::holds_alternative<CTExactInt>(value)) {
        const CTExactInt& exact = std::get<CTExactInt>(value);
        out += exact.is_unsigned ? "U:" : "I:";
        out += exact.value.to_string();
        return true;
    }
    if (std::holds_alternative<bool>(value)) {
        out += std::get<bool>(value) ? "b:1" : "b:0";
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        uint64_t bits = 0;
        double dv = std::get<double>(value);
        static_assert(sizeof(bits) == sizeof(dv), "double/u64 size mismatch");
        std::memcpy(&bits, &dv, sizeof(bits));
        out += "f:";
        append_u64_hex(out, bits);
        return true;
    }
    if (std::holds_alternative<std::string>(value)) {
        const std::string& s = std::get<std::string>(value);
        out += "s:";
        out += std::to_string(s.size());
        out.push_back(':');
        out += s;
        return true;
    }
    return false;
}

} // namespace

bool CompileTimeEvaluator::eval_call(ExprPtr expr, CTValue& result) {
    // Look up function or type
    if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
        error_msg = "Cannot evaluate non-identifier function calls at compile time";
        return false;
    }

    std::string func_name = expr->operand->name;
    Symbol* sym = nullptr;
    if (expr->operand) {
        sym = expr->operand->resolved_symbol;
    }
    if (!sym && type_checker) {
        sym = type_checker->binding_for(expr->operand.get());
        if (expr->operand) {
            expr->operand->resolved_symbol = sym;
        }
    }
    if (!sym && type_checker && type_checker->get_scope()) {
        sym = type_checker->get_scope()->lookup(func_name);
    }

    if (!sym) {
        error_msg = "Symbol not found: " + func_name;
        return false;
    }

    // Check if this is a type constructor call
    if (sym->kind == Symbol::Kind::Type) {
        return eval_type_constructor(expr, result);
    }

    if (sym->kind != Symbol::Kind::Function || !sym->declaration) {
        error_msg = "Not a function: " + func_name;
        return false;
    }

    StmtPtr func = sym->declaration;
    if (expr->args.size() != func->params.size()) {
        error_msg = "Argument count mismatch in compile-time evaluation";
        return false;
    }

    // Check if function is pure enough for compile-time evaluation
    if (func->is_external) {
        error_msg = "External functions cannot be evaluated at compile time";
        return false;
    }

    // Do not reject calls with whole-function purity checks here.
    // Evaluation is path-sensitive: if the concrete call instance reaches an impure
    // operation (e.g., external call, mutable global write), try_evaluate fails.

    bool memo_candidate = !sym->is_local;
    if (memo_candidate) {
        for (const auto& param : func->params) {
            if (param.is_expression_param) {
                memo_candidate = false;
                break;
            }
        }
    }
    std::vector<CTValue> memo_receivers;
    std::vector<CTValue> memo_args;
    memo_receivers.reserve(expr->receivers.size());
    memo_args.reserve(expr->args.size());

    // Evaluate arguments
    std::unordered_map<std::string, CTValue> saved_constants = constants;
    std::unordered_set<std::string> saved_uninitialized = uninitialized_locals;
    std::unordered_set<std::string> call_bindings;
    std::unordered_map<std::string, ExprPtr> expr_param_bindings;

    auto restore_all_state = [&]() {
        constants = saved_constants;
        uninitialized_locals = saved_uninitialized;
    };

    auto restore_binding = [&](const std::string& name) {
        auto it = saved_constants.find(name);
        if (it != saved_constants.end()) {
            constants[name] = copy_ct_value(it->second);
        } else {
            constants.erase(name);
        }
        if (saved_uninitialized.count(name) > 0) {
            uninitialized_locals.insert(name);
        } else {
            uninitialized_locals.erase(name);
        }
    };

    auto cleanup_call_frame = [&]() {
        for (const auto& name : call_bindings) {
            restore_binding(name);
        }
    };

    if (!func->ref_params.empty()) {
        if (expr->receivers.size() != func->ref_params.size()) {
            error_msg = "Receiver count mismatch in compile-time evaluation";
            return false;
        }
        for (size_t i = 0; i < func->ref_params.size(); i++) {
            call_bindings.insert(func->ref_params[i]);
            CTValue rec_val;
            if (!try_evaluate(expr->receivers[i], rec_val)) {
                restore_all_state();
                return false;
            }
            if (i < func->ref_param_types.size() && func->ref_param_types[i]) {
                CTValue coerced;
                if (!coerce_value_to_type(rec_val, func->ref_param_types[i], coerced)) {
                    restore_all_state();
                    return false;
                }
                constants[func->ref_params[i]] = copy_ct_value(coerced);
                if (memo_candidate) {
                    memo_receivers.push_back(clone_ct_value(coerced));
                }
            } else {
                constants[func->ref_params[i]] = copy_ct_value(rec_val);
                if (memo_candidate) {
                    memo_receivers.push_back(clone_ct_value(rec_val));
                }
            }
            uninitialized_locals.erase(func->ref_params[i]);
        }
    }
    for (size_t i = 0; i < expr->args.size(); i++) {
        const Parameter& param = func->params[i];
        call_bindings.insert(param.name);
        if (param.is_expression_param) {
            expr_param_bindings[param.name] = expr->args[i];
            continue;
        }
        CTValue arg_val;
        if (!try_evaluate(expr->args[i], arg_val)) {
            restore_all_state();
            return false;
        }
        if (param.type) {
            CTValue coerced;
            if (!coerce_value_to_type(arg_val, param.type, coerced)) {
                restore_all_state();
                return false;
            }
            constants[param.name] = copy_ct_value(coerced);
            if (memo_candidate) {
                memo_args.push_back(clone_ct_value(coerced));
            }
        } else {
            constants[param.name] = copy_ct_value(arg_val);
            if (memo_candidate) {
                memo_args.push_back(clone_ct_value(arg_val));
            }
        }
        uninitialized_locals.erase(param.name);
    }

    std::string memo_key;
    bool memo_key_active = false;
    bool memo_store_allowed = false;
    if (memo_candidate) {
        // Conservative capture-safety rule: only memoize when the current evaluator
        // frame contains no ambient local bindings beyond this call's own bindings.
        // This avoids unsound reuse for nested functions that may capture locals.
        for (const auto& entry : constants) {
            if (!call_bindings.count(entry.first)) {
                memo_candidate = false;
                break;
            }
        }
    }
    if (memo_candidate) {
        for (const auto& name : uninitialized_locals) {
            if (!call_bindings.count(name)) {
                memo_candidate = false;
                break;
            }
        }
    }
    if (memo_candidate) {
        memo_key.reserve(96 + (memo_receivers.size() + memo_args.size()) * 24);
        memo_key += "fn:";
        memo_key += std::to_string(reinterpret_cast<uintptr_t>(sym));
        memo_key += "|r:";
        for (const auto& v : memo_receivers) {
            if (!append_memo_key_value(memo_key, v)) {
                memo_candidate = false;
                break;
            }
            memo_key.push_back('|');
        }
        if (memo_candidate) {
            memo_key += "|a:";
            for (const auto& v : memo_args) {
                if (!append_memo_key_value(memo_key, v)) {
                    memo_candidate = false;
                    break;
                }
                memo_key.push_back('|');
            }
        }
        if (memo_candidate) {
            auto cached = call_result_cache.find(memo_key);
            if (cached != call_result_cache.end()) {
                result = clone_ct_value(cached->second);
                cleanup_call_frame();
                return true;
            }
            if (!active_call_memo_keys.count(memo_key)) {
                active_call_memo_keys.insert(memo_key);
                memo_key_active = true;
                memo_store_allowed = true;
            }
        }
    }
    struct MemoGuard {
        CompileTimeEvaluator* self = nullptr;
        std::string* key = nullptr;
        bool active = false;
        ~MemoGuard() {
            if (active && self && key) {
                self->active_call_memo_keys.erase(*key);
            }
        }
    } memo_guard{this, &memo_key, memo_key_active};

    // Evaluate function body
    if (!func->body) {
        error_msg = "Function has no body";
        restore_all_state();
        return false;
    }

    push_ref_params(func);
    struct RefParamGuard {
        CompileTimeEvaluator* self;
        ~RefParamGuard() {
            if (self) self->pop_ref_params();
        }
    } ref_guard{this};

    bool pushed_expr_params = false;
    if (!expr_param_bindings.empty()) {
        expr_param_stack.push_back(std::move(expr_param_bindings));
        pushed_expr_params = true;
    }
    struct ExprParamGuard {
        CompileTimeEvaluator* self;
        bool active = false;
        ~ExprParamGuard() {
            if (active && self && !self->expr_param_stack.empty()) {
                self->expr_param_stack.pop_back();
            }
        }
    } expr_param_guard{this, pushed_expr_params};

    struct ReturnGuard {
        int& depth;
        explicit ReturnGuard(int& d) : depth(d) { depth++; }
        ~ReturnGuard() { depth--; }
    } return_guard(return_depth);

    bool success = false;
    try {
        success = try_evaluate(func->body, result);
    } catch (const EvalReturn& ret) {
        result = copy_ct_value(ret.value);
        success = true;
    }
    if (success) {
        if (!func->return_types.empty()) {
            if (!std::holds_alternative<std::shared_ptr<CTComposite>>(result)) {
                error_msg = "Tuple return value expected for compile-time call";
                success = false;
            } else {
                auto in_comp = std::get<std::shared_ptr<CTComposite>>(result);
                if (!in_comp) {
                    error_msg = "Tuple return value is null in compile-time call";
                    success = false;
                } else {
                    auto out_comp = std::make_shared<CTComposite>();
                    if (expr->type && expr->type->kind == Type::Kind::Named) {
                        out_comp->type_name = expr->type->type_name;
                    } else {
                        out_comp->type_name = in_comp->type_name;
                    }
                    for (size_t i = 0; i < func->return_types.size(); ++i) {
                        const std::string field_name = std::string(MANGLED_PREFIX) + std::to_string(i);
                        auto it = in_comp->fields.find(field_name);
                        if (it == in_comp->fields.end()) {
                            error_msg = "Missing tuple return field in compile-time call: " + field_name;
                            success = false;
                            break;
                        }
                        CTValue coerced_field = copy_ct_value(it->second);
                        if (func->return_types[i] &&
                            !coerce_value_to_type(it->second, func->return_types[i], coerced_field)) {
                            success = false;
                            break;
                        }
                        out_comp->fields[field_name] = copy_ct_value(coerced_field);
                    }
                    if (success) {
                        result = out_comp;
                    }
                }
            }
        } else if (func->return_type) {
            CTValue coerced_result;
            if (!coerce_value_to_type(result, func->return_type, coerced_result)) {
                success = false;
            } else {
                result = copy_ct_value(coerced_result);
            }
        }
    }
    if (!success) {
        restore_all_state();
        return false;
    }

    if (memo_store_allowed) {
        call_result_cache[memo_key] = clone_ct_value(result);
    }

    cleanup_call_frame();
    return true;
}

} // namespace vexel
