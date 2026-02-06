#include "typechecker.h"
#include "constants.h"
#include "resolver.h"
#include <unordered_map>

namespace vexel {

bool TypeSignature::types_equal_static(TypePtr a, TypePtr b) {
    if (!a && !b) return true;
    if (!a || !b) return false;

    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case Type::Kind::Primitive:
            return a->primitive == b->primitive;
        case Type::Kind::Array:
            return types_equal_static(a->element_type, b->element_type);
        case Type::Kind::Named:
            return a->type_name == b->type_name;
        case Type::Kind::TypeVar:
            return a->var_name == b->var_name;
    }
    return false;
}
size_t TypeSignatureHash::type_hash(TypePtr t) {
    if (!t) return 0;

    size_t hash = static_cast<size_t>(t->kind);

    switch (t->kind) {
        case Type::Kind::Primitive:
            hash ^= static_cast<size_t>(t->primitive) << 8;
            break;
        case Type::Kind::Array:
            hash ^= type_hash(t->element_type) << 4;
            break;
        case Type::Kind::Named:
            hash ^= std::hash<std::string>{}(t->type_name);
            break;
        case Type::Kind::TypeVar:
            hash ^= std::hash<std::string>{}(t->var_name);
            break;
    }

    return hash;
}
std::string TypeChecker::get_or_create_instantiation(const std::string& func_name,
                                                     const std::vector<TypePtr>& arg_types,
                                                     StmtPtr generic_func) {
    // Create type signature
    TypeSignature sig;
    sig.param_types = arg_types;

    // Keep instantiations per module instance.
    int instance_id = current_instance_id;
    std::string lookup_key = func_name + "_inst" + std::to_string(instance_id);

    // Check if instantiation already exists
    auto func_it = instantiations.find(lookup_key);
    if (func_it != instantiations.end()) {
        auto inst_it = func_it->second.find(sig);
        if (inst_it != func_it->second.end()) {
            return inst_it->second.mangled_name;
        }
    }

    // Create new instantiation
    StmtPtr cloned = clone_function(generic_func);
    substitute_types(cloned, arg_types);

    // Generate mangled name
    std::string mangled = mangle_generic_name(func_name, arg_types);
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
        if (type->kind == Type::Kind::Primitive) {
            result += "_" + primitive_name(type->primitive);
        } else if (type->kind == Type::Kind::Named) {
            result += "_" + type->type_name;
        } else if (type->kind == Type::Kind::Array) {
            result += "_array";
        }
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
void TypeChecker::substitute_types(StmtPtr func, const std::vector<TypePtr>& concrete_types) {
    // Build type substitution map
    std::unordered_map<std::string, TypePtr> type_map;

    for (size_t i = 0; i < func->params.size() && i < concrete_types.size(); i++) {
        func->params[i].type = concrete_types[i];
        // If there was a type variable, map it
        // This is simplified - full implementation would track type variable names
    }

    // Substitute types in body
    if (func->body) {
        substitute_types_in_expr(func->body, type_map);
    }
}
void TypeChecker::substitute_types_in_expr(ExprPtr expr, const std::unordered_map<std::string, TypePtr>& type_map) {
    if (!expr) return;

    // Recursively substitute in sub-expressions
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
}

} // namespace vexel
