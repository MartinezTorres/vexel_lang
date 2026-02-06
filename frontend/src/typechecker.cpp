#include "typechecker.h"
#include "evaluator.h"
#include "type_use_validator.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iostream>
#include "lexer.h"
#include "parser.h"
#include "constants.h"

namespace vexel {

TypeChecker::TypeChecker(const std::string& proj_root, bool allow_process_exprs)
    : current_scope(nullptr),
      type_var_counter(0),
      scope_counter(0),
      loop_depth(0),
      project_root(proj_root),
      allow_process(allow_process_exprs),
      current_module(nullptr) {
    push_scope();
}

void TypeChecker::push_scope() {
    scopes.push_back(std::make_unique<Scope>(current_scope, scope_counter++));
    current_scope = scopes.back().get();
}

void TypeChecker::pop_scope() {
    if (current_scope->parent) {
        current_scope = current_scope->parent;
    }
}

void TypeChecker::check_module(Module& mod) {
    current_module = &mod;
    validate_annotations(mod);

    // Two-pass checking: functions and types allow forward references, constants do not
    // Pass 1: Declare all functions and types (without checking bodies/initializers)
    for (auto& stmt : mod.top_level) {
        if (stmt->kind == Stmt::Kind::FuncDecl) {
            // Declare function signature only
            std::string func_name = stmt->func_name;
            if (!stmt->type_namespace.empty()) {
                func_name = stmt->type_namespace + "::" + stmt->func_name;
            }

            // Check if generic
            stmt->is_generic = is_generic_function(stmt);

            verify_no_shadowing(func_name, stmt->location);

            Symbol sym;
            sym.kind = Symbol::Kind::Function;
            sym.is_external = stmt->is_external;
            sym.is_exported = stmt->is_exported;
            sym.declaration = stmt;
            current_scope->define(func_name, sym);
        } else if (stmt->kind == Stmt::Kind::TypeDecl) {
            verify_no_shadowing(stmt->type_decl_name, stmt->location);

            Symbol sym;
            sym.kind = Symbol::Kind::Type;
            sym.declaration = stmt;
            current_scope->define(stmt->type_decl_name, sym);
        }
        // Note: Constants are NOT pre-declared - they must be defined in parse order
    }

    // Pass 2: Type-check all statements in order (constants, functions, variables)
    // This enforces parse-order initialization for constants. Iterate by index
    // because imports/generic instantiations can append new statements.
    for (size_t i = 0; i < mod.top_level.size(); ++i) {
        StmtPtr stmt = mod.top_level[i];
        check_stmt(stmt);
    }

    // Process pending generic instantiations
    while (!pending_instantiations.empty()) {
        std::vector<StmtPtr> batch;
        batch.swap(pending_instantiations);

        for (auto& inst : batch) {
            // Type check the instantiation
            check_func_decl(inst);

            // Add to module
            mod.top_level.push_back(inst);
        }
    }
}

void TypeChecker::check_stmt(StmtPtr stmt) {
    if (!stmt) return;
    if (checked_statements.count(stmt.get())) {
        return;
    }
    checked_statements.insert(stmt.get());

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            check_func_decl(stmt);
            break;
        case Stmt::Kind::TypeDecl:
            check_type_decl(stmt);
            break;
        case Stmt::Kind::VarDecl:
            check_var_decl(stmt);
            break;
        case Stmt::Kind::Import:
            check_import(stmt);
            break;
        case Stmt::Kind::Expr:
            if (stmt->expr) check_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            if (stmt->return_expr) check_expr(stmt->return_expr);
            break;
        case Stmt::Kind::Break:
            if (loop_depth == 0) {
                throw CompileError("Break statement outside of loop", stmt->location);
            }
            break;
        case Stmt::Kind::Continue:
            if (loop_depth == 0) {
                throw CompileError("Continue statement outside of loop", stmt->location);
            }
            break;
        case Stmt::Kind::ConditionalStmt: {
            TypePtr cond_type = stmt->condition ? check_expr(stmt->condition) : nullptr;
            require_boolean(cond_type, stmt->condition ? stmt->condition->location : stmt->location,
                            "Conditional statement");
            check_stmt(stmt->true_stmt);
            break;
        }
        default:
            break;
    }
}

