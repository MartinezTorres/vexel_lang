#include "lowered_printer.h"
#include "constants.h"
#include "expr_access.h"
#include <sstream>

namespace vexel {

namespace {

std::string indent(int level) {
    return std::string(static_cast<size_t>(level) * 4, ' ');
}

std::string render_annotations(const std::vector<Annotation>& anns) {
    if (anns.empty()) return "";
    std::ostringstream os;
    for (size_t i = 0; i < anns.size(); ++i) {
        const auto& ann = anns[i];
        os << "[[" << ann.name;
        if (!ann.args.empty()) {
            os << "(";
            for (size_t j = 0; j < ann.args.size(); ++j) {
                if (j > 0) os << ", ";
                os << ann.args[j];
            }
            os << ")";
        }
        os << "]]";
        if (i + 1 < anns.size()) os << " ";
    }
    os << " ";
    return os.str();
}

std::string render_type(TypePtr type);
std::string render_expr(const ExprPtr& expr, int level, bool inline_ctx = false);
std::string render_stmt(const StmtPtr& stmt, int level);

std::string render_type(TypePtr type) {
    if (!type) return "#?";
    switch (type->kind) {
        case Type::Kind::Primitive:
            return "#" + primitive_name(type->primitive);
        case Type::Kind::Named:
            return "#" + type->type_name;
        case Type::Kind::TypeVar:
            return "#" + type->var_name;
        case Type::Kind::Array: {
            std::string elem = render_type(type->element_type);
            std::string size = type->array_size ? render_expr(type->array_size, 0, true) : "";
            return elem + "[" + size + "]";
        }
    }
    return "#?";
}

std::string render_expr(const ExprPtr& expr, int level, bool inline_ctx) {
    if (!expr) return "";
    std::ostringstream os;
    std::string ann = render_annotations(expr->annotations);
    auto wrap_ann = [&](const std::string& body) {
        return ann.empty() ? body : ann + body;
    };

    switch (expr->kind) {
        case Expr::Kind::IntLiteral:
            os << wrap_ann(expr->raw_literal.empty() ? std::to_string(expr->uint_val) : expr->raw_literal);
            break;
        case Expr::Kind::FloatLiteral:
            os << wrap_ann(expr->raw_literal.empty() ? std::to_string(expr->float_val) : expr->raw_literal);
            break;
        case Expr::Kind::StringLiteral:
            os << wrap_ann("\"" + expr->string_val + "\"");
            break;
        case Expr::Kind::CharLiteral:
            os << wrap_ann("'" + expr->raw_literal + "'");
            break;
        case Expr::Kind::Identifier:
            os << wrap_ann(expr->name);
            break;
        case Expr::Kind::Binary:
            os << wrap_ann(render_expr(expr->left, level, true) + " " + expr->op + " " + render_expr(expr->right, level, true));
            break;
        case Expr::Kind::Unary:
            os << wrap_ann(expr->op + render_expr(expr->operand, level, true));
            break;
        case Expr::Kind::Call: {
            std::string fn = render_expr(expr->operand, level, true);
            os << ann << fn << "(";
            for (size_t i = 0; i < expr->args.size(); ++i) {
                if (i > 0) os << ", ";
                os << render_expr(expr->args[i], level, true);
            }
            os << ")";
            break;
        }
        case Expr::Kind::Index:
            os << wrap_ann(render_expr(expr->operand, level, true) + "[" + render_expr(expr->args[0], level, true) + "]");
            break;
        case Expr::Kind::Member:
            os << wrap_ann(render_expr(expr->operand, level, true) + "." + expr->name);
            break;
        case Expr::Kind::ArrayLiteral: {
            os << ann << "[";
            for (size_t i = 0; i < expr->elements.size(); ++i) {
                if (i > 0) os << ", ";
                os << render_expr(expr->elements[i], level, true);
            }
            os << "]";
            break;
        }
        case Expr::Kind::TupleLiteral: {
            os << ann << "(";
            for (size_t i = 0; i < expr->elements.size(); ++i) {
                if (i > 0) os << ", ";
                os << render_expr(expr->elements[i], level, true);
            }
            os << ")";
            break;
        }
        case Expr::Kind::Block: {
            os << ann << "{\n";
            for (const auto& st : expr->statements) {
                os << render_stmt(st, level + 1);
            }
            if (expr->result_expr) {
                os << indent(level + 1) << render_expr(expr->result_expr, level + 1, true) << "\n";
            }
            os << indent(level) << "}";
            if (!inline_ctx) os << "\n";
            break;
        }
        case Expr::Kind::Conditional:
            os << wrap_ann(render_expr(expr->condition, level, true) + " ? " +
                           render_expr(expr->true_expr, level, true) + " : " +
                           render_expr(expr->false_expr, level, true));
            break;
        case Expr::Kind::Cast:
            os << wrap_ann("( " + render_type(expr->target_type) + " ) " + render_expr(expr->operand, level, true));
            break;
        case Expr::Kind::Assignment:
            os << wrap_ann(render_expr(expr->left, level, true) + " = " + render_expr(expr->right, level, true));
            break;
        case Expr::Kind::Range:
            os << wrap_ann(render_expr(expr->left, level, true) + ".." + render_expr(expr->right, level, true));
            break;
        case Expr::Kind::Length:
            os << wrap_ann("|" + render_expr(expr->operand, level, true) + "|");
            break;
        case Expr::Kind::Iteration: {
            std::string op = expr->is_sorted_iteration ? "@@" : "@";
            os << ann << render_expr(loop_subject(expr), level, true) << op
               << render_expr(loop_body(expr), level, true);
            break;
        }
        case Expr::Kind::Repeat:
            if (loop_body(expr) && loop_body(expr)->kind == Expr::Kind::Block) {
                os << wrap_ann(render_expr(loop_subject(expr), level, true) + "@" +
                               render_expr(loop_body(expr), level + 1, true));
            } else {
                os << wrap_ann(render_expr(loop_subject(expr), level, true) + "@{" +
                               render_expr(loop_body(expr), level + 1, true) + "}");
            }
            break;
        case Expr::Kind::Resource: {
            os << ann << "::";
            for (size_t i = 0; i < expr->resource_path.size(); ++i) {
                if (i > 0) os << "::";
                os << expr->resource_path[i];
            }
            break;
        }
        case Expr::Kind::Process:
            os << ann << "::\"" << expr->process_command << "\"";
            break;
    }
    return os.str();
}

std::string render_params(const std::vector<Parameter>& params) {
    std::ostringstream os;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) os << ", ";
        os << render_annotations(params[i].annotations);
        if (params[i].is_expression_param) os << "$";
        os << params[i].name;
        if (params[i].type) {
            os << ": " << render_type(params[i].type);
        }
    }
    return os.str();
}

