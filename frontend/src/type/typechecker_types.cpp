#include "typechecker.h"
#include "evaluator.h"
#include "constants.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>

namespace vexel {

TypePtr TypeChecker::parse_type_from_string(const std::string& type_str, const SourceLocation& loc) {
    auto parse_shape_dim_literal = [&](const std::string& text) -> ExprPtr {
        unsigned long long value = 0;
        try {
            value = std::stoull(text);
        } catch (const std::exception&) {
            throw CompileError("Invalid shape dimension in type '#" + type_str + "'", loc);
        }
        return Expr::make_int_exact(APInt(static_cast<uint64_t>(value)), true, loc, text);
    };
    auto parse_vector_or_matrix = [&](char prefix) -> TypePtr {
        if (type_str.size() < 5 || type_str[0] != prefix || type_str[1] != '(' || type_str.back() != ')') {
            return nullptr;
        }
        std::string inner = type_str.substr(2, type_str.size() - 3);
        if (prefix == 'v') {
            size_t comma = inner.rfind(',');
            if (comma == std::string::npos) return nullptr;
            std::string elem_part = inner.substr(0, comma);
            std::string size_part = inner.substr(comma + 1);
            elem_part.erase(remove_if(elem_part.begin(), elem_part.end(), ::isspace), elem_part.end());
            size_part.erase(remove_if(size_part.begin(), size_part.end(), ::isspace), size_part.end());
            return Type::make_vector(parse_type_from_string(elem_part, loc),
                                     parse_shape_dim_literal(size_part),
                                     loc);
        }
        size_t first = inner.find(',');
        if (first == std::string::npos) return nullptr;
        size_t second = inner.find(',', first + 1);
        if (second == std::string::npos) return nullptr;
        std::string elem_part = inner.substr(0, first);
        std::string rows_part = inner.substr(first + 1, second - first - 1);
        std::string cols_part = inner.substr(second + 1);
        elem_part.erase(remove_if(elem_part.begin(), elem_part.end(), ::isspace), elem_part.end());
        rows_part.erase(remove_if(rows_part.begin(), rows_part.end(), ::isspace), rows_part.end());
        cols_part.erase(remove_if(cols_part.begin(), cols_part.end(), ::isspace), cols_part.end());
        return Type::make_matrix(parse_type_from_string(elem_part, loc),
                                 parse_shape_dim_literal(rows_part),
                                 parse_shape_dim_literal(cols_part),
                                 loc);
    };

    // Parse primitive types
    auto parse_fixed_family = [&](char prefix, PrimitiveType kind) -> TypePtr {
        if (type_str.size() < 4 || type_str[0] != prefix) return nullptr;
        size_t dot = type_str.find('.');
        if (dot == std::string::npos || dot <= 1 || dot + 1 >= type_str.size()) {
            return nullptr;
        }
        for (size_t i = 1; i < dot; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(type_str[i]))) {
                return nullptr;
            }
        }
        bool negative_frac = false;
        size_t frac_start = dot + 1;
        if (type_str[frac_start] == '-') {
            negative_frac = true;
            ++frac_start;
        }
        if (frac_start >= type_str.size()) return nullptr;
        for (size_t i = frac_start; i < type_str.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(type_str[i]))) {
                return nullptr;
            }
        }

        uint64_t integer_bits = 0;
        int64_t fractional_bits = 0;
        try {
            integer_bits = std::stoull(type_str.substr(1, dot - 1));
            unsigned long long parsed_frac = std::stoull(type_str.substr(frac_start));
            if (negative_frac) {
                if (parsed_frac > static_cast<unsigned long long>(std::numeric_limits<int64_t>::max())) {
                    throw std::out_of_range("fractional width too negative");
                }
                fractional_bits = -static_cast<int64_t>(parsed_frac);
            } else {
                if (parsed_frac > static_cast<unsigned long long>(std::numeric_limits<int64_t>::max())) {
                    throw std::out_of_range("fractional width too large");
                }
                fractional_bits = static_cast<int64_t>(parsed_frac);
            }
        } catch (const std::exception&) {
            throw CompileError("Invalid fixed-point width in type '#" + type_str + "'", loc);
        }
        if (integer_bits == 0) {
            throw CompileError("Fixed-point integer width must be greater than zero in type '#" + type_str + "'", loc);
        }
        if (integer_bits > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            throw CompileError("Fixed-point total width is too large in type '#" + type_str + "'", loc);
        }
        int64_t total = static_cast<int64_t>(integer_bits) + fractional_bits;
        if (total <= 0) {
            throw CompileError("Fixed-point type '#" + type_str + "' must satisfy I + F > 0", loc);
        }
        return Type::make_primitive(kind, loc, integer_bits, fractional_bits);
    };
    auto parse_int_family = [&](char prefix, PrimitiveType kind) -> TypePtr {
        if (type_str.size() < 2 || type_str[0] != prefix) return nullptr;
        for (size_t i = 1; i < type_str.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(type_str[i]))) {
                return nullptr;
            }
        }
        uint64_t bits = 0;
        try {
            bits = std::stoull(type_str.substr(1));
        } catch (const std::exception&) {
            throw CompileError("Invalid integer width in type '#" + type_str + "'", loc);
        }
        if (bits == 0) {
            throw CompileError("Integer width must be greater than zero in type '#" + type_str + "'", loc);
        }
        return Type::make_primitive(kind, loc, bits);
    };

    if (auto fixed_int = parse_fixed_family('i', PrimitiveType::FixedInt)) return fixed_int;
    if (auto fixed_uint = parse_fixed_family('u', PrimitiveType::FixedUInt)) return fixed_uint;
    if (auto int_type = parse_int_family('i', PrimitiveType::Int)) return int_type;
    if (auto uint_type = parse_int_family('u', PrimitiveType::UInt)) return uint_type;
    if (auto vec_type = parse_vector_or_matrix('v')) return vec_type;
    if (auto mat_type = parse_vector_or_matrix('m')) return mat_type;
    if (type_str == "f16") return Type::make_primitive(PrimitiveType::F16, loc);
    if (type_str == "f32") return Type::make_primitive(PrimitiveType::F32, loc);
    if (type_str == "f64") return Type::make_primitive(PrimitiveType::F64, loc);
    if (type_str == "b") return Type::make_primitive(PrimitiveType::Bool, loc);
    if (type_str == "s") return Type::make_primitive(PrimitiveType::String, loc);

    // Named/complex types are resolved later through bindings and resolver scopes.
    TypePtr named = Type::make_named(type_str, loc);
    if (bindings) {
        Symbol* sym = lookup_type_global(type_str);
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
    if (is_signed_fixed(type->primitive)) return TypeFamily::Other;
    if (is_unsigned_fixed(type->primitive)) return TypeFamily::Other;
    if (is_float(type->primitive)) return TypeFamily::Float;
    return TypeFamily::Other;
}
bool TypeChecker::types_in_same_family(TypePtr a, TypePtr b) {
    return get_type_family(a) == get_type_family(b) &&
           get_type_family(a) != TypeFamily::Other;
}
bool TypeChecker::is_generic_function(StmtPtr func) {
    if (!func || func->kind != Stmt::Kind::FuncDecl) return false;

    bool has_untyped_receiver = false;
    for (size_t i = 0; i < func->ref_param_types.size(); ++i) {
        if (i == 0 && !func->type_namespace.empty()) {
            continue;
        }
        const auto& recv_type = func->ref_param_types[i];
        if (!recv_type || recv_type->kind == Type::Kind::TypeVar) {
            has_untyped_receiver = true;
            break;
        }
    }

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

    return has_untyped_receiver || has_untyped_param || has_typevar_return;
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
                APInt normalized_size(uint64_t(0));
                if (std::holds_alternative<int64_t>(size_val)) {
                    const int64_t signed_size = std::get<int64_t>(size_val);
                    if (signed_size < 0) {
                        throw CompileError("Array size must be non-negative", loc);
                    }
                    normalized_size = APInt(static_cast<uint64_t>(signed_size));
                } else if (std::holds_alternative<uint64_t>(size_val)) {
                    normalized_size = APInt(std::get<uint64_t>(size_val));
                } else if (std::holds_alternative<CTExactInt>(size_val)) {
                    const CTExactInt& exact = std::get<CTExactInt>(size_val);
                    if (!exact.is_unsigned && exact.value.is_negative()) {
                        throw CompileError("Array size must be non-negative", loc);
                    }
                    if (exact.value.is_negative()) {
                        throw CompileError("Array size must be non-negative", loc);
                    }
                    normalized_size = exact.value;
                } else if (std::holds_alternative<bool>(size_val)) {
                    normalized_size = APInt(uint64_t(std::get<bool>(size_val) ? 1ULL : 0ULL));
                } else {
                    throw CompileError("Array size must be an integer compile-time constant", loc);
                }

                // Canonicalize array-size expressions to integer literals so all later
                // type comparisons/hashing use semantic size identity.
                SourceLocation size_loc = type->array_size->location;
                type->array_size = Expr::make_int_exact(normalized_size,
                                                        true,
                                                        size_loc,
                                                        normalized_size.to_string());
            }
            return type;
        }
        case Type::Kind::Vector:
        case Type::Kind::Matrix: {
            type->element_type = validate_type(type->element_type, loc);
            if (!type->element_type || type->element_type->kind != Type::Kind::Primitive ||
                !(type->element_type->primitive == PrimitiveType::Bool ||
                  is_signed_int(type->element_type->primitive) ||
                  is_unsigned_int(type->element_type->primitive) ||
                  is_signed_fixed(type->element_type->primitive) ||
                  is_unsigned_fixed(type->element_type->primitive) ||
                  is_float(type->element_type->primitive))) {
                throw CompileError("Vector and matrix element types must be primitive scalar types", loc);
            }
            auto validate_dim = [&](ExprPtr& dim, const std::string& what) {
                if (!dim) {
                    throw CompileError(what + " must be present", loc);
                }
                CTEQueryResult q = query_constexpr(dim);
                if (q.status == CTEQueryStatus::Error) {
                    throw CompileError(q.message.empty() ? (what + " evaluation failed") : q.message, loc);
                }
                if (q.status != CTEQueryStatus::Known) {
                    throw CompileError(what + " must be a compile-time constant", loc);
                }
                APInt normalized(uint64_t(0));
                if (std::holds_alternative<int64_t>(q.value)) {
                    int64_t v = std::get<int64_t>(q.value);
                    if (v <= 0) throw CompileError(what + " must be positive", loc);
                    normalized = APInt(static_cast<uint64_t>(v));
                } else if (std::holds_alternative<uint64_t>(q.value)) {
                    uint64_t v = std::get<uint64_t>(q.value);
                    if (v == 0) throw CompileError(what + " must be positive", loc);
                    normalized = APInt(v);
                } else if (std::holds_alternative<CTExactInt>(q.value)) {
                    const CTExactInt& exact = std::get<CTExactInt>(q.value);
                    if (exact.value.is_negative() || exact.value.is_zero()) {
                        throw CompileError(what + " must be positive", loc);
                    }
                    normalized = exact.value;
                } else {
                    throw CompileError(what + " must be an integer compile-time constant", loc);
                }
                dim = Expr::make_int_exact(normalized, true, dim->location, normalized.to_string());
            };
            validate_dim(type->array_size,
                         type->kind == Type::Kind::Vector ? "Vector length" : "Matrix row count");
            if (type->kind == Type::Kind::Matrix) {
                validate_dim(type->matrix_cols, "Matrix column count");
            }
            return type;
        }
        case Type::Kind::Named: {
            if (type->type_name.rfind(TUPLE_TYPE_PREFIX, 0) == 0) {
                return type;
            }
            // Check for recursive types
            Symbol* type_sym = nullptr;
            if (bindings) {
                type_sym = bindings->lookup(current_instance_id, type.get());
            }
            if (!type_sym) {
                type_sym = lookup_type_global(type->type_name);
            }
            if (!type_sym) {
                throw CompileError("Undefined type: " + type->type_name, loc);
            }
            if (type_sym->kind != Symbol::Kind::Type) {
                throw CompileError("Identifier is not a type: " + type->type_name, loc);
            }
            if (type_sym->declaration) {
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
        case Type::Kind::Vector:
            if (!type->array_size) {
                if (reason) *reason = "vector length must be compile-time known";
                return false;
            }
            return is_abi_data_type(type->element_type, visiting_named_types, reason);
        case Type::Kind::Matrix:
            if (!type->array_size || !type->matrix_cols) {
                if (reason) *reason = "matrix shape must be compile-time known";
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
                type_sym = lookup_type_global(type->type_name);
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
    if (type->kind == Type::Kind::Vector || type->kind == Type::Kind::Matrix) {
        if (reason) *reason = "top-level vectors and matrices are not allowed at function ABI boundaries";
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
        std::function<void(ExprPtr)> apply_bool = [&](ExprPtr node) {
            if (!node) return;
            if ((node->type &&
                 node->type->kind == Type::Kind::Primitive &&
                 (node->type->primitive == PrimitiveType::Int ||
                  node->type->primitive == PrimitiveType::UInt) &&
                 node->type->integer_bits == 0) ||
                node->kind == Expr::Kind::IntLiteral) {
                node->type = bool_type;
            }
            switch (node->kind) {
                case Expr::Kind::Block:
                    apply_bool(node->result_expr);
                    if (node->result_expr) {
                        node->type = bool_type;
                    }
                    break;
                case Expr::Kind::Binary:
                    apply_bool(node->left);
                    apply_bool(node->right);
                    break;
                case Expr::Kind::Unary:
                    apply_bool(node->operand);
                    break;
                case Expr::Kind::Conditional:
                    apply_bool(node->true_expr);
                    apply_bool(node->false_expr);
                    break;
                case Expr::Kind::Assignment:
                    apply_bool(node->right);
                    break;
                default:
                    break;
            }
        };
        apply_bool(expr);
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
