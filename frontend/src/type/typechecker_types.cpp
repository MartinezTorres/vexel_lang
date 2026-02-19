#include "typechecker.h"
#include "evaluator.h"
#include "constants.h"
#include <limits>

namespace vexel {

TypePtr TypeChecker::parse_type_from_string(const std::string& type_str, const SourceLocation& loc) {
    // Parse primitive types
    if (type_str == "i8") return Type::make_primitive(PrimitiveType::I8, loc);
    if (type_str == "i16") return Type::make_primitive(PrimitiveType::I16, loc);
    if (type_str == "i32") return Type::make_primitive(PrimitiveType::I32, loc);
    if (type_str == "i64") return Type::make_primitive(PrimitiveType::I64, loc);
    if (type_str == "u8") return Type::make_primitive(PrimitiveType::U8, loc);
    if (type_str == "u16") return Type::make_primitive(PrimitiveType::U16, loc);
    if (type_str == "u32") return Type::make_primitive(PrimitiveType::U32, loc);
    if (type_str == "u64") return Type::make_primitive(PrimitiveType::U64, loc);
    if (type_str == "f32") return Type::make_primitive(PrimitiveType::F32, loc);
    if (type_str == "f64") return Type::make_primitive(PrimitiveType::F64, loc);
    if (type_str == "b") return Type::make_primitive(PrimitiveType::Bool, loc);
    if (type_str == "s") return Type::make_primitive(PrimitiveType::String, loc);

    // Named/complex types are resolved later through bindings and resolver scopes.
    TypePtr named = Type::make_named(type_str, loc);
    if (bindings) {
        Symbol* sym = lookup_global(type_str);
        if (sym) {
            bindings->bind(current_instance_id, named.get(), sym);
        }
    }
    return named;
}
TypeChecker::TypeFamily TypeChecker::get_type_family(TypePtr type) {
    if (!type || type->kind != Type::Kind::Primitive) return TypeFamily::Other;

    if (is_signed_int(type->primitive)) return TypeFamily::Signed;
    if (is_unsigned_int(type->primitive)) return TypeFamily::Unsigned;
    if (is_float(type->primitive)) return TypeFamily::Float;
    return TypeFamily::Other;
}
bool TypeChecker::types_in_same_family(TypePtr a, TypePtr b) {
    return get_type_family(a) == get_type_family(b) &&
           get_type_family(a) != TypeFamily::Other;
}
bool TypeChecker::is_generic_function(StmtPtr func) {
    if (!func || func->kind != Stmt::Kind::FuncDecl) return false;

    bool has_untyped_param = false;
    for (const auto& param : func->params) {
        if ((!param.type || param.type->kind == Type::Kind::TypeVar) && !param.is_expression_param) {
            has_untyped_param = true;
            break;
        }
    }

    bool has_typevar_return = false;
    if (!func->return_types.empty()) {
        for (const auto& rt : func->return_types) {
            if (!rt || rt->kind == Type::Kind::TypeVar) {
                has_typevar_return = true;
                break;
            }
        }
    } else if (func->return_type && func->return_type->kind == Type::Kind::TypeVar) {
        has_typevar_return = true;
    }

    return has_untyped_param || has_typevar_return;
}
TypePtr TypeChecker::validate_type(TypePtr type, const SourceLocation& loc) {
    if (!type) return nullptr;

    switch (type->kind) {
        case Type::Kind::Array: {
            // Recursively validate element type and materialize any type expression.
            type->element_type = validate_type(type->element_type, loc);
            if (type->array_size) {
                CTEQueryResult size_query = query_constexpr(type->array_size);
                if (size_query.status == CTEQueryStatus::Error) {
                    throw CompileError(size_query.message.empty()
                                           ? "Array size evaluation failed"
                                           : size_query.message,
                                       loc);
                }
                if (size_query.status != CTEQueryStatus::Known) {
                    throw CompileError("Array size must be a compile-time constant", loc);
                }

                const CTValue& size_val = size_query.value;
                uint64_t normalized_size = 0;
                if (std::holds_alternative<int64_t>(size_val)) {
                    const int64_t signed_size = std::get<int64_t>(size_val);
                    if (signed_size < 0) {
                        throw CompileError("Array size must be non-negative", loc);
                    }
                    normalized_size = static_cast<uint64_t>(signed_size);
                } else if (std::holds_alternative<uint64_t>(size_val)) {
                    normalized_size = std::get<uint64_t>(size_val);
                } else if (std::holds_alternative<bool>(size_val)) {
                    normalized_size = std::get<bool>(size_val) ? 1ULL : 0ULL;
                } else {
                    throw CompileError("Array size must be an integer compile-time constant", loc);
                }

                // Canonicalize array-size expressions to integer literals so all later
                // type comparisons/hashing use semantic size identity.
                SourceLocation size_loc = type->array_size->location;
                type->array_size = Expr::make_uint(normalized_size,
                                                   size_loc,
                                                   std::to_string(normalized_size));
            }
            return type;
        }
        case Type::Kind::Named: {
            // Check for recursive types
            Symbol* type_sym = nullptr;
            if (bindings) {
                type_sym = bindings->lookup(current_instance_id, type.get());
            }
            if (!type_sym) {
                type_sym = lookup_global(type->type_name);
            }
            if (type_sym && type_sym->kind == Symbol::Kind::Type && type_sym->declaration) {
                check_recursive_type(type->type_name, type_sym->declaration, loc);
            }
            return type;
        }
        case Type::Kind::TypeOf: {
            if (!type->typeof_expr) {
                throw CompileError("Type expression #[...] requires an expression", loc);
            }
            TypePtr resolved = check_expr(type->typeof_expr);
            if (!resolved) {
                throw CompileError("Type expression #[...] does not resolve to a type", type->typeof_expr->location);
            }
            resolved = resolve_type(resolved);
            if (resolved && resolved->kind == Type::Kind::TypeVar) {
                throw CompileError("Type expression #[...] resolved to an unknown type", type->typeof_expr->location);
            }
            if (resolved && resolved->kind == Type::Kind::TypeOf) {
                throw CompileError("Type expression #[...] must resolve to a concrete type", type->typeof_expr->location);
            }
            return validate_type(resolved, loc);
        }
        case Type::Kind::Primitive:
        case Type::Kind::TypeVar:
            return type;
    }

    return type;
}
void TypeChecker::check_recursive_type(const std::string& type_name, StmtPtr type_decl, const SourceLocation& loc) {
    // Check if any field has the same type as the parent (direct recursion)
    for (const auto& field : type_decl->fields) {
        if (field.type && field.type->kind == Type::Kind::Named &&
            field.type->type_name == type_name) {
            throw CompileError("Recursive types are not allowed (type " + type_name +
                             " contains field of its own type)", loc);
        }
    }
}
bool TypeChecker::is_primitive_type(TypePtr type) {
    if (!type) return false;
    return type->kind == Type::Kind::Primitive;
}

bool TypeChecker::is_abi_data_type(TypePtr type,
                                   std::unordered_set<std::string>& visiting_named_types,
                                   std::string* reason) {
    if (!type) {
        if (reason) *reason = "missing type";
        return false;
    }

    type = resolve_type(type);
    if (!type) {
        if (reason) *reason = "missing type";
        return false;
    }

    switch (type->kind) {
        case Type::Kind::Primitive:
            return true;
        case Type::Kind::Array:
            if (!type->array_size) {
                if (reason) *reason = "array size must be compile-time known";
                return false;
            }
            return is_abi_data_type(type->element_type, visiting_named_types, reason);
        case Type::Kind::Named: {
            if (type->type_name.empty()) {
                if (reason) *reason = "named type has no identifier";
                return false;
            }
            if (type->type_name.rfind(TUPLE_TYPE_PREFIX, 0) == 0) {
                if (reason) *reason = "tuple types are not allowed at ABI boundaries";
                return false;
            }
            if (!visiting_named_types.insert(type->type_name).second) {
                if (reason) *reason = "recursive named types are not allowed at ABI boundaries";
                return false;
            }

            Symbol* type_sym = nullptr;
            if (bindings) {
                type_sym = bindings->lookup(current_instance_id, type.get());
            }
            if (!type_sym && type->resolved_symbol) {
                type_sym = type->resolved_symbol;
            }
            if (!type_sym) {
                type_sym = lookup_global(type->type_name);
            }
            if (!type_sym || type_sym->kind != Symbol::Kind::Type || !type_sym->declaration ||
                type_sym->declaration->kind != Stmt::Kind::TypeDecl) {
                visiting_named_types.erase(type->type_name);
                if (reason) *reason = "named ABI type must resolve to a declared type";
                return false;
            }

            for (const auto& field : type_sym->declaration->fields) {
                if (!is_abi_data_type(field.type, visiting_named_types, reason)) {
                    visiting_named_types.erase(type->type_name);
                    return false;
                }
            }

            visiting_named_types.erase(type->type_name);
            return true;
        }
        case Type::Kind::TypeVar:
            if (reason) *reason = "type variables are not allowed at ABI boundaries";
            return false;
        case Type::Kind::TypeOf:
            if (reason) *reason = "type expressions are not allowed at ABI boundaries";
            return false;
    }

    if (reason) *reason = "unsupported ABI type";
    return false;
}

bool TypeChecker::is_external_abi_boundary_type(TypePtr type, std::string* reason) {
    if (!type) {
        if (reason) *reason = "missing type";
        return false;
    }

    type = resolve_type(type);
    if (!type) {
        if (reason) *reason = "missing type";
        return false;
    }

    if (type->kind == Type::Kind::Primitive) {
        return true;
    }
    if (type->kind == Type::Kind::Array) {
        if (reason) *reason = "top-level arrays are not allowed at function ABI boundaries";
        return false;
    }

    std::unordered_set<std::string> visiting_named_types;
    return is_abi_data_type(type, visiting_named_types, reason);
}

void TypeChecker::require_boolean(TypePtr type, const SourceLocation& loc, const std::string& context) {
    if (!type || type->kind != Type::Kind::Primitive || type->primitive != PrimitiveType::Bool) {
        throw CompileError(context + " requires a boolean expression", loc);
    }
}

void TypeChecker::require_boolean_expr(ExprPtr expr, TypePtr type, const SourceLocation& loc, const std::string& context) {
    if (type && type->kind == Type::Kind::Primitive && type->primitive == PrimitiveType::Bool) {
        return;
    }

    TypePtr bool_type = Type::make_primitive(PrimitiveType::Bool, loc);
    if (expr && literal_assignable_to(bool_type, expr)) {
        expr->type = bool_type;
        return;
    }

    throw CompileError(context + " requires a boolean expression", loc);
}

void TypeChecker::require_unsigned_integer(TypePtr type, const SourceLocation& loc, const std::string& context) {
    if (!type || type->kind != Type::Kind::Primitive || !is_unsigned_int(type->primitive)) {
        throw CompileError(context + " requires unsigned integer operands", loc);
    }
}

} // namespace vexel
