#include "evaluator.h"
#include "evaluator_internal.h"
#include "typechecker.h"
#include <functional>

namespace vexel {

bool CompileTimeEvaluator::eval_assignment(ExprPtr expr, CTValue& result) {
    // Evaluate the right-hand side
    CTValue rhs_val;
    if (!try_evaluate(expr->right, rhs_val)) {
        return false;
    }

    auto is_local = [&](const std::string& name) {
        return constants.count(name) > 0 || uninitialized_locals.count(name) > 0;
    };

    const bool creates_local_identifier =
        expr->creates_new_variable &&
        expr->left &&
        expr->left->kind == Expr::Kind::Identifier;

    std::string base = base_identifier(expr->left);
    if (!base.empty()) {
        if (base == "_") {
            error_msg = "Cannot assign to read-only loop variable '_'";
            return false;
        }
        if (is_ref_param(base)) {
            error_msg = "Cannot mutate receiver at compile time: " + base;
            return false;
        }
        Symbol* sym = type_checker && type_checker->get_scope()
            ? type_checker->get_scope()->lookup(base)
            : nullptr;
        if (!creates_local_identifier && sym && !sym->is_mutable && !is_local(base)) {
            error_msg = "Cannot assign to immutable constant: " + base;
            return false;
        }
        if (!creates_local_identifier &&
            sym &&
            sym->kind == Symbol::Kind::Variable &&
            sym->is_mutable &&
            !is_local(base)) {
            error_msg = "Cannot modify mutable globals at compile time: " + base;
            return false;
        }
    }

    CTValue assign_val = rhs_val;
    TypePtr assignment_type = expr->type;
    if (expr->creates_new_variable && expr->declared_var_type) {
        assignment_type = expr->declared_var_type;
    } else if (!assignment_type && expr->left && expr->left->type) {
        assignment_type = expr->left->type;
    }
    if (assignment_type &&
        !coerce_value_to_type(assign_val, assignment_type, assign_val)) {
        return false;
    }
    auto clone_composite = [&](const std::shared_ptr<CTComposite>& src) {
        if (!src) return std::shared_ptr<CTComposite>();
        auto dst = std::make_shared<CTComposite>();
        dst->type_name = src->type_name;
        for (const auto& entry : src->fields) {
            dst->fields[entry.first] = clone_ct_value(entry.second);
        }
        return dst;
    };
    auto clone_array = [&](const std::shared_ptr<CTArray>& src) {
        if (!src) return std::shared_ptr<CTArray>();
        auto dst = std::make_shared<CTArray>();
        dst->elements.reserve(src->elements.size());
        for (const auto& entry : src->elements) {
            dst->elements.push_back(clone_ct_value(entry));
        }
        return dst;
    };
    auto parse_index = [&](ExprPtr index_expr, int64_t& idx) -> bool {
        CTValue index_val;
        if (!try_evaluate(index_expr, index_val)) return false;
        if (!std::holds_alternative<int64_t>(index_val) &&
            !std::holds_alternative<uint64_t>(index_val) &&
            !std::holds_alternative<bool>(index_val)) {
            error_msg = "Index must be an integer/bool constant, got " + ct_value_kind(index_val);
            return false;
        }
        if (std::holds_alternative<int64_t>(index_val)) {
            idx = std::get<int64_t>(index_val);
        } else if (std::holds_alternative<uint64_t>(index_val)) {
            idx = static_cast<int64_t>(std::get<uint64_t>(index_val));
        } else {
            idx = std::get<bool>(index_val) ? 1 : 0;
        }
        if (idx < 0) {
            error_msg = "Index cannot be negative";
            return false;
        }
        return true;
    };

    std::function<bool(ExprPtr, CTValue*&)> resolve_lvalue_slot;
    resolve_lvalue_slot = [&](ExprPtr target, CTValue*& out) -> bool {
        if (!target) {
            error_msg = "Assignment target is not addressable at compile time";
            return false;
        }
        switch (target->kind) {
            case Expr::Kind::Identifier: {
                auto it = constants.find(target->name);
                if (it == constants.end()) {
                    // Assignment writes can materialize an lvalue slot without reading prior value.
                    constants[target->name] = CTUninitialized{};
                    it = constants.find(target->name);
                }
                out = &it->second;
                return true;
            }
            case Expr::Kind::Member: {
                CTValue* base_slot = nullptr;
                if (!resolve_lvalue_slot(target->operand, base_slot)) return false;
                if (!base_slot || !std::holds_alternative<std::shared_ptr<CTComposite>>(*base_slot)) {
                    error_msg = "Member access on non-composite value";
                    return false;
                }
                auto& comp_ref = std::get<std::shared_ptr<CTComposite>>(*base_slot);
                if (!comp_ref) {
                    error_msg = "Member access on null composite value";
                    return false;
                }
                if (!comp_ref.unique()) {
                    comp_ref = clone_composite(comp_ref);
                }
                auto it = comp_ref->fields.find(target->name);
                if (it == comp_ref->fields.end()) {
                    error_msg = "Field not found: " + target->name;
                    return false;
                }
                out = &it->second;
                return true;
            }
            case Expr::Kind::Index: {
                if (target->args.empty()) {
                    error_msg = "Index expression missing index";
                    return false;
                }
                CTValue* base_slot = nullptr;
                if (!resolve_lvalue_slot(target->operand, base_slot)) return false;
                if (!base_slot || !std::holds_alternative<std::shared_ptr<CTArray>>(*base_slot)) {
                    error_msg = "Indexing non-array value at compile time";
                    return false;
                }
                auto& array_ref = std::get<std::shared_ptr<CTArray>>(*base_slot);
                if (!array_ref) {
                    error_msg = "Indexing null array";
                    return false;
                }
                if (!array_ref.unique()) {
                    array_ref = clone_array(array_ref);
                }
                int64_t idx = 0;
                if (!parse_index(target->args[0], idx)) return false;
                if (static_cast<size_t>(idx) >= array_ref->elements.size()) {
                    error_msg = "Index out of bounds in compile-time evaluation";
                    return false;
                }
                out = &array_ref->elements[static_cast<size_t>(idx)];
                return true;
            }
            default:
                error_msg = "Assignment target is not addressable at compile time";
                return false;
        }
    };

    if (creates_local_identifier &&
        constants.count(expr->left->name) == 0 &&
        uninitialized_locals.count(expr->left->name) == 0) {
        constants[expr->left->name] = CTUninitialized{};
    }

    CTValue* slot = nullptr;
    if (!resolve_lvalue_slot(expr->left, slot)) {
        return false;
    }
    if (!slot) {
        error_msg = "Assignment target is not addressable at compile time";
        return false;
    }

    CTValue stored_val;
    if (!coerce_value_to_lvalue_type(expr->left, assign_val, stored_val)) {
        return false;
    }
    *slot = copy_ct_value(stored_val);
    ExprPtr root_ident = expr->left;
    while (root_ident &&
           (root_ident->kind == Expr::Kind::Member || root_ident->kind == Expr::Kind::Index)) {
        root_ident = root_ident->operand;
    }
    if (root_ident && root_ident->kind == Expr::Kind::Identifier) {
        uninitialized_locals.erase(root_ident->name);
    }
    result = copy_ct_value(stored_val);
    return true;
}

} // namespace vexel
