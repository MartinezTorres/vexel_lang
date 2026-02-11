#include "residualizer.h"

#include "expr_access.h"

namespace vexel {

namespace {

bool scalar_to_bool(const CTValue& value, bool& out) {
    if (std::holds_alternative<int64_t>(value)) {
        out = std::get<int64_t>(value) != 0;
        return true;
    }
    if (std::holds_alternative<uint64_t>(value)) {
        out = std::get<uint64_t>(value) != 0;
        return true;
    }
    if (std::holds_alternative<bool>(value)) {
        out = std::get<bool>(value);
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        out = std::get<double>(value) != 0.0;
        return true;
    }
    return false;
}

bool literal_to_bool(const ExprPtr& expr, bool& out) {
    if (!expr) return false;
    switch (expr->kind) {
        case Expr::Kind::IntLiteral:
            out = expr->uint_val != 0;
            return true;
        case Expr::Kind::FloatLiteral:
            out = expr->float_val != 0.0;
            return true;
        default:
            return false;
    }
}

bool is_literal_expr_kind(Expr::Kind kind) {
    switch (kind) {
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            return true;
        default:
            return false;
    }
}

} // namespace

bool Residualizer::run(Module& mod) {
    changed_ = false;
    rewrite_stmt_list(mod.top_level, true);
    return changed_;
}

StmtPtr Residualizer::rewrite_stmt(StmtPtr stmt, bool top_level) {
    if (!stmt) return nullptr;

    switch (stmt->kind) {
        case Stmt::Kind::FuncDecl:
            if (stmt->body) {
                stmt->body = rewrite_expr(stmt->body);
            }
            return stmt;

        case Stmt::Kind::VarDecl:
            if (stmt->var_init) {
                stmt->var_init = rewrite_expr(stmt->var_init);
            }
            return stmt;

        case Stmt::Kind::Expr:
            if (stmt->expr) {
                stmt->expr = rewrite_expr(stmt->expr);
            }
            return stmt;

        case Stmt::Kind::Return:
            if (stmt->return_expr) {
                stmt->return_expr = rewrite_expr(stmt->return_expr);
            }
            return stmt;

        case Stmt::Kind::ConditionalStmt: {
            const Expr* original_cond = stmt->condition.get();
            stmt->condition = rewrite_expr(stmt->condition);
            if (stmt->true_stmt) {
                stmt->true_stmt = rewrite_stmt(stmt->true_stmt, top_level);
            }
            auto cond = constexpr_condition(stmt->condition, original_cond);
            if (!cond.has_value()) {
                return stmt->true_stmt ? stmt : nullptr;
            }
            changed_ = true;
            if (!cond.value()) {
                return nullptr;
            }
            return stmt->true_stmt;
        }

        default:
            return stmt;
    }
}

ExprPtr Residualizer::rewrite_expr(ExprPtr expr, bool allow_fold) {
    if (!expr) return nullptr;

    if (allow_fold && can_fold_expr(expr) && !is_literal_expr_kind(expr->kind)) {
        auto value_it = facts_.constexpr_values.find(expr.get());
        if (value_it != facts_.constexpr_values.end()) {
            ExprPtr folded = ctvalue_to_expr(value_it->second, expr);
            if (folded) {
                changed_ = true;
                return folded;
            }
        }
    }

    switch (expr->kind) {
        case Expr::Kind::Conditional: {
            const Expr* original_cond = expr->condition.get();
            expr->condition = rewrite_expr(expr->condition);
            expr->true_expr = rewrite_expr(expr->true_expr);
            expr->false_expr = rewrite_expr(expr->false_expr);
            auto cond = constexpr_condition(expr->condition, original_cond);
            if (cond.has_value()) {
                changed_ = true;
                return cond.value() ? expr->true_expr : expr->false_expr;
            }
            return expr;
        }

        case Expr::Kind::Binary:
        case Expr::Kind::Assignment:
        case Expr::Kind::Range:
            expr->left = rewrite_expr(expr->left, expr->kind == Expr::Kind::Assignment ? false : true);
            expr->right = rewrite_expr(expr->right);
            return expr;

        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
        case Expr::Kind::Member:
            expr->operand = rewrite_expr(expr->operand);
            return expr;

        case Expr::Kind::Call:
            expr->operand = rewrite_expr(expr->operand, false);
            for (auto& rec : expr->receivers) {
                rec = rewrite_expr(rec, false);
            }
            for (auto& arg : expr->args) {
                arg = rewrite_expr(arg);
            }
            return expr;

        case Expr::Kind::Index:
            expr->operand = rewrite_expr(expr->operand, allow_fold);
            for (auto& arg : expr->args) {
                arg = rewrite_expr(arg, allow_fold);
            }
            return expr;

        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (auto& elem : expr->elements) {
                elem = rewrite_expr(elem);
            }
            return expr;

        case Expr::Kind::Block:
            rewrite_stmt_list(expr->statements, false);
            if (expr->result_expr) {
                expr->result_expr = rewrite_expr(expr->result_expr);
            }
            return expr;

        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
            loop_subject_ref(expr) = rewrite_expr(loop_subject(expr));
            loop_body_ref(expr) = rewrite_expr(loop_body(expr));
            return expr;

        default:
            return expr;
    }
}

void Residualizer::rewrite_stmt_list(std::vector<StmtPtr>& stmts, bool top_level) {
    std::vector<StmtPtr> rewritten;
    rewritten.reserve(stmts.size());

    bool terminated = false;
    for (auto& stmt : stmts) {
        if (terminated) {
            changed_ = true;
            continue;
        }

        StmtPtr next = rewrite_stmt(stmt, top_level);
        if (!next) {
            changed_ = true;
            continue;
        }

        rewritten.push_back(next);
        if (!top_level && is_terminal_stmt(next)) {
            terminated = true;
        }
    }

    if (rewritten.size() != stmts.size()) {
        changed_ = true;
    }
    stmts.swap(rewritten);
}

bool Residualizer::should_drop_expr_stmt(const ExprPtr& expr) const {
    if (!expr) return true;
    return is_pure_expr(expr);
}

bool Residualizer::is_pure_expr(const ExprPtr& expr) const {
    if (!expr) return true;

    switch (expr->kind) {
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::StringLiteral:
        case Expr::Kind::CharLiteral:
        case Expr::Kind::Identifier:
            return !expr->is_expr_param_ref;
        case Expr::Kind::Resource:
            return true;

        case Expr::Kind::Call:
        case Expr::Kind::Assignment:
        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
        case Expr::Kind::Process:
        case Expr::Kind::Block:
            return false;

        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
        case Expr::Kind::Member:
            return is_pure_expr(expr->operand);

        case Expr::Kind::Binary:
        case Expr::Kind::Range:
            return is_pure_expr(expr->left) && is_pure_expr(expr->right);

        case Expr::Kind::Index: {
            if (!is_pure_expr(expr->operand)) return false;
            for (const auto& arg : expr->args) {
                if (!is_pure_expr(arg)) return false;
            }
            return true;
        }

        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) {
                if (!is_pure_expr(elem)) return false;
            }
            return true;

        case Expr::Kind::Conditional:
            return is_pure_expr(expr->condition) &&
                   is_pure_expr(expr->true_expr) &&
                   is_pure_expr(expr->false_expr);

        default:
            return false;
    }
}

