#include "lowerer.h"
#include "expr_access.h"
#include "typechecker.h"

#include <functional>

namespace vexel {

namespace {
ExprPtr wrap_stmt_block(ExprPtr expr) {
    if (!expr || expr->kind == Expr::Kind::Block) return expr;
    std::vector<StmtPtr> stmts;
    stmts.push_back(Stmt::make_expr(expr, expr->location));
    return Expr::make_block(stmts, nullptr, expr->location);
}

bool exact_dim_expr_u64(const ExprPtr& size_expr, uint64_t& out) {
    if (!size_expr || size_expr->kind != Expr::Kind::IntLiteral) return false;
    if (size_expr->has_exact_int_val) {
        if (!size_expr->exact_int_val.fits_u64()) return false;
        out = size_expr->exact_int_val.to_u64();
        return true;
    }
    out = size_expr->uint_val;
    return true;
}

bool collect_shape_dims(const TypePtr& type, std::vector<uint64_t>& dims) {
    dims.clear();
    TypePtr current = type;
    while (current) {
        if (current->kind == Type::Kind::Vector) {
            uint64_t n = 0;
            if (!exact_dim_expr_u64(current->array_size, n)) return false;
            dims.push_back(n);
            current = current->element_type;
            continue;
        }
        if (current->kind == Type::Kind::Matrix) {
            uint64_t rows = 0;
            uint64_t cols = 0;
            if (!exact_dim_expr_u64(current->array_size, rows) || !exact_dim_expr_u64(current->matrix_cols, cols)) {
                return false;
            }
            dims.push_back(rows);
            dims.push_back(cols);
            current = current->element_type;
            continue;
        }
        if (current->kind == Type::Kind::Array) {
            uint64_t n = 0;
            if (!exact_dim_expr_u64(current->array_size, n)) return false;
            dims.push_back(n);
            current = current->element_type;
            continue;
        }
        break;
    }
    return true;
}

TypePtr scalar_element_type(TypePtr type) {
    TypePtr current = type;
    while (current) {
        if (current->kind == Type::Kind::Array || current->kind == Type::Kind::Vector || current->kind == Type::Kind::Matrix) {
            current = current->element_type;
            continue;
        }
        return current;
    }
    return nullptr;
}

ExprPtr uint_lit(uint64_t value, const SourceLocation& loc) {
    return Expr::make_int_exact(APInt(value), true, loc, std::to_string(value));
}

ExprPtr clone_expr_tree(const ExprPtr& expr) {
    if (!expr) return nullptr;
    ExprPtr cloned = std::make_shared<Expr>(*expr);
    cloned->left = clone_expr_tree(expr->left);
    cloned->right = clone_expr_tree(expr->right);
    cloned->operand = clone_expr_tree(expr->operand);
    cloned->condition = clone_expr_tree(expr->condition);
    cloned->true_expr = clone_expr_tree(expr->true_expr);
    cloned->false_expr = clone_expr_tree(expr->false_expr);
    cloned->args.clear();
    for (const auto& arg : expr->args) cloned->args.push_back(clone_expr_tree(arg));
    cloned->receivers.clear();
    for (const auto& rec : expr->receivers) cloned->receivers.push_back(clone_expr_tree(rec));
    cloned->elements.clear();
    for (const auto& elem : expr->elements) cloned->elements.push_back(clone_expr_tree(elem));
    return cloned;
}

ExprPtr index_chain(ExprPtr base, const std::vector<uint64_t>& indices, const SourceLocation& loc) {
    ExprPtr current = clone_expr_tree(base);
    for (uint64_t idx : indices) {
        current = Expr::make_index(current, uint_lit(idx, loc), loc);
    }
    return current;
}

ExprPtr sum_chain(const std::vector<ExprPtr>& terms, const SourceLocation& loc) {
    if (terms.empty()) return Expr::make_int(0, loc);
    ExprPtr current = terms.front();
    for (size_t i = 1; i < terms.size(); ++i) {
        current = Expr::make_binary("+", current, terms[i], loc);
    }
    return current;
}

ExprPtr eq_chain(const std::vector<ExprPtr>& terms, const SourceLocation& loc, bool negate) {
    if (terms.empty()) {
        return Expr::make_int(negate ? 0 : 1, loc);
    }
    ExprPtr current = terms.front();
    for (size_t i = 1; i < terms.size(); ++i) {
        current = Expr::make_binary(negate ? "||" : "&&", current, terms[i], loc);
    }
    return current;
}

void collect_flat_indices_recursive(const std::vector<uint64_t>& dims,
                                    size_t depth,
                                    std::vector<uint64_t>& current,
                                    std::vector<std::vector<uint64_t>>& out) {
    if (depth == dims.size()) {
        out.push_back(current);
        return;
    }
    for (uint64_t i = 0; i < dims[depth]; ++i) {
        current.push_back(i);
        collect_flat_indices_recursive(dims, depth + 1, current, out);
        current.pop_back();
    }
}

std::vector<std::vector<uint64_t>> collect_flat_indices(const std::vector<uint64_t>& dims) {
    std::vector<std::vector<uint64_t>> out;
    std::vector<uint64_t> current;
    collect_flat_indices_recursive(dims, 0, current, out);
    return out;
}

ExprPtr lex_compare_chain(ExprPtr left,
                          ExprPtr right,
                          const std::vector<uint64_t>& dims,
                          const std::string& op,
                          const SourceLocation& loc) {
    std::vector<std::vector<uint64_t>> indices = collect_flat_indices(dims);
    if (indices.empty()) {
        return Expr::make_int(0, loc);
    }

    const std::string strict_op = (op == ">" || op == ">=") ? ">" : "<";
    std::function<ExprPtr(size_t)> build = [&](size_t pos) -> ExprPtr {
        ExprPtr lhs = index_chain(left, indices[pos], loc);
        ExprPtr rhs = index_chain(right, indices[pos], loc);
        if (pos + 1 == indices.size()) {
            return Expr::make_binary(op, lhs, rhs, loc);
        }
        ExprPtr strict_cmp = Expr::make_binary(strict_op, clone_expr_tree(lhs), clone_expr_tree(rhs), loc);
        ExprPtr equal_cmp = Expr::make_binary("==", lhs, rhs, loc);
        ExprPtr tail = build(pos + 1);
        return Expr::make_binary("||",
                                 strict_cmp,
                                 Expr::make_binary("&&", equal_cmp, tail, loc),
                                 loc);
    };
    return build(0);
}

ExprPtr build_shape_literal(const std::vector<uint64_t>& dims,
                           size_t depth,
                           std::vector<uint64_t>& indices,
                           const std::function<ExprPtr(const std::vector<uint64_t>&)>& scalar_builder,
                           const SourceLocation& loc) {
    if (depth == dims.size()) {
        return scalar_builder(indices);
    }
    std::vector<ExprPtr> elements;
    elements.reserve(static_cast<size_t>(dims[depth]));
    for (uint64_t i = 0; i < dims[depth]; ++i) {
        indices.push_back(i);
        elements.push_back(build_shape_literal(dims, depth + 1, indices, scalar_builder, loc));
        indices.pop_back();
    }
    return Expr::make_array(elements, loc);
}

} // namespace

Lowerer::Lowerer(TypeChecker* checker)
    : checker(checker) {}

TypePtr Lowerer::lower_type(TypePtr type) {
    if (!type) return nullptr;
    switch (type->kind) {
        case Type::Kind::Array:
            return Type::make_array(lower_type(type->element_type), type->array_size, type->location);
        case Type::Kind::Vector:
        case Type::Kind::Matrix:
            return lower_shape_type_to_array(type);
        default:
            return type;
    }
}

void Lowerer::run(Module& mod) {
    // Invariant: lowering only simplifies expressions; it must not change inferred types.
    for (auto& stmt : mod.top_level) {
        lower_stmt(stmt);
    }
}

void Lowerer::lower_stmt(StmtPtr stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            if (stmt->body) {
                stmt->body = lower_expr(stmt->body);
            }
            for (auto& param : stmt->params) {
                param.type = lower_type(param.type);
            }
            for (auto& ref_type : stmt->ref_param_types) {
                ref_type = lower_type(ref_type);
            }
            stmt->return_type = lower_type(stmt->return_type);
            for (auto& rt : stmt->return_types) {
                rt = lower_type(rt);
            }
            break;
        case Stmt::Kind::VarDecl:
            if (stmt->var_init) {
                stmt->var_init = lower_expr(stmt->var_init);
            }
            stmt->var_type = lower_type(stmt->var_type);
            break;
        case Stmt::Kind::Expr:
            stmt->expr = lower_expr(stmt->expr);
            break;
        case Stmt::Kind::Return:
            stmt->return_expr = lower_expr(stmt->return_expr);
            break;
        case Stmt::Kind::ConditionalStmt:
            stmt->condition = lower_expr(stmt->condition);
            lower_stmt(stmt->true_stmt);
            break;
        case Stmt::Kind::TypeDecl:
            for (auto& field : stmt->fields) {
                field.type = lower_type(field.type);
            }
            break;
        case Stmt::Kind::Import:
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
    }
}

