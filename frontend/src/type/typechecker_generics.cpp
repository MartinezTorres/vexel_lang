#include "typechecker.h"
#include "constants.h"
#include "resolver.h"
#include <unordered_map>

namespace vexel {

namespace {

bool array_sizes_equal(ExprPtr a, ExprPtr b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->kind == Expr::Kind::IntLiteral && b->kind == Expr::Kind::IntLiteral) {
        return a->uint_val == b->uint_val;
    }
    return a.get() == b.get();
}

size_t array_size_hash(ExprPtr size) {
    if (!size) {
        return 0x9e3779b9ULL;
    }
    if (size->kind == Expr::Kind::IntLiteral) {
        return std::hash<uint64_t>{}(size->uint_val);
    }
    return std::hash<const Expr*>{}(size.get());
}

std::string mangle_type_component(TypePtr type) {
    if (!type) return "unknown";

    switch (type->kind) {
        case Type::Kind::Primitive:
            return primitive_name(type->primitive, type->integer_bits);
        case Type::Kind::Named:
            return type->type_name;
        case Type::Kind::Array: {
            std::string component = "array_" + mangle_type_component(type->element_type);
            if (type->array_size && type->array_size->kind == Expr::Kind::IntLiteral) {
                component += "_n" + std::to_string(type->array_size->uint_val);
            } else if (type->array_size) {
                component += "_dyn";
            } else {
                component += "_unsized";
            }
            return component;
        }
        case Type::Kind::TypeVar:
            return "tv_" + type->var_name;
        case Type::Kind::TypeOf:
            return "typeof";
    }
    return "unknown";
}

TypePtr freeze_signature_type(TypePtr type) {
    if (!type) return nullptr;

    TypePtr frozen = std::make_shared<Type>(*type);
    if (type->kind == Type::Kind::Array) {
        frozen->element_type = freeze_signature_type(type->element_type);
        if (type->array_size && type->array_size->kind == Expr::Kind::IntLiteral) {
            frozen->array_size = Expr::make_uint(type->array_size->uint_val,
                                                 type->array_size->location,
                                                 std::to_string(type->array_size->uint_val));
        } else {
            frozen->array_size = type->array_size;
        }
    }
    return frozen;
}

} // namespace