bool Residualizer::is_terminal_stmt(const StmtPtr& stmt) {
    if (!stmt) return false;
    return stmt->kind == Stmt::Kind::Return ||
           stmt->kind == Stmt::Kind::Break ||
           stmt->kind == Stmt::Kind::Continue;
}

void Residualizer::copy_expr_meta(const ExprPtr& from, const ExprPtr& to) {
    if (!from || !to) return;
    to->location = from->location;
    to->type = from->type;
    to->annotations = from->annotations;
    to->scope_instance_id = from->scope_instance_id;
}

ExprPtr Residualizer::ctvalue_to_expr(const CTValue& value, const ExprPtr& origin) const {
    ExprPtr result;
    if (std::holds_alternative<int64_t>(value)) {
        result = Expr::make_int(std::get<int64_t>(value), origin ? origin->location : SourceLocation());
    } else if (std::holds_alternative<uint64_t>(value)) {
        result = Expr::make_uint(std::get<uint64_t>(value), origin ? origin->location : SourceLocation());
    } else if (std::holds_alternative<bool>(value)) {
        result = Expr::make_uint(std::get<bool>(value) ? 1u : 0u,
                                 origin ? origin->location : SourceLocation());
    } else if (std::holds_alternative<double>(value)) {
        result = Expr::make_float(std::get<double>(value), origin ? origin->location : SourceLocation());
    } else if (std::holds_alternative<std::string>(value)) {
        result = Expr::make_string(std::get<std::string>(value), origin ? origin->location : SourceLocation());
    } else if (std::holds_alternative<std::shared_ptr<CTArray>>(value)) {
        auto array = std::get<std::shared_ptr<CTArray>>(value);
        if (!array) return nullptr;

        std::vector<ExprPtr> elems;
        elems.reserve(array->elements.size());
        for (const auto& elem : array->elements) {
            ExprPtr elem_expr = ctvalue_to_expr(elem, origin);
            if (!elem_expr) {
                return nullptr;
            }
            elems.push_back(elem_expr);
        }
        result = Expr::make_array(std::move(elems), origin ? origin->location : SourceLocation());
    } else {
        return nullptr;
    }

    copy_expr_meta(origin, result);
    return result;
}

std::optional<bool> Residualizer::constexpr_condition(const ExprPtr& cond, const Expr* original) const {
    if (original) {
        auto it = facts_.constexpr_conditions.find(original);
        if (it != facts_.constexpr_conditions.end()) {
            return it->second;
        }
    }

    if (cond) {
        auto it = facts_.constexpr_conditions.find(cond.get());
        if (it != facts_.constexpr_conditions.end()) {
            return it->second;
        }
    }

    if (cond) {
        auto value_it = facts_.constexpr_values.find(cond.get());
        if (value_it != facts_.constexpr_values.end()) {
            bool out = false;
            if (scalar_to_bool(value_it->second, out)) {
                return out;
            }
        }
    }

    bool literal = false;
    if (literal_to_bool(cond, literal)) {
        return literal;
    }
    return std::nullopt;
}

bool Residualizer::can_fold_expr(const ExprPtr& expr) const {
    if (!expr) return false;

    switch (expr->kind) {
        case Expr::Kind::Call: {
            if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
                return false;
            }
            return true;
        }

        default:
            return false;
    }
}

} // namespace vexel
