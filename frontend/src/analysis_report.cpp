#include "analysis_report.h"
#include "function_key.h"
#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace vexel {

namespace {

std::string format_key(const std::string& key) {
    std::string name;
    int scope = -1;
    split_reachability_key(key, name, scope);
    if (scope < 0) return name;
    return name + " [scope=" + std::to_string(scope) + "]";
}

std::string format_location(const SourceLocation& loc) {
    if (loc.filename.empty()) return "";
    std::ostringstream os;
    os << loc.filename << ":" << loc.line << ":" << loc.column;
    return os.str();
}

std::string mutability_label(VarMutability mut) {
    switch (mut) {
        case VarMutability::Mutable:
            return "mutable";
        case VarMutability::NonMutableRuntime:
            return "runtime-immutable";
        case VarMutability::Constexpr:
            return "constexpr";
    }
    return "unknown";
}

void collect_var_decls(const Module& mod, std::unordered_map<const Stmt*, std::string>& out) {
    std::function<void(StmtPtr)> visit_stmt;
    std::function<void(ExprPtr)> visit_expr;

    auto record = [&](const StmtPtr& stmt) {
        std::string name = stmt->var_name;
        if (stmt->scope_instance_id >= 0) {
            name += " [scope=" + std::to_string(stmt->scope_instance_id) + "]";
        }
        std::string loc = format_location(stmt->location);
        if (!loc.empty()) {
            name += " (" + loc + ")";
        }
        out.emplace(stmt.get(), std::move(name));
    };

    visit_expr = [&](ExprPtr expr) {
        if (!expr) return;
        switch (expr->kind) {
            case Expr::Kind::Block:
                for (const auto& st : expr->statements) visit_stmt(st);
                visit_expr(expr->result_expr);
                break;
            case Expr::Kind::Call:
                visit_expr(expr->operand);
                for (const auto& rec : expr->receivers) visit_expr(rec);
                for (const auto& arg : expr->args) visit_expr(arg);
                break;
            case Expr::Kind::Binary:
            case Expr::Kind::Assignment:
            case Expr::Kind::Range:
                visit_expr(expr->left);
                visit_expr(expr->right);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
            case Expr::Kind::Member:
                visit_expr(expr->operand);
                break;
            case Expr::Kind::Index:
                visit_expr(expr->operand);
                if (!expr->args.empty()) visit_expr(expr->args[0]);
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) visit_expr(elem);
                break;
            case Expr::Kind::Conditional:
                visit_expr(expr->condition);
                visit_expr(expr->true_expr);
                visit_expr(expr->false_expr);
                break;
            case Expr::Kind::Iteration:
            case Expr::Kind::Repeat:
                visit_expr(expr->left);
                visit_expr(expr->right);
                break;
            default:
                break;
        }
    };

    visit_stmt = [&](StmtPtr stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
            case Stmt::Kind::VarDecl:
                record(stmt);
                visit_expr(stmt->var_init);
                break;
            case Stmt::Kind::FuncDecl:
                if (!stmt->is_external) {
                    visit_expr(stmt->body);
                }
                break;
            case Stmt::Kind::Expr:
                visit_expr(stmt->expr);
                break;
            case Stmt::Kind::Return:
                visit_expr(stmt->return_expr);
                break;
            case Stmt::Kind::ConditionalStmt:
                visit_expr(stmt->condition);
                visit_stmt(stmt->true_stmt);
                break;
            default:
                break;
        }
    };

    for (const auto& stmt : mod.top_level) {
        visit_stmt(stmt);
    }
}

} // namespace

std::string format_analysis_report(const Module& mod, const AnalysisFacts& analysis,
                                   const OptimizationFacts* optimization) {
    std::ostringstream out;
    out << "# Vexel Analysis Report\n";
    if (!mod.name.empty()) {
        out << "Module: " << mod.name << "\n";
    }
    out << "\n";

    if (optimization) {
        out << "## Optimization Summary\n";
        out << "- Constexpr expressions: " << optimization->constexpr_values.size() << "\n";
        out << "- Constexpr inits: " << optimization->constexpr_inits.size() << "\n";
        out << "- Foldable functions: " << optimization->foldable_functions.size() << "\n";
        out << "- Constexpr conditions: " << optimization->constexpr_conditions.size() << "\n\n";
    }

    std::vector<std::string> reachable(analysis.reachable_functions.begin(),
                                       analysis.reachable_functions.end());
    std::sort(reachable.begin(), reachable.end());
    out << "## Reachable Functions\n";
    for (const auto& key : reachable) {
        out << "- " << format_key(key) << "\n";
    }
    out << "\n";

    std::vector<std::string> reent_keys;
    reent_keys.reserve(analysis.reentrancy_variants.size());
    for (const auto& pair : analysis.reentrancy_variants) {
        reent_keys.push_back(pair.first);
    }
    std::sort(reent_keys.begin(), reent_keys.end());
    out << "## Reentrancy Variants\n";
    for (const auto& key : reent_keys) {
        const auto& variants = analysis.reentrancy_variants.at(key);
        std::string tags;
        for (char v : variants) {
            if (!tags.empty()) tags += ",";
            tags.push_back(v);
        }
        out << "- " << format_key(key) << ": " << tags << "\n";
    }
    out << "\n";

    std::vector<std::string> ref_keys;
    ref_keys.reserve(analysis.ref_variants.size());
    for (const auto& pair : analysis.ref_variants) {
        ref_keys.push_back(pair.first);
    }
    std::sort(ref_keys.begin(), ref_keys.end());
    out << "## Ref Variants\n";
    for (const auto& func : ref_keys) {
        const auto& masks = analysis.ref_variants.at(func);
        std::vector<std::string> sorted_masks(masks.begin(), masks.end());
        std::sort(sorted_masks.begin(), sorted_masks.end());
        out << "- " << func << ": ";
        for (size_t i = 0; i < sorted_masks.size(); ++i) {
            if (i > 0) out << ", ";
            out << (sorted_masks[i].empty() ? "<default>" : sorted_masks[i]);
        }
        out << "\n";
    }
    out << "\n";

    std::unordered_map<const Stmt*, std::string> var_names;
    collect_var_decls(mod, var_names);
    std::vector<std::string> mut_lines;
    mut_lines.reserve(analysis.var_mutability.size());
    for (const auto& pair : analysis.var_mutability) {
        const Stmt* stmt = pair.first;
        auto name_it = var_names.find(stmt);
        std::string label = (name_it != var_names.end()) ? name_it->second : "<unknown>";
        mut_lines.push_back(label + " -> " + mutability_label(pair.second));
    }
    std::sort(mut_lines.begin(), mut_lines.end());
    out << "## Variable Mutability\n";
    for (const auto& line : mut_lines) {
        out << "- " << line << "\n";
    }
    out << "\n";

    std::vector<std::string> used_globals;
    used_globals.reserve(analysis.used_global_vars.size());
    for (const auto& stmt : analysis.used_global_vars) {
        auto it = var_names.find(stmt);
        if (it != var_names.end()) {
            used_globals.push_back(it->second);
        }
    }
    std::sort(used_globals.begin(), used_globals.end());
    out << "## Used Globals\n";
    for (const auto& name : used_globals) {
        out << "- " << name << "\n";
    }
    out << "\n";

    std::vector<std::string> used_types(analysis.used_type_names.begin(),
                                        analysis.used_type_names.end());
    std::sort(used_types.begin(), used_types.end());
    out << "## Used Types\n";
    for (const auto& name : used_types) {
        out << "- " << name << "\n";
    }

    return out.str();
}

} // namespace vexel
