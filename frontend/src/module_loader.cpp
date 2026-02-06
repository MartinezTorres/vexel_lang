#include "module_loader.h"
#include "expr_access.h"
#include "lexer.h"
#include "parser.h"
#include "path_utils.h"
#include <filesystem>
#include <fstream>

namespace vexel {

namespace {
std::string normalize_path(const std::string& path) {
    if (path.empty()) return path;
    return std::filesystem::path(path).lexically_normal().string();
}
}

Program ModuleLoader::load(const std::string& entry_path) {
    Program program;
    load_module(normalize_path(entry_path), program);
    return program;
}

ModuleId ModuleLoader::load_module(const std::string& path, Program& program) {
    std::string normalized = normalize_path(path);
    auto it = program.path_to_id.find(normalized);
    if (it != program.path_to_id.end()) {
        return it->second;
    }

    ModuleInfo info;
    info.id = static_cast<ModuleId>(program.modules.size());
    info.path = normalized;
    info.module = parse_module_file(normalized);
    program.modules.push_back(std::move(info));
    program.path_to_id[normalized] = program.modules.back().id;

    // Collect imports from this module to load recursively.
    std::vector<std::vector<std::string>> imports;
    for (const auto& stmt : program.modules.back().module.top_level) {
        collect_imports(stmt, imports);
    }

    for (const auto& import_path : imports) {
        std::string resolved;
        if (!resolve_module_path(import_path, normalized, resolved)) {
            continue; // Resolver will report missing imports later.
        }
        load_module(normalize_path(resolved), program);
    }

    return program.modules.back().id;
}

void ModuleLoader::collect_imports(StmtPtr stmt, std::vector<std::vector<std::string>>& out) const {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::Import:
            out.push_back(stmt->import_path);
            break;
        case Stmt::Kind::Expr:
            collect_imports_expr(stmt->expr, out);
            break;
        case Stmt::Kind::Return:
            collect_imports_expr(stmt->return_expr, out);
            break;
        case Stmt::Kind::ConditionalStmt:
            collect_imports_expr(stmt->condition, out);
            collect_imports(stmt->true_stmt, out);
            break;
        case Stmt::Kind::FuncDecl:
            collect_imports_expr(stmt->body, out);
            break;
        case Stmt::Kind::VarDecl:
            collect_imports_expr(stmt->var_init, out);
            break;
        case Stmt::Kind::TypeDecl:
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}

void ModuleLoader::collect_imports_expr(ExprPtr expr, std::vector<std::vector<std::string>>& out) const {
    if (!expr) return;
    switch (expr->kind) {
        case Expr::Kind::Block:
            for (const auto& st : expr->statements) {
                collect_imports(st, out);
            }
            collect_imports_expr(expr->result_expr, out);
            break;
        case Expr::Kind::Conditional:
            collect_imports_expr(expr->condition, out);
            collect_imports_expr(expr->true_expr, out);
            collect_imports_expr(expr->false_expr, out);
            break;
        case Expr::Kind::Binary:
            collect_imports_expr(expr->left, out);
            collect_imports_expr(expr->right, out);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
            collect_imports_expr(expr->operand, out);
            break;
        case Expr::Kind::Call:
            collect_imports_expr(expr->operand, out);
            for (const auto& rec : expr->receivers) collect_imports_expr(rec, out);
            for (const auto& arg : expr->args) collect_imports_expr(arg, out);
            break;
        case Expr::Kind::Index:
            collect_imports_expr(expr->operand, out);
            if (!expr->args.empty()) collect_imports_expr(expr->args[0], out);
            break;
        case Expr::Kind::Member:
            collect_imports_expr(expr->operand, out);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) collect_imports_expr(elem, out);
            break;
        case Expr::Kind::Assignment:
            collect_imports_expr(expr->left, out);
            collect_imports_expr(expr->right, out);
            break;
        case Expr::Kind::Range:
            collect_imports_expr(expr->left, out);
            collect_imports_expr(expr->right, out);
            break;
        case Expr::Kind::Iteration:
            collect_imports_expr(loop_subject(expr), out);
            collect_imports_expr(loop_body(expr), out);
            break;
        case Expr::Kind::Repeat:
            collect_imports_expr(loop_subject(expr), out);
            collect_imports_expr(loop_body(expr), out);
            break;
        case Expr::Kind::Resource:
        case Expr::Kind::Process:
        case Expr::Kind::Identifier:
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
            break;
    }
}

bool ModuleLoader::resolve_module_path(const std::vector<std::string>& import_path,
                                       const std::string& current_file,
                                       std::string& out_path) const {
    std::string relative = join_import_path(import_path) + ".vx";
    return try_resolve_relative_path(relative, current_file, project_root, out_path);
}

Module ModuleLoader::parse_module_file(const std::string& path) const {
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

} // namespace vexel
