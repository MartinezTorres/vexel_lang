#include "typechecker.h"
#include "lexer.h"
#include "parser.h"
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace vexel {

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

} // namespace vexel