std::string render_fields(const std::vector<Field>& fields) {
    std::ostringstream os;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) os << ", ";
        os << render_annotations(fields[i].annotations);
        os << fields[i].name;
        if (fields[i].type) {
            os << ": " << render_type(fields[i].type);
        }
    }
    return os.str();
}

std::string render_stmt(const StmtPtr& stmt, int level) {
    if (!stmt) return "";
    std::ostringstream os;
    os << indent(level) << render_annotations(stmt->annotations);

    switch (stmt->kind) {
        case Stmt::Kind::VarDecl: {
            if (stmt->is_mutable) os << "mut ";
            os << stmt->var_name;
            if (stmt->var_type) {
                os << ": " << render_type(stmt->var_type);
            }
            if (stmt->var_init) {
                os << " = " << render_expr(stmt->var_init, level, true);
            }
            os << ";\n";
            break;
        }
        case Stmt::Kind::FuncDecl: {
            std::string sigil = "&";
            if (stmt->is_external) sigil = "&!";
            else if (stmt->is_exported) sigil = "&^";
            os << sigil;
            if (!stmt->ref_params.empty()) {
                os << "(";
                for (size_t i = 0; i < stmt->ref_params.size(); ++i) {
                    if (i > 0) os << ", ";
                    os << stmt->ref_params[i];
                }
                os << ")";
            }
            if (!stmt->type_namespace.empty()) {
                os << "#" << stmt->type_namespace << "::";
            }
            os << stmt->func_name << "(" << render_params(stmt->params) << ")";
            if (!stmt->return_types.empty()) {
                os << " -> (";
                for (size_t i = 0; i < stmt->return_types.size(); ++i) {
                    if (i > 0) os << ", ";
                    os << render_type(stmt->return_types[i]);
                }
                os << ")";
            } else if (stmt->return_type) {
                os << " -> " << render_type(stmt->return_type);
            }
            if (stmt->body) {
                os << " " << render_expr(stmt->body, level, true) << "\n";
            } else {
                os << ";\n";
            }
            break;
        }
        case Stmt::Kind::TypeDecl: {
            os << "#" << stmt->type_decl_name << "(" << render_fields(stmt->fields) << ");\n";
            break;
        }
        case Stmt::Kind::Import: {
            os << "::";
            for (size_t i = 0; i < stmt->import_path.size(); ++i) {
                if (i > 0) os << "::";
                os << stmt->import_path[i];
            }
            os << ";\n";
            break;
        }
        case Stmt::Kind::Expr: {
            os << render_expr(stmt->expr, level, true) << ";\n";
            break;
        }
        case Stmt::Kind::Return: {
            os << "->";
            if (stmt->return_expr) {
                os << " " << render_expr(stmt->return_expr, level, true);
            }
            os << ";\n";
            break;
        }
        case Stmt::Kind::Break:
            os << "->|;\n";
            break;
        case Stmt::Kind::Continue:
            os << "->>;\n";
            break;
        case Stmt::Kind::ConditionalStmt: {
            os << render_expr(stmt->condition, level, true) << " ? ";
            // Render nested statement on the same line if short, otherwise new line
            std::string nested = render_stmt(stmt->true_stmt, level + 1);
            if (nested.find('\n') == std::string::npos) {
                os << nested;
            } else {
                os << "\n" << nested;
            }
            break;
        }
    }
    return os.str();
}

} // namespace

std::string print_lowered_module(const Module& mod) {
    std::ostringstream os;
    os << "// Lowered Vexel module: " << mod.name << "\n";
    for (const auto& stmt : mod.top_level) {
        os << render_stmt(stmt, 0);
    }
    return os.str();
}

}