void TypeChecker::check_func_decl(StmtPtr stmt) {
    // Build the function name (qualified if it's a method)
    std::string func_name = stmt->func_name;
    if (!stmt->type_namespace.empty()) {
        func_name = stmt->type_namespace + "::" + stmt->func_name;
    }

    // Check if function is generic (has parameters without types)
    // Don't recalculate if already explicitly set to false (for instantiations)
    bool was_set_to_non_generic = !stmt->is_generic;
    stmt->is_generic = is_generic_function(stmt);
    // If it was explicitly marked as non-generic (e.g., instantiation), keep it that way
    if (was_set_to_non_generic && stmt->is_generic) {
        stmt->is_generic = false;
    }

    if (stmt->is_generic && (stmt->is_exported || stmt->is_external)) {
        throw CompileError("Generic functions cannot be exported or external", stmt->location);
    }

    // Check if already declared (from pass 1)
    Symbol* existing = current_scope->lookup(func_name);
    if (!existing || existing->kind != Symbol::Kind::Function) {
        // Not declared yet - this must be a generic instantiation
        verify_no_shadowing(func_name, stmt->location);

        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.is_external = stmt->is_external;
        sym.is_exported = stmt->is_exported;
        sym.declaration = stmt;
        current_scope->define(func_name, sym);
    }

    // Validate external function signatures (primitives only)
    if (stmt->is_external) {
        for (const auto& param : stmt->params) {
            if (param.type && !is_primitive_type(param.type)) {
                throw CompileError("External functions can only use primitive types (found " +
                                 param.type->to_string() + " in parameter " + param.name + ")", stmt->location);
            }
        }
        if (stmt->return_type && !is_primitive_type(stmt->return_type)) {
            throw CompileError("External functions can only use primitive types in return type (found " +
                             stmt->return_type->to_string() + ")", stmt->location);
        }
    }

    // Skip type-checking body for generic functions - only type-check instantiations
    if (stmt->is_generic) {
        return;
    }

    if (!stmt->is_external && stmt->body) {
        push_scope();

        if (stmt->ref_param_types.size() < stmt->ref_params.size()) {
            stmt->ref_param_types.resize(stmt->ref_params.size(), nullptr);
        }
        // Add receiver parameters (mutable) if this is a reference-taking function
        for (size_t i = 0; i < stmt->ref_params.size(); ++i) {
            const std::string& ref_param = stmt->ref_params[i];
            Symbol rsym;
            rsym.kind = Symbol::Kind::Variable;
            // If this is a method, infer receiver type from type namespace
            if (!stmt->type_namespace.empty() && i == 0) {
                rsym.type = Type::make_named(stmt->type_namespace, stmt->location);
            } else {
                rsym.type = make_fresh_typevar();  // Type will be inferred from usage
            }
            rsym.is_mutable = true;
            current_scope->define(ref_param, rsym);
            stmt->ref_param_types[i] = rsym.type;
        }

        // Add regular parameters (immutable)
        for (auto& param : stmt->params) {
            if (!param.type) {
                param.type = make_fresh_typevar();
            }
            Symbol psym;
            psym.kind = Symbol::Kind::Variable;
            psym.type = param.type;
            psym.is_mutable = false;
            current_scope->define(param.name, psym);
        }

        TypePtr body_type = check_expr(stmt->body);

        // Handle tuple return types
        if (!stmt->return_types.empty()) {
            // Generate anonymous tuple type: #__Tuple2_T1_T2(__0:T1, __1:T2)
            // For now, just use the first return type (simplified)
            // Full implementation would create the tuple type
            if (!stmt->return_type) {
                stmt->return_type = stmt->return_types[0];  // Placeholder
            }
        } else if (!stmt->return_type) {
            stmt->return_type = body_type;
        } else {
            if (!types_compatible(body_type, stmt->return_type)) {
                throw CompileError("Return type mismatch in function '" + stmt->func_name + "'",
                                   stmt->location);
            }
        }

        pop_scope();
    }
}