ExprPtr Lowerer::lower_expr(ExprPtr expr) {
    if (!expr) return nullptr;

    TypePtr original_type = expr->type;
    TypePtr original_target_type = expr->target_type;
    TypePtr original_declared_var_type = expr->declared_var_type;

    switch (expr->kind) {
        case Expr::Kind::Binary: {
            TypePtr orig_left_type = expr->left ? expr->left->type : nullptr;
            TypePtr orig_right_type = expr->right ? expr->right->type : nullptr;
            expr->left = lower_expr(expr->left);
            expr->right = lower_expr(expr->right);

            if ((orig_left_type && is_vector_or_matrix_type(orig_left_type)) ||
                (orig_right_type && is_vector_or_matrix_type(orig_right_type)) ||
                (original_type && is_vector_or_matrix_type(original_type))) {
                std::vector<uint64_t> lhs_shape;
                std::vector<uint64_t> rhs_shape;
                collect_shape_dims(orig_left_type, lhs_shape);
                collect_shape_dims(orig_right_type, rhs_shape);
                auto left_scalar = scalar_element_type(orig_left_type);
                auto right_scalar = scalar_element_type(orig_right_type);
                auto left_expr = expr->left;
                auto right_expr = expr->right;
                auto loc = expr->location;

                if (expr->op == "+" || expr->op == "-") {
                    auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                        return Expr::make_binary(expr->op, index_chain(left_expr, idxs, loc), index_chain(right_expr, idxs, loc), loc);
                    };
                    std::vector<uint64_t> idxs;
                    expr = build_shape_literal(lhs_shape, 0, idxs, scalar_builder, loc);
                } else if (expr->op == "*") {
                    bool lhs_vm = orig_left_type && is_vector_or_matrix_type(orig_left_type);
                    bool rhs_vm = orig_right_type && is_vector_or_matrix_type(orig_right_type);
                    if (lhs_vm && rhs_vm) {
                        if (orig_left_type->kind == Type::Kind::Vector && orig_right_type->kind == Type::Kind::Vector) {
                            std::vector<ExprPtr> terms;
                            terms.reserve(lhs_shape[0]);
                            for (uint64_t i = 0; i < lhs_shape[0]; ++i) {
                                terms.push_back(Expr::make_binary("*",
                                                                  index_chain(left_expr, {i}, loc),
                                                                  index_chain(right_expr, {i}, loc),
                                                                  loc));
                            }
                            expr = sum_chain(terms, loc);
                        } else if (orig_left_type->kind == Type::Kind::Matrix && orig_right_type->kind == Type::Kind::Vector) {
                            auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                                uint64_t r = idxs[0];
                                std::vector<ExprPtr> terms;
                                terms.reserve(lhs_shape[1]);
                                for (uint64_t k = 0; k < lhs_shape[1]; ++k) {
                                    terms.push_back(Expr::make_binary("*",
                                                                      index_chain(left_expr, {r, k}, loc),
                                                                      index_chain(right_expr, {k}, loc),
                                                                      loc));
                                }
                                return sum_chain(terms, loc);
                            };
                            std::vector<uint64_t> idxs;
                            expr = build_shape_literal({lhs_shape[0]}, 0, idxs, scalar_builder, loc);
                        } else if (orig_left_type->kind == Type::Kind::Vector && orig_right_type->kind == Type::Kind::Matrix) {
                            auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                                uint64_t c = idxs[0];
                                std::vector<ExprPtr> terms;
                                terms.reserve(lhs_shape[0]);
                                for (uint64_t k = 0; k < lhs_shape[0]; ++k) {
                                    terms.push_back(Expr::make_binary("*",
                                                                      index_chain(left_expr, {k}, loc),
                                                                      index_chain(right_expr, {k, c}, loc),
                                                                      loc));
                                }
                                return sum_chain(terms, loc);
                            };
                            std::vector<uint64_t> idxs;
                            expr = build_shape_literal({rhs_shape[1]}, 0, idxs, scalar_builder, loc);
                        } else if (orig_left_type->kind == Type::Kind::Matrix && orig_right_type->kind == Type::Kind::Matrix) {
                            auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                                uint64_t r = idxs[0];
                                uint64_t c = idxs[1];
                                std::vector<ExprPtr> terms;
                                terms.reserve(lhs_shape[1]);
                                for (uint64_t k = 0; k < lhs_shape[1]; ++k) {
                                    terms.push_back(Expr::make_binary("*",
                                                                      index_chain(left_expr, {r, k}, loc),
                                                                      index_chain(right_expr, {k, c}, loc),
                                                                      loc));
                                }
                                return sum_chain(terms, loc);
                            };
                            std::vector<uint64_t> idxs;
                            expr = build_shape_literal({lhs_shape[0], rhs_shape[1]}, 0, idxs, scalar_builder, loc);
                        }
                    } else if (lhs_vm) {
                        auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                            return Expr::make_binary("*", index_chain(left_expr, idxs, loc), clone_expr_tree(right_expr), loc);
                        };
                        std::vector<uint64_t> idxs;
                        expr = build_shape_literal(lhs_shape, 0, idxs, scalar_builder, loc);
                    } else if (rhs_vm) {
                        auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                            return Expr::make_binary("*", clone_expr_tree(left_expr), index_chain(right_expr, idxs, loc), loc);
                        };
                        std::vector<uint64_t> idxs;
                        expr = build_shape_literal(rhs_shape, 0, idxs, scalar_builder, loc);
                    }
                } else if (expr->op == "/") {
                    auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                        return Expr::make_binary("/", index_chain(left_expr, idxs, loc), clone_expr_tree(right_expr), loc);
                    };
                    std::vector<uint64_t> idxs;
                    expr = build_shape_literal(lhs_shape, 0, idxs, scalar_builder, loc);
                } else if (expr->op == "==" || expr->op == "!=") {
                    std::vector<ExprPtr> terms;
                    std::vector<uint64_t> idxs;
                    std::function<void(size_t)> walk = [&](size_t depth) {
                        if (depth == lhs_shape.size()) {
                            terms.push_back(Expr::make_binary("==",
                                                              index_chain(left_expr, idxs, loc),
                                                              index_chain(right_expr, idxs, loc),
                                                              loc));
                            return;
                        }
                        for (uint64_t i = 0; i < lhs_shape[depth]; ++i) {
                            idxs.push_back(i);
                            walk(depth + 1);
                            idxs.pop_back();
                        }
                    };
                    walk(0);
                    expr = eq_chain(terms, loc, expr->op == "!=");
                } else if (expr->op == "<" || expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
                    expr = lex_compare_chain(left_expr, right_expr, lhs_shape, expr->op, loc);
                }
                expr = lower_expr(expr);
                if (checker) {
                    expr->type = checker->recheck_lowered_expr(expr);
                } else {
                    expr->type = lower_type(original_type);
                }
                return expr;
            }
            break;
        }
        case Expr::Kind::Unary: {
            TypePtr orig_operand_type = expr->operand ? expr->operand->type : nullptr;
            expr->operand = lower_expr(expr->operand);
            if (orig_operand_type && is_vector_or_matrix_type(orig_operand_type)) {
                std::vector<uint64_t> shape;
                collect_shape_dims(orig_operand_type, shape);
                auto operand = expr->operand;
                auto loc = expr->location;
                if (expr->op == "+") {
                    expr = operand;
                } else if (expr->op == "-") {
                    auto scalar_builder = [&](const std::vector<uint64_t>& idxs) {
                        return Expr::make_unary("-", index_chain(operand, idxs, loc), loc);
                    };
                    std::vector<uint64_t> idxs;
                    expr = build_shape_literal(shape, 0, idxs, scalar_builder, loc);
                }
                expr = lower_expr(expr);
                if (checker) {
                    expr->type = checker->recheck_lowered_expr(expr);
                } else {
                    expr->type = lower_type(original_type);
                }
                return expr;
            }
            break;
        }
        case Expr::Kind::Cast:
            expr->operand = lower_expr(expr->operand);
            if ((original_target_type && is_vector_or_matrix_type(original_target_type)) ||
                (expr->operand && expr->operand->type && is_vector_or_matrix_type(expr->operand->type))) {
                ExprPtr lowered_operand = expr->operand;
                lowered_operand->type = lower_type(original_target_type);
                lowered_operand->declared_var_type = lower_type(lowered_operand->declared_var_type);
                return lowered_operand;
            }
            expr->target_type = lower_type(expr->target_type);
            break;
        case Expr::Kind::Length:
            expr->operand = lower_expr(expr->operand);
            break;
        case Expr::Kind::Call:
            for (auto& rec : expr->receivers) {
                rec = lower_expr(rec);
            }
            for (auto& arg : expr->args) {
                arg = lower_expr(arg);
            }
            break;
        case Expr::Kind::Index:
            expr->operand = lower_expr(expr->operand);
            if (!expr->args.empty()) {
                expr->args[0] = lower_expr(expr->args[0]);
            }
            break;
        case Expr::Kind::Member:
            expr->operand = lower_expr(expr->operand);
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (auto& elem : expr->elements) {
                elem = lower_expr(elem);
            }
            break;
        case Expr::Kind::Block:
            for (auto& st : expr->statements) {
                lower_stmt(st);
            }
            expr->result_expr = lower_expr(expr->result_expr);
            break;
        case Expr::Kind::Conditional: {
            expr->condition = lower_expr(expr->condition);
            expr->true_expr = lower_expr(expr->true_expr);
            expr->false_expr = lower_expr(expr->false_expr);
            break;
        }
        case Expr::Kind::Assignment:
            expr->left = lower_expr(expr->left);
            expr->right = lower_expr(expr->right);
            break;
        case Expr::Kind::Range:
            expr->left = lower_expr(expr->left);
            expr->right = lower_expr(expr->right);
            break;
        case Expr::Kind::Iteration:
            loop_subject_ref(expr) = lower_expr(loop_subject(expr));
            loop_body_ref(expr) = lower_expr(loop_body(expr));
            loop_body_ref(expr) = wrap_stmt_block(loop_body(expr));
            break;
        case Expr::Kind::Repeat:
            loop_subject_ref(expr) = lower_expr(loop_subject(expr));
            loop_body_ref(expr) = lower_expr(loop_body(expr));
            loop_body_ref(expr) = wrap_stmt_block(loop_body(expr));
            break;
        default:
            break;
    }

    expr->type = lower_type(original_type);
    expr->target_type = lower_type(original_target_type);
    expr->declared_var_type = lower_type(original_declared_var_type);
    return expr;
}

} // namespace vexel