bool TypeSignature::types_equal_static(TypePtr a, TypePtr b) {
    if (!a && !b) return true;
    if (!a || !b) return false;

    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case Type::Kind::Primitive:
            if (a->primitive != b->primitive) return false;
            if (is_signed_int(a->primitive) || is_unsigned_int(a->primitive)) {
                return a->integer_bits == b->integer_bits;
            }
            return true;
        case Type::Kind::Array:
            return types_equal_static(a->element_type, b->element_type) &&
                   array_sizes_equal(a->array_size, b->array_size);
        case Type::Kind::Named:
            return a->type_name == b->type_name;
        case Type::Kind::TypeVar:
            return a->var_name == b->var_name;
        case Type::Kind::TypeOf:
            return a->typeof_expr.get() == b->typeof_expr.get();
    }
    return false;
}
size_t TypeSignatureHash::type_hash(TypePtr t) {
    if (!t) return 0;

    size_t hash = static_cast<size_t>(t->kind);

    switch (t->kind) {
        case Type::Kind::Primitive:
            hash ^= static_cast<size_t>(t->primitive) << 8;
            if (is_signed_int(t->primitive) || is_unsigned_int(t->primitive)) {
                hash ^= std::hash<uint64_t>{}(t->integer_bits) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            break;
        case Type::Kind::Array:
            hash ^= type_hash(t->element_type) << 4;
            hash ^= array_size_hash(t->array_size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            break;
        case Type::Kind::Named:
            hash ^= std::hash<std::string>{}(t->type_name);
            break;
        case Type::Kind::TypeVar:
            hash ^= std::hash<std::string>{}(t->var_name);
            break;
        case Type::Kind::TypeOf:
            hash ^= std::hash<const Expr*>{}(t->typeof_expr.get());
            break;
    }

    return hash;
}
std::string TypeChecker::get_or_create_instantiation(const std::string& func_name,
                                                     const std::vector<TypePtr>& arg_types,
                                                     StmtPtr generic_func) {
    // Create type signature
    TypeSignature sig;
    sig.param_types.reserve(arg_types.size());
    for (const auto& arg_type : arg_types) {
        sig.param_types.push_back(freeze_signature_type(resolve_type(arg_type)));
    }

    // Keep instantiations per module instance.
    int instance_id = current_instance_id;
    std::string lookup_key = func_name + "_inst" + std::to_string(instance_id);
    std::string mangled = mangle_generic_name(func_name, sig.param_types);

    // Check if instantiation already exists
    auto func_it = instantiations.find(lookup_key);
    if (func_it != instantiations.end()) {
        auto inst_it = func_it->second.find(sig);
        if (inst_it != func_it->second.end()) {
            return inst_it->second.mangled_name;
        }
    }

    // Fallback: if an equivalent generated symbol already exists in scope,
    // reuse it and rehydrate the cache entry.
    if (Symbol* existing = lookup_global(mangled)) {
        if (existing->kind == Symbol::Kind::Function && existing->declaration) {
            GenericInstantiation inst;
            inst.mangled_name = mangled;
            inst.declaration = existing->declaration;
            instantiations[lookup_key][sig] = inst;
            return mangled;
        }
    }

    // Create new instantiation
    StmtPtr cloned = clone_function(generic_func);
    substitute_types(cloned, sig.param_types);

    // Generate mangled name
    cloned->func_name = mangled;

    // Mark as non-generic since types have been substituted
    cloned->is_generic = false;
    cloned->is_instantiation = true;

    if (resolver) {
        resolver->resolve_generated_function(cloned, instance_id);
    }

    // Type check the instantiation immediately to infer return type
    check_func_decl(cloned);

    // Store instantiation
    GenericInstantiation inst;
    inst.mangled_name = mangled;
    inst.declaration = cloned;

    instantiations[lookup_key][sig] = inst;
    pending_instantiations.push_back(cloned);

    return mangled;
}
std::string TypeChecker::mangle_generic_name(const std::string& base_name,
                                              const std::vector<TypePtr>& types) {
    std::string result = base_name + "_G";

    for (const auto& type : types) {
        result += "_" + mangle_type_component(type);
    }

    return result;
}
StmtPtr TypeChecker::clone_function(StmtPtr func) {
    auto cloned = std::make_shared<Stmt>();
    cloned->kind = func->kind;
    cloned->location = func->location;
    cloned->annotations = func->annotations;
    cloned->func_name = func->func_name;
    cloned->is_external = func->is_external;
    cloned->is_exported = func->is_exported;
    cloned->is_generic = func->is_generic;
    cloned->is_instantiation = func->is_instantiation;
    cloned->type_namespace = func->type_namespace;

    // Clone parameters
    for (const auto& param : func->params) {
        cloned->params.push_back(Parameter(param.name, param.type, param.is_expression_param, param.location, param.annotations));
    }
    cloned->ref_params = func->ref_params;
    cloned->ref_param_types = func->ref_param_types;

    // Clone return type
    cloned->return_type = func->return_type; // Will be substituted later

    // Clone body
    cloned->body = func->body ? clone_expr(func->body) : nullptr;

    return cloned;
}
ExprPtr TypeChecker::clone_expr(ExprPtr expr) {
    if (!expr) return nullptr;

    auto cloned = std::make_shared<Expr>();
    cloned->kind = expr->kind;
    cloned->location = expr->location;
    cloned->annotations = expr->annotations;
    // Don't copy type - let type-checker infer it fresh for the instantiation
    cloned->type = nullptr;
    // Clone literals
    cloned->uint_val = expr->uint_val;
    cloned->float_val = expr->float_val;
    cloned->string_val = expr->string_val;
    cloned->resource_path = expr->resource_path;

    // Clone identifier info
    cloned->name = expr->name;
    cloned->is_expr_param_ref = expr->is_expr_param_ref;
    cloned->creates_new_variable = expr->creates_new_variable;
    cloned->declared_var_type = expr->declared_var_type;
    cloned->is_mutable_binding = expr->is_mutable_binding;

    // Clone operator
    cloned->op = expr->op;

    // Clone sub-expressions
    cloned->left = clone_expr(expr->left);
    cloned->right = clone_expr(expr->right);
    cloned->operand = clone_expr(expr->operand);
    cloned->condition = clone_expr(expr->condition);
    cloned->true_expr = clone_expr(expr->true_expr);
    cloned->false_expr = clone_expr(expr->false_expr);
    cloned->result_expr = clone_expr(expr->result_expr);
    cloned->target_type = expr->target_type;

    // Clone arguments
    for (const auto& arg : expr->args) {
        cloned->args.push_back(clone_expr(arg));
    }

    // Clone elements
    for (const auto& elem : expr->elements) {
        cloned->elements.push_back(clone_expr(elem));
    }

    // Clone receivers
    for (const auto& rec : expr->receivers) {
        cloned->receivers.push_back(clone_expr(rec));
    }

    // Clone statements (for blocks) - deep cloning
    for (const auto& stmt : expr->statements) {
        cloned->statements.push_back(clone_stmt(stmt));
    }

    return cloned;
}
StmtPtr TypeChecker::clone_stmt(StmtPtr stmt) {
    if (!stmt) return nullptr;

    auto cloned = std::make_shared<Stmt>();
    cloned->kind = stmt->kind;
    cloned->location = stmt->location;
    cloned->annotations = stmt->annotations;
    cloned->is_instantiation = stmt->is_instantiation;
    cloned->ref_param_symbols.clear();

    // Clone based on statement type
    switch (stmt->kind) {
        case Stmt::Kind::Expr:
        case Stmt::Kind::Return:
            cloned->expr = clone_expr(stmt->expr);
            cloned->return_expr = clone_expr(stmt->return_expr);
            break;

        case Stmt::Kind::VarDecl:
            cloned->var_name = stmt->var_name;
            cloned->var_type = stmt->var_type;
            cloned->var_init = clone_expr(stmt->var_init);
            cloned->is_mutable = stmt->is_mutable;
            cloned->is_exported = stmt->is_exported;
            cloned->var_linkage = stmt->var_linkage;
            break;

        case Stmt::Kind::ConditionalStmt:
            cloned->condition = clone_expr(stmt->condition);
            cloned->true_stmt = clone_stmt(stmt->true_stmt);
            break;

        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            // No additional data to clone
            break;

        default:
            // Other statement types shouldn't appear in function bodies
            break;
    }

    return cloned;
}

namespace {

void collect_typevar_bindings(TypePtr pattern,
                              TypePtr concrete,
                              std::unordered_map<std::string, TypePtr>& type_map) {
    if (!pattern || !concrete) return;
    if (pattern->kind == Type::Kind::TypeVar) {
        type_map[pattern->var_name] = concrete;
        return;
    }
    if (pattern->kind == Type::Kind::Array && concrete->kind == Type::Kind::Array) {
        collect_typevar_bindings(pattern->element_type, concrete->element_type, type_map);
    }
}

} // namespace

TypePtr TypeChecker::substitute_type_with_map(TypePtr type,
                                              const std::unordered_map<std::string, TypePtr>& type_map) {
    if (!type) return nullptr;

    if (type->kind == Type::Kind::TypeVar) {
        auto it = type_map.find(type->var_name);
        if (it != type_map.end()) {
            return it->second;
        }
        return type;
    }

    if (type->kind == Type::Kind::Array) {
        TypePtr elem = substitute_type_with_map(type->element_type, type_map);
        if (elem == type->element_type) {
            return type;
        }
        TypePtr cloned = std::make_shared<Type>(*type);
        cloned->element_type = elem;
        return cloned;
    }

    return type;
}

void TypeChecker::substitute_types(StmtPtr func, const std::vector<TypePtr>& concrete_types) {
    if (!func) return;

    std::unordered_map<std::string, TypePtr> substitutions;

    for (size_t i = 0; i < func->params.size() && i < concrete_types.size(); i++) {
        TypePtr concrete = concrete_types[i];
        if (!concrete) continue;
        collect_typevar_bindings(func->params[i].type, concrete, substitutions);
        func->params[i].type = concrete;
    }

    for (auto& param : func->params) {
        param.type = substitute_type_with_map(param.type, substitutions);
    }
    for (auto& ref_type : func->ref_param_types) {
        ref_type = substitute_type_with_map(ref_type, substitutions);
    }
    func->return_type = substitute_type_with_map(func->return_type, substitutions);
    for (auto& ret_type : func->return_types) {
        ret_type = substitute_type_with_map(ret_type, substitutions);
    }

    if (func->body) {
        substitute_types_in_expr(func->body, substitutions);
    }
}

void TypeChecker::substitute_types_in_stmt(StmtPtr stmt,
                                           const std::unordered_map<std::string, TypePtr>& type_map) {
    if (!stmt) return;

    switch (stmt->kind) {
        case Stmt::Kind::Expr:
            substitute_types_in_expr(stmt->expr, type_map);
            break;
        case Stmt::Kind::Return:
            substitute_types_in_expr(stmt->return_expr, type_map);
            break;
        case Stmt::Kind::VarDecl:
            stmt->var_type = substitute_type_with_map(stmt->var_type, type_map);
            substitute_types_in_expr(stmt->var_init, type_map);
            break;
        case Stmt::Kind::ConditionalStmt:
            substitute_types_in_expr(stmt->condition, type_map);
            substitute_types_in_stmt(stmt->true_stmt, type_map);
            break;
        case Stmt::Kind::FuncDecl:
            for (auto& param : stmt->params) {
                param.type = substitute_type_with_map(param.type, type_map);
            }
            for (auto& ref_type : stmt->ref_param_types) {
                ref_type = substitute_type_with_map(ref_type, type_map);
            }
            stmt->return_type = substitute_type_with_map(stmt->return_type, type_map);
            for (auto& ret_type : stmt->return_types) {
                ret_type = substitute_type_with_map(ret_type, type_map);
            }
            substitute_types_in_expr(stmt->body, type_map);
            break;
        default:
            break;
    }
}

void TypeChecker::substitute_types_in_expr(ExprPtr expr, const std::unordered_map<std::string, TypePtr>& type_map) {
    if (!expr) return;

    expr->type = substitute_type_with_map(expr->type, type_map);
    expr->declared_var_type = substitute_type_with_map(expr->declared_var_type, type_map);
    expr->target_type = substitute_type_with_map(expr->target_type, type_map);

    substitute_types_in_expr(expr->left, type_map);
    substitute_types_in_expr(expr->right, type_map);
    substitute_types_in_expr(expr->operand, type_map);
    substitute_types_in_expr(expr->condition, type_map);
    substitute_types_in_expr(expr->true_expr, type_map);
    substitute_types_in_expr(expr->false_expr, type_map);
    substitute_types_in_expr(expr->result_expr, type_map);

    for (auto& arg : expr->args) {
        substitute_types_in_expr(arg, type_map);
    }

    for (auto& elem : expr->elements) {
        substitute_types_in_expr(elem, type_map);
    }

    for (auto& receiver : expr->receivers) {
        substitute_types_in_expr(receiver, type_map);
    }

    for (auto& stmt : expr->statements) {
        substitute_types_in_stmt(stmt, type_map);
    }
}

} // namespace vexel