void TypeChecker::check_type_decl(StmtPtr stmt) {
    // Check if already declared (from pass 1)
    Symbol* existing = current_scope->lookup(stmt->type_decl_name);
    bool already_declared = (existing && existing->kind == Symbol::Kind::Type);

    if (!already_declared) {
        verify_no_shadowing(stmt->type_decl_name, stmt->location);

        Symbol sym;
        sym.kind = Symbol::Kind::Type;
        sym.declaration = stmt;
        current_scope->define(stmt->type_decl_name, sym);
    }

    // Check fields for type inference
    for (auto& field : stmt->fields) {
        if (!field.type) {
            field.type = make_fresh_typevar();
        }
    }

    // Check for recursive type (type contains itself as a field)
    check_recursive_type(stmt->type_decl_name, stmt, stmt->location);
}

void TypeChecker::check_var_decl(StmtPtr stmt) {
    // Verify no shadowing
    verify_no_shadowing(stmt->var_name, stmt->location);

    TypePtr type = stmt->var_type;
    if (stmt->var_init) {
        TypePtr init_type = check_expr(stmt->var_init);
        if (!type) {
            type = init_type;
            stmt->var_type = type;
        } else if (type->kind == Type::Kind::Array &&
                   stmt->var_init->kind == Expr::Kind::ArrayLiteral) {
            bool compatible = true;
            for (auto& el : stmt->var_init->elements) {
                if (!types_compatible(el->type, type->element_type) &&
                    !literal_assignable_to(type->element_type, el)) {
                    compatible = false;
                    break;
                }
            }
            if (compatible) {
                stmt->var_init->type = type;
            } else {
                throw CompileError("Type mismatch in variable initialization", stmt->location);
            }
        } else if (stmt->var_init->kind == Expr::Kind::Cast) {
            // Allow explicit casts to satisfy the annotated type
            stmt->var_init->type = type;
        } else if (!types_compatible(init_type, type)) {
            if (literal_assignable_to(type, stmt->var_init)) {
                stmt->var_init->type = type;
            } else {
                throw CompileError("Type mismatch in variable initialization", stmt->location);
            }
        }
    } else if (!type) {
        throw CompileError("Variable must have type annotation or initializer", stmt->location);
    }

    // Validate the type
    validate_type(type, stmt->location);

    // Define the variable/constant
    Symbol sym;
    sym.kind = stmt->is_mutable ? Symbol::Kind::Variable : Symbol::Kind::Constant;
    sym.type = type;
    sym.is_mutable = stmt->is_mutable;
    sym.declaration = stmt;
    current_scope->define(stmt->var_name, sym);
}

void TypeChecker::check_import(StmtPtr stmt) {
    std::string resolved_path;
    if (!try_resolve_module_path(stmt->import_path, stmt->location.filename, resolved_path)) {
        throw CompileError("Import failed: cannot resolve module", stmt->location);
    }

    if (scope_loaded_modules[current_scope].count(resolved_path)) {
        return;
    }
    scope_loaded_modules[current_scope].insert(resolved_path);

    Module imported_mod = load_module_file(resolved_path);
    std::vector<StmtPtr> cloned_decls = clone_module_declarations(imported_mod.top_level);

    for (auto& decl : cloned_decls) {
        decl->scope_instance_id = current_scope->id;
        check_stmt(decl);

        std::string symbol_name;
        if (decl->kind == Stmt::Kind::FuncDecl) {
            symbol_name = decl->func_name;
            if (!decl->type_namespace.empty()) {
                symbol_name = decl->type_namespace + "::" + decl->func_name;
            }
        } else if (decl->kind == Stmt::Kind::VarDecl) {
            symbol_name = decl->var_name;
        } else if (decl->kind == Stmt::Kind::TypeDecl) {
            symbol_name = decl->type_decl_name;
        }

        if (!symbol_name.empty() && current_scope->symbols.count(symbol_name)) {
            current_scope->symbols[symbol_name].scope_instance_id = current_scope->id;
        }

        tag_scope_instances(decl, current_scope->id);

        if (current_module) {
            current_module->top_level.push_back(decl);
        }
    }
}

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

