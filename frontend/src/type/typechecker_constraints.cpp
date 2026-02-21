#include "typechecker.h"

#include "constants.h"
#include "resolver.h"

#include <functional>
#include <unordered_set>

namespace vexel {

namespace {

bool is_untyped_integer_primitive_type(const TypePtr& type) {
    return type &&
           type->kind == Type::Kind::Primitive &&
           (type->primitive == PrimitiveType::Int || type->primitive == PrimitiveType::UInt) &&
           type->integer_bits == 0;
}

bool is_numeric_or_bool_primitive(const TypePtr& type) {
    if (!type || type->kind != Type::Kind::Primitive) return false;
    return type->primitive == PrimitiveType::Bool ||
           type->primitive == PrimitiveType::Int ||
           type->primitive == PrimitiveType::UInt ||
           type->primitive == PrimitiveType::F16 ||
           type->primitive == PrimitiveType::F32 ||
           type->primitive == PrimitiveType::F64;
}

bool is_binary_value_op(const std::string& op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
           op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
}

bool is_tuple_field_member_name(const std::string& name, size_t& index_out) {
    const std::string prefix = std::string(MANGLED_PREFIX);
    if (name.rfind(prefix, 0) != 0) return false;
    const std::string suffix = name.substr(prefix.size());
    if (suffix.empty()) return false;
    for (char ch : suffix) {
        if (ch < '0' || ch > '9') return false;
    }
    try {
        index_out = static_cast<size_t>(std::stoull(suffix));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

void TypeChecker::sync_function_signature_from_bindings(const StmtPtr& func) {
    if (!func || func->kind != Stmt::Kind::FuncDecl) return;

    for (auto& param : func->params) {
        Symbol* psym = lookup_binding(&param);
        if (!psym || !psym->type) continue;
        TypePtr resolved = resolve_type(psym->type);
        if (!resolved) continue;
        if (!param.type ||
            param.type->kind == Type::Kind::TypeVar ||
            is_untyped_integer_primitive_type(param.type)) {
            param.type = resolved;
        }
    }

    if (func->body && func->body->type) {
        if (!func->return_type ||
            func->return_type->kind == Type::Kind::TypeVar ||
            is_untyped_integer_primitive_type(func->return_type)) {
            func->return_type = resolve_type(func->body->type);
        }
    }
}

bool TypeChecker::apply_type_constraint(const ExprPtr& expr, TypePtr target) {
    if (!expr || !target) return false;

    target = resolve_type(target);
    if (!target) return false;

    std::function<bool(TypePtr)> type_is_unresolved = [&](TypePtr t) -> bool {
        if (!t) return true;
        t = resolve_type(t);
        if (!t) return true;
        switch (t->kind) {
            case Type::Kind::TypeVar:
            case Type::Kind::TypeOf:
                return true;
            case Type::Kind::Primitive:
                return is_untyped_integer_primitive_type(t);
            case Type::Kind::Array:
                return type_is_unresolved(t->element_type);
            case Type::Kind::Named:
                return false;
        }
        return true;
    };

    auto refine_slot = [&](TypePtr& slot, TypePtr desired) -> bool {
        desired = resolve_type(desired);
        if (!desired) return false;
        TypePtr current = resolve_type(slot);

        const bool desired_unresolved = type_is_unresolved(desired);
        const bool current_unresolved = type_is_unresolved(current);

        if (!current) {
            slot = desired;
            return true;
        }
        if (desired_unresolved) {
            // Monotonic constraint rule: unresolved targets cannot weaken a known type.
            return true;
        }
        if (current_unresolved) {
            if (current->kind == Type::Kind::Primitive &&
                desired->kind == Type::Kind::Primitive &&
                is_untyped_integer_primitive_type(current) &&
                !is_untyped_integer_primitive_type(desired)) {
                // Keep placeholder identity for aliased unresolved integers.
                current->primitive = desired->primitive;
                current->integer_bits = desired->integer_bits;
                slot = current;
                return true;
            }
            slot = desired;
            return true;
        }
        if (types_equal(current, desired) || types_compatible(current, desired)) {
            return true;
        }
        return false;
    };

    auto refine_expr_type = [&](const ExprPtr& e, TypePtr desired) -> bool {
        if (!e) return true;
        return refine_slot(e->type, desired);
    };

    auto constrain_identifier_symbol = [&](const ExprPtr& id_expr, const TypePtr& desired) -> bool {
        if (!id_expr || id_expr->kind != Expr::Kind::Identifier || !desired) return true;
        Symbol* sym = lookup_binding(id_expr.get());
        if (!sym) {
            sym = lookup_global(id_expr->name);
            if (sym && bindings) {
                bindings->bind(current_instance_id, id_expr.get(), sym);
            }
        }
        if (!sym) return true;

        if (!refine_slot(sym->type, desired)) {
            return false;
        }
        if (sym->declaration && sym->declaration->kind == Stmt::Kind::VarDecl) {
            if (!refine_slot(sym->declaration->var_type, sym->type)) {
                return false;
            }
        }
        return true;
    };

    std::function<bool(const StmtPtr&, const TypePtr&, bool&)> constrain_stmt_returns;
    constrain_stmt_returns = [&](const StmtPtr& stmt, const TypePtr& return_target, bool& saw_return) -> bool {
        if (!stmt) return true;
        switch (stmt->kind) {
            case Stmt::Kind::Return:
                if (!stmt->return_expr) {
                    saw_return = true;
                    return false;
                }
                saw_return = true;
                return apply_type_constraint(stmt->return_expr, return_target);
            case Stmt::Kind::ConditionalStmt:
                return constrain_stmt_returns(stmt->true_stmt, return_target, saw_return);
            case Stmt::Kind::Expr:
            case Stmt::Kind::VarDecl:
            case Stmt::Kind::FuncDecl:
            case Stmt::Kind::TypeDecl:
            case Stmt::Kind::Import:
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue:
                return true;
        }
        return true;
    };

    if (expr->kind == Expr::Kind::Block) {
        if (expr->result_expr) {
            if (!apply_type_constraint(expr->result_expr, target)) {
                return false;
            }
            if (expr->result_expr->type) {
                return refine_expr_type(expr, target);
            }
            expr->type = nullptr;
            return true;
        }

        bool saw_return = false;
        for (const auto& stmt : expr->statements) {
            if (!constrain_stmt_returns(stmt, target, saw_return)) {
                return false;
            }
        }

        // Statement-only blocks do not become typed value expressions.
        if (!saw_return && !type_is_unresolved(target)) {
            return false;
        }
        expr->type = nullptr;
        return true;
    }

    if (expr->kind == Expr::Kind::IntLiteral || expr->kind == Expr::Kind::CharLiteral) {
        if (is_numeric_or_bool_primitive(target) &&
            !is_untyped_integer_primitive_type(target) &&
            !literal_assignable_to(target, expr)) {
            return false;
        }
        return refine_expr_type(expr, target);
    }

    if (expr->kind == Expr::Kind::FloatLiteral) {
        if (target->kind == Type::Kind::Primitive && is_float(target->primitive)) {
            expr->type = target;
            return true;
        }
        return false;
    }

    if (expr->type &&
        is_untyped_integer_primitive_type(expr->type) &&
        is_numeric_or_bool_primitive(target)) {
        if (is_untyped_integer_primitive_type(target) || literal_assignable_to(target, expr)) {
            if (!refine_expr_type(expr, target)) return false;
        }
    } else if (expr->type && expr->type->kind == Type::Kind::TypeVar) {
        if (!refine_expr_type(expr, target)) return false;
    }

    if (!constrain_identifier_symbol(expr, target)) {
        return false;
    }

    switch (expr->kind) {
        case Expr::Kind::Identifier:
            if (!refine_expr_type(expr, target)) return false;
            return true;

        case Expr::Kind::Binary:
            if (is_binary_value_op(expr->op)) {
                if (expr->left && !apply_type_constraint(expr->left, target)) return false;
                if (expr->right && !apply_type_constraint(expr->right, target)) return false;
            }
            if (!refine_expr_type(expr, target)) return false;
            return true;

        case Expr::Kind::Unary:
            if (expr->operand && !apply_type_constraint(expr->operand, target)) return false;
            if (!refine_expr_type(expr, target)) return false;
            return true;

        case Expr::Kind::Conditional:
            if (expr->true_expr && !apply_type_constraint(expr->true_expr, target)) return false;
            if (expr->false_expr && !apply_type_constraint(expr->false_expr, target)) return false;
            if (!refine_expr_type(expr, target)) return false;
            return true;

        case Expr::Kind::Assignment:
            if (expr->right && !apply_type_constraint(expr->right, target)) return false;
            if (!refine_expr_type(expr, target)) return false;
            return true;

        case Expr::Kind::Member: {
            if (!expr->operand) {
                return refine_expr_type(expr, target);
            }
            TypePtr obj_type = resolve_type(expr->operand->type);
            if (obj_type && obj_type->kind == Type::Kind::Named) {
                size_t tuple_index = 0;
                if (obj_type->type_name.rfind(TUPLE_TYPE_PREFIX, 0) == 0 &&
                    is_tuple_field_member_name(expr->name, tuple_index)) {
                    auto it = forced_tuple_types.find(obj_type->type_name);
                    if (it != forced_tuple_types.end() && tuple_index < it->second.size()) {
                        if (!refine_slot(it->second[tuple_index], target)) return false;
                        return refine_expr_type(expr, it->second[tuple_index]);
                    }
                }

                Symbol* type_sym = nullptr;
                if (bindings) {
                    type_sym = bindings->lookup(current_instance_id, obj_type.get());
                }
                if (!type_sym) {
                    type_sym = lookup_global(obj_type->type_name);
                }
                if (type_sym &&
                    type_sym->kind == Symbol::Kind::Type &&
                    type_sym->declaration &&
                    type_sym->declaration->kind == Stmt::Kind::TypeDecl) {
                    for (auto& field : type_sym->declaration->fields) {
                        if (field.name != expr->name) continue;
                        if (!refine_slot(field.type, target)) return false;
                        return refine_expr_type(expr, field.type);
                    }
                }
            }
            return refine_expr_type(expr, target);
        }

        case Expr::Kind::Index: {
            if (expr->operand) {
                TypePtr arr_type = resolve_type(expr->operand->type);
                if (arr_type && arr_type->kind == Type::Kind::Array) {
                    if (!refine_slot(arr_type->element_type, target)) return false;
                    if (!apply_type_constraint(expr->operand, arr_type)) return false;
                    return refine_expr_type(expr, arr_type->element_type);
                }
            }
            return refine_expr_type(expr, target);
        }

        case Expr::Kind::TupleLiteral: {
            if (target->kind == Type::Kind::Named &&
                target->type_name.rfind(TUPLE_TYPE_PREFIX, 0) == 0) {
                auto it = forced_tuple_types.find(target->type_name);
                if (it != forced_tuple_types.end() && it->second.size() == expr->elements.size()) {
                    for (size_t i = 0; i < expr->elements.size(); ++i) {
                        if (!apply_type_constraint(expr->elements[i], it->second[i])) return false;
                    }
                }
            }
            return refine_expr_type(expr, target);
        }

        case Expr::Kind::Cast:
            if (expr->target_type) {
                TypePtr cast_target = resolve_type(expr->target_type);
                TypePtr operand_type = expr->operand ? resolve_type(expr->operand->type) : nullptr;
                if (expr->operand && cast_target && is_untyped_integer_primitive_type(operand_type)) {
                    if (!apply_type_constraint(expr->operand, cast_target)) return false;
                }
                if (!refine_expr_type(expr, cast_target ? cast_target : target)) return false;
                if (cast_target && !types_equal(cast_target, target) &&
                    !types_compatible(cast_target, target) &&
                    !types_compatible(target, cast_target)) {
                    return false;
                }
                return true;
            }
            return refine_expr_type(expr, target);

        case Expr::Kind::ArrayLiteral:
            if (target->kind != Type::Kind::Array) {
                return refine_expr_type(expr, target);
            }
            if (target->array_size &&
                target->array_size->kind == Expr::Kind::IntLiteral &&
                expr->elements.size() != target->array_size->uint_val) {
                return false;
            }
            for (const auto& elem : expr->elements) {
                if (!apply_type_constraint(elem, target->element_type)) {
                    return false;
                }
            }
            expr->type = target;
            return true;

        case Expr::Kind::Call: {
            if (!refine_expr_type(expr, target)) return false;
            if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
                return true;
            }
            Symbol* callee = lookup_binding(expr->operand.get());
            if (!callee) {
                callee = lookup_global(expr->operand->name);
                if (callee && bindings) {
                    bindings->bind(current_instance_id, expr->operand.get(), callee);
                }
            }
            if (!callee || callee->kind != Symbol::Kind::Function || !callee->declaration) {
                return true;
            }

            if (type_is_unresolved(target)) {
                return true;
            }

            StmtPtr func = callee->declaration;
            if (func->is_external && !func->return_type) {
                // External functions without declared return types must stay unresolved.
                return false;
            }
            if (!refine_slot(func->return_type, target)) {
                return false;
            }

            if (!func->body) {
                return true;
            }

            static thread_local std::unordered_set<const Stmt*> active_constraint_funcs;
            if (active_constraint_funcs.count(func.get()) > 0) {
                return true;
            }
            active_constraint_funcs.insert(func.get());
            auto scoped = scoped_instance(callee->instance_id);
            bool ok = apply_type_constraint(func->body, func->return_type);
            active_constraint_funcs.erase(func.get());
            if (!ok) {
                return false;
            }
            sync_function_signature_from_bindings(func);
            return true;
        }

        default:
            return refine_expr_type(expr, target);
    }
}

} // namespace vexel
