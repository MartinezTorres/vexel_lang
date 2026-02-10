#include "vexel_backend.h"

#include "backend_registry.h"
#include "expr_access.h"
#include "io_utils.h"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace vexel {

namespace {

class LoweredVexelPrinter {
public:
    std::string render(const Module& mod, const std::string& source_path) {
        std::ostringstream out;
        out << "// Lowered Vexel module: " << source_path << "\n";
        for (const auto& stmt : mod.top_level) {
            append_stmt(out, stmt, 0);
        }
        return out.str();
    }

private:
    static std::string indent(int level) {
        return std::string(static_cast<size_t>(level) * 4u, ' ');
    }

    static std::string join_path(const std::vector<std::string>& parts) {
        std::ostringstream out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) out << "::";
            out << parts[i];
        }
        return out.str();
    }

    static int precedence(const ExprPtr& expr) {
        if (!expr) return 0;
        switch (expr->kind) {
            case Expr::Kind::Assignment:
                return 1;
            case Expr::Kind::Conditional:
                return 2;
            case Expr::Kind::Binary:
                if (expr->op == "||") return 3;
                if (expr->op == "&&") return 4;
                if (expr->op == "|") return 5;
                if (expr->op == "^") return 6;
                if (expr->op == "&") return 7;
                if (expr->op == "==" || expr->op == "!=") return 8;
                if (expr->op == "<" || expr->op == ">" || expr->op == "<=" || expr->op == ">=") return 9;
                if (expr->op == "<<" || expr->op == ">>") return 10;
                if (expr->op == "+" || expr->op == "-") return 11;
                if (expr->op == "*" || expr->op == "/" || expr->op == "%") return 12;
                return 12;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
                return 13;
            case Expr::Kind::Call:
            case Expr::Kind::Index:
            case Expr::Kind::Member:
                return 14;
            default:
                return 15;
        }
    }

    static bool is_right_associative(const ExprPtr& expr) {
        return expr && (expr->kind == Expr::Kind::Assignment || expr->kind == Expr::Kind::Conditional);
    }

    static std::string format_float(double value) {
        std::ostringstream out;
        out << std::setprecision(17) << value;
        return out.str();
    }

    std::string format_type(const TypePtr& type) {
        if (!type) return "#T";
        switch (type->kind) {
            case Type::Kind::Primitive:
                return "#" + primitive_name(type->primitive);
            case Type::Kind::Named:
                return "#" + type->type_name;
            case Type::Kind::TypeVar:
                return "#" + type->var_name;
            case Type::Kind::Array: {
                std::string size = "...";
                if (type->array_size) {
                    size = format_expr(type->array_size, 0, 0);
                }
                return format_type(type->element_type) + "[" + size + "]";
            }
        }
        return "#T";
    }

    std::string format_param(const Parameter& param) {
        std::ostringstream out;
        if (param.is_expression_param) {
            out << "$";
        }
        out << param.name;
        if (param.type) {
            out << ": " << format_type(param.type);
        }
        return out.str();
    }

    std::string format_function_signature(const StmtPtr& stmt) {
        std::ostringstream out;
        out << "&";
        if (stmt->is_external) {
            out << "!";
        } else if (stmt->is_exported) {
            out << "^";
        }

        if (!stmt->ref_params.empty()) {
            out << "(";
            for (size_t i = 0; i < stmt->ref_params.size(); ++i) {
                if (i > 0) out << ", ";
                out << stmt->ref_params[i];
            }
            out << ")";
        }

        if (!stmt->type_namespace.empty()) {
            out << "#" << stmt->type_namespace << "::";
        }
        out << stmt->func_name;

        out << "(";
        bool first = true;
        for (const auto& param : stmt->params) {
            if (!first) out << ", ";
            first = false;
            out << format_param(param);
        }
        out << ")";

        if (!stmt->return_types.empty()) {
            out << " -> (";
            for (size_t i = 0; i < stmt->return_types.size(); ++i) {
                if (i > 0) out << ", ";
                out << format_type(stmt->return_types[i]);
            }
            out << ")";
        } else if (stmt->return_type) {
            out << " -> " << format_type(stmt->return_type);
        }

        return out.str();
    }

    void append_stmt(std::ostringstream& out, const StmtPtr& stmt, int level) {
        if (!stmt) return;

        switch (stmt->kind) {
            case Stmt::Kind::Expr:
                out << indent(level) << format_expr(stmt->expr, 0, level) << ";\n";
                return;
            case Stmt::Kind::Return:
                if (stmt->return_expr) {
                    out << indent(level) << "-> " << format_expr(stmt->return_expr, 0, level) << ";\n";
                } else {
                    out << indent(level) << "->;\n";
                }
                return;
            case Stmt::Kind::Break:
                out << indent(level) << "->|;\n";
                return;
            case Stmt::Kind::Continue:
                out << indent(level) << "->>;\n";
                return;
            case Stmt::Kind::VarDecl:
                out << indent(level);
                if (stmt->is_exported) {
                    out << "^";
                }
                out << stmt->var_name;
                if (stmt->var_type) {
                    out << ": " << format_type(stmt->var_type);
                }
                if (stmt->var_init) {
                    out << " = " << format_expr(stmt->var_init, 0, level);
                }
                out << ";\n";
                return;
            case Stmt::Kind::TypeDecl:
                out << indent(level) << "#" << stmt->type_decl_name << "(";
                for (size_t i = 0; i < stmt->fields.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << stmt->fields[i].name;
                    if (stmt->fields[i].type) {
                        out << ": " << format_type(stmt->fields[i].type);
                    }
                }
                out << ");\n";
                return;
            case Stmt::Kind::Import:
                out << indent(level) << "::" << join_path(stmt->import_path) << ";\n";
                return;
            case Stmt::Kind::ConditionalStmt:
                out << indent(level) << format_expr(stmt->condition, 0, level) << " ? \n";
                append_stmt(out, stmt->true_stmt, level + 1);
                return;
            case Stmt::Kind::FuncDecl:
                out << indent(level) << format_function_signature(stmt);
                if (stmt->is_external || !stmt->body) {
                    out << ";\n";
                    return;
                }
                out << " {\n";
                append_function_body(out, stmt->body, level + 1);
                out << indent(level) << "}\n";
                return;
        }
    }

    void append_function_body(std::ostringstream& out, const ExprPtr& body, int level) {
        if (!body) return;

        if (body->kind == Expr::Kind::Block) {
            for (const auto& st : body->statements) {
                append_stmt(out, st, level);
            }
            if (body->result_expr) {
                out << indent(level) << format_expr(body->result_expr, 0, level) << "\n";
            }
            return;
        }

        out << indent(level) << format_expr(body, 0, level) << "\n";
    }

    std::string format_expr(const ExprPtr& expr, int parent_prec, int level) {
        if (!expr) return "";

        const int my_prec = precedence(expr);
        const bool need_parens = my_prec < parent_prec;
        std::ostringstream out;

        if (need_parens) out << "(";

        switch (expr->kind) {
            case Expr::Kind::IntLiteral:
                if (!expr->raw_literal.empty()) {
                    out << expr->raw_literal;
                } else {
                    out << expr->uint_val;
                }
                break;
            case Expr::Kind::FloatLiteral:
                if (!expr->raw_literal.empty()) {
                    out << expr->raw_literal;
                } else {
                    out << format_float(expr->float_val);
                }
                break;
            case Expr::Kind::StringLiteral:
                out << '"' << expr->string_val << '"';
                break;
            case Expr::Kind::CharLiteral:
                out << '\'' << static_cast<char>(expr->uint_val & 0xFFu) << '\'';
                break;
            case Expr::Kind::Identifier:
                out << expr->name;
                break;
            case Expr::Kind::Binary: {
                out << format_expr(expr->left, my_prec, level);
                out << " " << expr->op << " ";
                int rhs_prec = my_prec + (is_right_associative(expr) ? 0 : 1);
                out << format_expr(expr->right, rhs_prec, level);
                break;
            }
            case Expr::Kind::Unary:
                out << expr->op << format_expr(expr->operand, my_prec, level);
                break;
            case Expr::Kind::Call: {
                out << format_expr(expr->operand, my_prec, level) << "(";
                bool first = true;
                for (const auto& arg : expr->args) {
                    if (!first) out << ", ";
                    first = false;
                    out << format_expr(arg, 0, level);
                }
                out << ")";
                break;
            }
            case Expr::Kind::Index:
                out << format_expr(expr->operand, my_prec, level) << "[";
                if (!expr->args.empty()) {
                    out << format_expr(expr->args[0], 0, level);
                }
                out << "]";
                break;
            case Expr::Kind::Member:
                out << format_expr(expr->operand, my_prec, level) << "." << expr->name;
                break;
            case Expr::Kind::ArrayLiteral:
                out << "[";
                for (size_t i = 0; i < expr->elements.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << format_expr(expr->elements[i], 0, level);
                }
                out << "]";
                break;
            case Expr::Kind::TupleLiteral:
                out << "(";
                for (size_t i = 0; i < expr->elements.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << format_expr(expr->elements[i], 0, level);
                }
                out << ")";
                break;
            case Expr::Kind::Block: {
                out << "{\n";
                for (const auto& st : expr->statements) {
                    append_stmt(out, st, level + 1);
                }
                if (expr->result_expr) {
                    out << indent(level + 1) << format_expr(expr->result_expr, 0, level + 1) << "\n";
                }
                out << indent(level) << "}";
                break;
            }
            case Expr::Kind::Conditional:
                out << format_expr(expr->condition, my_prec, level)
                    << " ? "
                    << format_expr(expr->true_expr, my_prec, level)
                    << " : "
                    << format_expr(expr->false_expr, my_prec, level);
                break;
            case Expr::Kind::Cast:
                out << "( " << format_type(expr->target_type) << " ) "
                    << format_expr(expr->operand, my_prec, level);
                break;
            case Expr::Kind::Assignment:
                out << format_expr(expr->left, my_prec, level)
                    << " = "
                    << format_expr(expr->right, my_prec, level);
                break;
            case Expr::Kind::Range:
                out << format_expr(expr->left, my_prec, level)
                    << ".."
                    << format_expr(expr->right, my_prec, level);
                break;
            case Expr::Kind::Length:
                out << "|" << format_expr(expr->operand, 0, level) << "|";
                break;
            case Expr::Kind::Iteration:
                out << format_expr(loop_subject(expr), 0, level)
                    << (expr->is_sorted_iteration ? "@@" : "@")
                    << format_expr(loop_body(expr), my_prec, level + 1);
                break;
            case Expr::Kind::Repeat:
                out << format_expr(loop_subject(expr), 0, level)
                    << "@"
                    << format_expr(loop_body(expr), my_prec, level + 1);
                break;
            case Expr::Kind::Resource:
                out << "::" << join_path(expr->resource_path);
                break;
            case Expr::Kind::Process:
                out << "::\"" << expr->process_command << "\"";
                break;
        }

        if (need_parens) out << ")";
        return out.str();
    }
};

