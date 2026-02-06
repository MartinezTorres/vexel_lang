#include "typechecker.h"
#include "evaluator.h"
#include "constants.h"
#include <filesystem>
#include <limits>

namespace vexel {

void TypeChecker::verify_no_shadowing(const std::string& name, const SourceLocation& loc) {
    if (name == "_") return; // Underscore can shadow

    Symbol* existing = current_scope->lookup(name);
    if (existing) {
        throw CompileError("Name shadows existing definition: " + name, loc);
    }
}
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

    // For now, return type variable for complex types
    // TODO: Handle array types, named types, etc.
    return Type::make_named(type_str, loc);
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
                CompileTimeEvaluator evaluator(this);
                CTValue size_val;
                if (!evaluator.try_evaluate(type->array_size, size_val)) {
                    throw CompileError("Array size must be a compile-time constant", loc);
                }
            }
            break;
        }
        case Type::Kind::Named: {
            // Check for recursive types
            Symbol* type_sym = current_scope->lookup(type->type_name);
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
void TypeChecker::require_unsigned_integer(TypePtr type, const SourceLocation& loc, const std::string& context) {
    if (!type || type->kind != Type::Kind::Primitive || !is_unsigned_int(type->primitive)) {
        throw CompileError(context + " requires unsigned integer operands", loc);
    }
}

bool TypeChecker::try_resolve_relative_path(const std::string& relative,
                                            const std::string& current_file,
                                            std::string& out_path) {
    std::filesystem::path rel_path(relative);

    if (!project_root.empty()) {
        std::filesystem::path full = std::filesystem::path(project_root) / rel_path;
        if (std::filesystem::exists(full)) {
            out_path = full.string();
            return true;
        }
    }

    if (!current_file.empty()) {
        std::filesystem::path current_dir = std::filesystem::path(current_file).parent_path();
        if (!current_dir.empty()) {
            std::filesystem::path full = current_dir / rel_path;
            if (std::filesystem::exists(full)) {
                out_path = full.string();
                return true;
            }
        }
    }

    return false;
}

bool TypeChecker::try_resolve_resource_path(const std::vector<std::string>& import_path,
                                            const std::string& current_file,
                                            std::string& out_path) {
    std::string relative = join_import_path(import_path);
    return try_resolve_relative_path(relative, current_file, out_path);
}

std::string TypeChecker::join_import_path(const std::vector<std::string>& import_path) {
    std::string path;
    for (size_t i = 0; i < import_path.size(); ++i) {
        if (i > 0) path += "/";
        path += import_path[i];
    }
    return path;
}

} // namespace vexel