bool TypeChecker::try_resolve_relative_path(const std::string& relative, const std::string& current_file, std::string& out_path) {
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

bool TypeChecker::try_resolve_module_path(const std::vector<std::string>& import_path, const std::string& current_file, std::string& out_path) {
    std::string relative = join_import_path(import_path) + ".vx";
    return try_resolve_relative_path(relative, current_file, out_path);
}

bool TypeChecker::try_resolve_resource_path(const std::vector<std::string>& import_path, const std::string& current_file, std::string& out_path) {
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

Module TypeChecker::load_module_file(const std::string& path) {
    // Read file
    std::ifstream file(path);
    if (!file) {
        throw CompileError("Cannot open file: " + path, SourceLocation());
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Lex and parse
    Lexer lexer(source, path);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse_module(path, path);
}

static bool ann_is(const Annotation& ann, const std::string& name) {
    return ann.name == name;
}

void TypeChecker::warn_annotation(const Annotation& ann, const std::string& msg) {
    std::cerr << "Warning: " << msg;
    if (!ann.location.filename.empty()) {
        std::cerr << " at " << ann.location.filename << ":" << ann.location.line << ":" << ann.location.column;
    }
    std::cerr << " [[" << ann.name << "]]" << std::endl;
}

void TypeChecker::validate_annotations(const Module& mod) {
    for (const auto& stmt : mod.top_level) {
        validate_stmt_annotations(stmt);
    }
}

void TypeChecker::validate_stmt_annotations(StmtPtr stmt) {
    if (!stmt) return;

    auto warn_if = [&](const Annotation& ann, bool condition, const std::string& msg) {
        if (condition) warn_annotation(ann, msg);
    };

    for (const auto& ann : stmt->annotations) {
        bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                          ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
                          ann_is(ann, "inline") || ann_is(ann, "noinline");
        if (!recognized) continue;
        switch (stmt->kind) {
            case Stmt::Kind::FuncDecl:
                // All recognized annotations allowed on functions
                break;
            case Stmt::Kind::VarDecl:
                warn_if(ann, ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                 ann_is(ann, "nonreentrant") || ann_is(ann, "inline") || ann_is(ann, "noinline"),
                        "[[" + ann.name + "]] is only meaningful on functions");
                break;
            default:
                warn_if(ann, true, "[[" + ann.name + "]] is only supported on functions or globals");
                break;
        }
    }

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl: {
            for (const auto& param : stmt->params) {
                for (const auto& ann : param.annotations) {
                    bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                      ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
                                      ann_is(ann, "inline") || ann_is(ann, "noinline");
                    if (recognized) {
                        warn_annotation(ann, "[[" + ann.name + "]] is not used on parameters");
                    }
                }
            }
            if (stmt->body) {
                validate_expr_annotations(stmt->body);
            }
            break;
        }
        case Stmt::Kind::VarDecl:
            validate_expr_annotations(stmt->var_init);
            break;
        case Stmt::Kind::TypeDecl:
            for (const auto& field : stmt->fields) {
                for (const auto& ann : field.annotations) {
                    bool recognized = ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
                                      ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
                                      ann_is(ann, "inline") || ann_is(ann, "noinline");
                    if (recognized) {
                        warn_annotation(ann, "[[" + ann.name + "]] is not used on struct fields");
                    }
                }
            }
            break;
        case Stmt::Kind::Import:
            break;
        case Stmt::Kind::Expr:
            validate_expr_annotations(stmt->expr);
            break;
        case Stmt::Kind::Return:
            validate_expr_annotations(stmt->return_expr);
            break;
        case Stmt::Kind::ConditionalStmt:
            validate_expr_annotations(stmt->condition);
            validate_stmt_annotations(stmt->true_stmt);
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}

void TypeChecker::validate_expr_annotations(ExprPtr expr) {
    if (!expr) return;

    auto warn_all = [&](const Annotation& ann) {
        if (ann_is(ann, "hot") || ann_is(ann, "cold") || ann_is(ann, "reentrant") ||
            ann_is(ann, "nonreentrant") || ann_is(ann, "nonbanked") ||
            ann_is(ann, "inline") || ann_is(ann, "noinline")) {
            warn_annotation(ann, "[[" + ann.name + "]] is not used on expressions");
        }
    };
    for (const auto& ann : expr->annotations) {
        warn_all(ann);
    }

    validate_expr_annotations(expr->left);
    validate_expr_annotations(expr->right);
    validate_expr_annotations(expr->operand);
    validate_expr_annotations(expr->condition);
    validate_expr_annotations(expr->true_expr);
    validate_expr_annotations(expr->false_expr);
    for (const auto& arg : expr->args) validate_expr_annotations(arg);
    for (const auto& elem : expr->elements) validate_expr_annotations(elem);
    for (const auto& st : expr->statements) validate_stmt_annotations(st);
    validate_expr_annotations(expr->result_expr);
}

StmtPtr TypeChecker::clone_stmt_deep(StmtPtr stmt) {
    if (!stmt) return nullptr;

    // Use existing clone_stmt for expressions, add full statement cloning
    auto cloned = std::make_shared<Stmt>();
    cloned->kind = stmt->kind;
    cloned->location = stmt->location;
    cloned->annotations = stmt->annotations;

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            cloned->func_name = stmt->func_name;
            cloned->params = stmt->params;
            cloned->ref_params = stmt->ref_params;
            cloned->ref_param_types = stmt->ref_param_types;
            cloned->return_type = stmt->return_type;
            cloned->body = clone_expr(stmt->body);
            cloned->is_external = stmt->is_external;
            cloned->is_exported = stmt->is_exported;
            cloned->type_namespace = stmt->type_namespace;
            cloned->return_types = stmt->return_types;
            cloned->is_generic = stmt->is_generic;
            break;
        case Stmt::Kind::TypeDecl:
            cloned->type_decl_name = stmt->type_decl_name;
            cloned->fields = stmt->fields;
            break;
        case Stmt::Kind::VarDecl:
            cloned->var_name = stmt->var_name;
            cloned->var_type = stmt->var_type;
            cloned->var_init = clone_expr(stmt->var_init);
            cloned->is_mutable = stmt->is_mutable;
            break;
        case Stmt::Kind::Import:
            cloned->import_path = stmt->import_path;
            break;
        case Stmt::Kind::Expr:
            cloned->expr = clone_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            cloned->return_expr = clone_expr(stmt->return_expr);
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
        case Stmt::Kind::ConditionalStmt:
            cloned->condition = clone_expr(stmt->condition);
            cloned->true_stmt = clone_stmt_deep(stmt->true_stmt);
            break;
    }

    return cloned;
}

std::vector<StmtPtr> TypeChecker::clone_module_declarations(const std::vector<StmtPtr>& decls) {
    std::vector<StmtPtr> cloned;
    for (const auto& stmt : decls) {
        // Don't clone Import statements
        if (stmt->kind != Stmt::Kind::Import) {
            cloned.push_back(clone_stmt_deep(stmt));
        }
    }
    return cloned;
}

// TypeSignature helper implementations
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

// Generic monomorphization implementation
std::string TypeChecker::get_or_create_instantiation(const std::string& func_name,
                                                     const std::vector<TypePtr>& arg_types,
                                                     StmtPtr generic_func) {
    // Create type signature
    TypeSignature sig;
    sig.param_types = arg_types;

    // For imported functions with scope_instance_id, make lookups scope-specific
    // by including scope_instance_id in the function name key
    int scope_id = generic_func->scope_instance_id;
    std::string lookup_key = func_name;
    if (scope_id >= 0) {
        lookup_key += "_scope" + std::to_string(scope_id);
    }

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

    // Inherit scope_instance_id from the original function
    cloned->scope_instance_id = generic_func->scope_instance_id;

    // Mark as non-generic since types have been substituted
    cloned->is_generic = false;

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

void TypeChecker::rename_identifiers(StmtPtr stmt, const std::unordered_map<std::string, std::string>& name_map) {
    if (!stmt) return;

    if (stmt->kind == Stmt::Kind::FuncDecl) {
        // Don't rename function body variables - only references to module-level items
        if (stmt->body) {
            rename_identifiers_in_expr(stmt->body, name_map);
        }
    } else if (stmt->kind == Stmt::Kind::VarDecl) {
        if (stmt->var_init) {
            rename_identifiers_in_expr(stmt->var_init, name_map);
        }
    } else if (stmt->kind == Stmt::Kind::Expr) {
        rename_identifiers_in_expr(stmt->expr, name_map);
    } else if (stmt->kind == Stmt::Kind::Return) {
        rename_identifiers_in_expr(stmt->return_expr, name_map);
    } else if (stmt->kind == Stmt::Kind::ConditionalStmt) {
        rename_identifiers_in_expr(stmt->condition, name_map);
        rename_identifiers(stmt->true_stmt, name_map);
    }
}

void TypeChecker::rename_identifiers_in_expr(ExprPtr expr, const std::unordered_map<std::string, std::string>& name_map) {
    if (!expr) return;

    // Rename identifier if it's in the map
    if (expr->kind == Expr::Kind::Identifier) {
        auto it = name_map.find(expr->name);
        if (it != name_map.end()) {
            expr->name = it->second;
        }
    }

    // Recursively rename in sub-expressions (only fields that exist in Expr)
    rename_identifiers_in_expr(expr->left, name_map);
    rename_identifiers_in_expr(expr->right, name_map);
    rename_identifiers_in_expr(expr->operand, name_map);
    rename_identifiers_in_expr(expr->condition, name_map);
    rename_identifiers_in_expr(expr->true_expr, name_map);
    rename_identifiers_in_expr(expr->false_expr, name_map);
    rename_identifiers_in_expr(expr->result_expr, name_map);

    for (auto& arg : expr->args) {
        rename_identifiers_in_expr(arg, name_map);
    }

    for (auto& elem : expr->elements) {
        rename_identifiers_in_expr(elem, name_map);
    }

    for (auto& s : expr->statements) {
        rename_identifiers(s, name_map);
    }
}

void TypeChecker::tag_scope_instances(StmtPtr stmt, int instance_id) {
    if (!stmt) return;

    if (stmt->kind == Stmt::Kind::FuncDecl || stmt->kind == Stmt::Kind::VarDecl) {
        // Collect module-level symbols
        std::unordered_set<std::string> module_symbols;
        for (const auto& pair : current_scope->symbols) {
            if (pair.second.declaration && pair.second.declaration->scope_instance_id == instance_id) {
                module_symbols.insert(pair.first);
            }
        }

        if (stmt->kind == Stmt::Kind::FuncDecl && stmt->body) {
            tag_scope_instances_in_expr(stmt->body, instance_id, module_symbols);
        } else if (stmt->kind == Stmt::Kind::VarDecl && stmt->var_init) {
            tag_scope_instances_in_expr(stmt->var_init, instance_id, module_symbols);
        }
    }
}

void TypeChecker::tag_scope_instances_in_expr(ExprPtr expr, int instance_id, const std::unordered_set<std::string>& module_symbols) {
    if (!expr) return;

    // Tag identifier if it's a module symbol
    if (expr->kind == Expr::Kind::Identifier && module_symbols.count(expr->name)) {
        expr->scope_instance_id = instance_id;
    }

    // Recursively tag sub-expressions
    tag_scope_instances_in_expr(expr->left, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->right, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->operand, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->condition, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->true_expr, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->false_expr, instance_id, module_symbols);
    tag_scope_instances_in_expr(expr->result_expr, instance_id, module_symbols);

    for (auto& arg : expr->args) {
        tag_scope_instances_in_expr(arg, instance_id, module_symbols);
    }

    for (auto& elem : expr->elements) {
        tag_scope_instances_in_expr(elem, instance_id, module_symbols);
    }

    for (auto& s : expr->statements) {
        tag_scope_instances(s, instance_id);
    }
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

void TypeChecker::validate_type_usage(const Module& mod, const AnalysisFacts& facts) {
    TypeUseContext ctx;
    ctx.resolve_type = [this](TypePtr type) { return resolve_type(type); };
    ctx.constexpr_condition = [this](ExprPtr expr) { return constexpr_condition(expr); };
    ::vexel::validate_type_usage(mod, facts, ctx);
}

}