static bool parse_vexel_backend_option(int, char**, int&, Compiler::Options&, std::string&) {
    return false;
}

static void print_vexel_backend_usage(std::ostream& os) {
    os << "  (none)\n";
}

static void emit_vexel_backend(const BackendInput& input) {
    LoweredVexelPrinter printer;
    const std::string text = printer.render(*input.program.module, input.options.input_file);

    std::filesystem::path output_path = input.outputs.dir / (input.outputs.stem + ".vx");
    write_text_file_or_throw(output_path.string(), text);

    if (input.options.verbose) {
        std::cout << "Writing lowered Vexel: " << output_path << std::endl;
    }
    std::cout << text;
}

static BackendAnalysisRequirements vexel_analysis_requirements(const Compiler::Options&,
                                                               std::string&) {
    BackendAnalysisRequirements req;
    req.required_passes = kAllAnalysisPasses;
    req.default_entry_reentrancy = 'R';
    req.default_exit_reentrancy = 'R';
    return req;
}

} // namespace

void register_backend_vexel() {
    Backend backend;
    backend.info.name = "vexel";
    backend.info.description = "Lowered Vexel snapshot backend";
    backend.info.version = "v0.1.0";
    backend.emit = emit_vexel_backend;
    backend.analysis_requirements = vexel_analysis_requirements;
    backend.parse_option = parse_vexel_backend_option;
    backend.print_usage = print_vexel_backend_usage;
    (void)register_backend(backend);
}

} // namespace vexel
