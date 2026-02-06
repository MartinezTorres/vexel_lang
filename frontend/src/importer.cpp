#include "importer.h"
#include "typechecker.h"
#include "lexer.h"
#include "parser.h"
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace vexel {

Importer::Importer(TypeChecker* checker)
    : checker(checker) {}

void Importer::handle_import(StmtPtr stmt) {
    if (!checker || !stmt) return;

    std::string resolved_path;
    if (!try_resolve_module_path(stmt->import_path, stmt->location.filename, resolved_path)) {
        throw CompileError("Import failed: cannot resolve module", stmt->location);
    }

    if (checker->scope_loaded_modules[checker->current_scope].count(resolved_path)) {
        return;
    }
    checker->scope_loaded_modules[checker->current_scope].insert(resolved_path);

    Module imported_mod = load_module_file(resolved_path);
    std::vector<StmtPtr> cloned_decls = clone_module_declarations(imported_mod.top_level);

    for (auto& decl : cloned_decls) {
        decl->scope_instance_id = checker->current_scope->id;
        checker->check_stmt(decl);

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

        if (!symbol_name.empty() && checker->current_scope->symbols.count(symbol_name)) {
            checker->current_scope->symbols[symbol_name].scope_instance_id = checker->current_scope->id;
        }

        tag_scope_instances(decl, checker->current_scope->id);

        if (checker->current_module) {
            checker->current_module->top_level.push_back(decl);
        }
    }
}

bool Importer::try_resolve_module_path(const std::vector<std::string>& import_path,
                                       const std::string& current_file,
                                       std::string& out_path) {
    std::string relative = checker->join_import_path(import_path) + ".vx";
    return checker->try_resolve_relative_path(relative, current_file, out_path);
}

Module Importer::load_module_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw CompileError("Cannot open file: " + path, SourceLocation());
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Lexer lexer(source, path);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse_module(path, path);
}

StmtPtr Importer::clone_stmt_deep(StmtPtr stmt) {
    if (!stmt) return nullptr;

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
            cloned->body = checker->clone_expr(stmt->body);
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
            cloned->var_init = checker->clone_expr(stmt->var_init);
            cloned->is_mutable = stmt->is_mutable;
            break;
        case Stmt::Kind::Import:
            cloned->import_path = stmt->import_path;
            break;
        case Stmt::Kind::Expr:
            cloned->expr = checker->clone_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            cloned->return_expr = checker->clone_expr(stmt->return_expr);
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
        case Stmt::Kind::ConditionalStmt:
            cloned->condition = checker->clone_expr(stmt->condition);
            cloned->true_stmt = clone_stmt_deep(stmt->true_stmt);
            break;
    }

    return cloned;
}

std::vector<StmtPtr> Importer::clone_module_declarations(const std::vector<StmtPtr>& decls) {
    std::vector<StmtPtr> cloned;
    for (const auto& stmt : decls) {
        if (stmt->kind != Stmt::Kind::Import) {
            cloned.push_back(clone_stmt_deep(stmt));
        }
    }
    return cloned;
}

void Importer::tag_scope_instances(StmtPtr stmt, int instance_id) {
    if (!stmt || !checker) return;

    if (stmt->kind == Stmt::Kind::FuncDecl || stmt->kind == Stmt::Kind::VarDecl) {
        std::unordered_set<std::string> module_symbols;
        for (const auto& pair : checker->current_scope->symbols) {
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

void Importer::tag_scope_instances_in_expr(ExprPtr expr,
                                           int instance_id,
                                           const std::unordered_set<std::string>& module_symbols) {
    if (!expr || !checker) return;

    if (expr->kind == Expr::Kind::Identifier && module_symbols.count(expr->name)) {
        expr->scope_instance_id = instance_id;
    }

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
