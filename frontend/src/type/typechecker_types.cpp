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
void TypeChecker::validate_type(TypePtr type, const SourceLocation& loc) {
    if (!type) return;

    switch (type->kind) {
        case Type::Kind::Array: {
            // Recursively validate element type
            validate_type(type->element_type, loc);
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
                if (std::holds_alternative<int64_t>(size_val)) {
                    if (std::get<int64_t>(size_val) < 0) {
                        throw CompileError("Array size must be non-negative", loc);
                    }
                } else if (std::holds_alternative<uint64_t>(size_val)) {
                    // Already non-negative.
                } else if (std::holds_alternative<bool>(size_val)) {
                    // Allow #b values as 0/1 in size position.
                } else {
                    throw CompileError("Array size must be an integer compile-time constant", loc);
                }
            }
            break;
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
            break;
        }
        default:
            break;
    }
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
