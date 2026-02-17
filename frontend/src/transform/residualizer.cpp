#include "residualizer.h"

#include "constants.h"
#include "cte_value_utils.h"
#include "expr_access.h"

#include <algorithm>

namespace vexel {

namespace {

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

bool expr_structurally_equal(const ExprPtr& a, const ExprPtr& b) {
    if (a.get() == b.get()) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case Expr::Kind::IntLiteral:
        case Expr::Kind::CharLiteral:
            return a->uint_val == b->uint_val;
        case Expr::Kind::FloatLiteral:
            return a->float_val == b->float_val;
        case Expr::Kind::StringLiteral:
            return a->string_val == b->string_val;
        case Expr::Kind::Identifier:
            return a->name == b->name;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            if (a->elements.size() != b->elements.size()) return false;
            for (size_t i = 0; i < a->elements.size(); ++i) {
                if (!expr_structurally_equal(a->elements[i], b->elements[i])) return false;
            }
            return true;
        case Expr::Kind::Call:
            if (!expr_structurally_equal(a->operand, b->operand)) return false;
            if (a->args.size() != b->args.size()) return false;
            if (a->receivers.size() != b->receivers.size()) return false;
            for (size_t i = 0; i < a->args.size(); ++i) {
                if (!expr_structurally_equal(a->args[i], b->args[i])) return false;
            }
            for (size_t i = 0; i < a->receivers.size(); ++i) {
                if (!expr_structurally_equal(a->receivers[i], b->receivers[i])) return false;
            }
            return true;
        default:
            return false;
    }
}

} // namespace

bool Residualizer::run(Module& mod) {
    changed_ = false;
    rebuild_type_field_order(mod);
    if (mod.top_level_instance_ids.size() != mod.top_level.size()) {
        throw CompileError("Internal error: residualizer requires top-level instance IDs aligned with merged module",
                           mod.location);
    }

    std::vector<StmtPtr> rewritten;
    std::vector<int> rewritten_instance_ids;
    rewritten.reserve(mod.top_level.size());
    rewritten_instance_ids.reserve(mod.top_level_instance_ids.size());

    for (size_t i = 0; i < mod.top_level.size(); ++i) {
        current_instance_id_ = mod.top_level_instance_ids[i];
        StmtPtr next = rewrite_stmt(mod.top_level[i], true);
        if (!next) {
            changed_ = true;
            continue;
        }
        rewritten.push_back(next);
        rewritten_instance_ids.push_back(current_instance_id_);
    }

    if (rewritten.size() != mod.top_level.size()) {
        changed_ = true;
    }
    mod.top_level.swap(rewritten);
    mod.top_level_instance_ids.swap(rewritten_instance_ids);
    current_instance_id_ = -1;
    return changed_;
}

void Residualizer::rebuild_type_field_order(const Module& mod) {
    type_field_order_.clear();
    type_field_types_.clear();
    for (const auto& stmt : mod.top_level) {
        if (!stmt || stmt->kind != Stmt::Kind::TypeDecl) continue;
        std::vector<std::string> names;
        names.reserve(stmt->fields.size());
        std::unordered_map<std::string, TypePtr> field_types;
        field_types.reserve(stmt->fields.size());
        for (const auto& field : stmt->fields) {
            names.push_back(field.name);
            field_types[field.name] = field.type;
        }
        type_field_order_[stmt->type_decl_name] = std::move(names);
        type_field_types_[stmt->type_decl_name] = std::move(field_types);
    }
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
            if (!top_level && should_drop_expr_stmt(stmt->expr)) {
                changed_ = true;
                return nullptr;
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
        auto value_it = facts_.constexpr_values.find(expr_fact_key(current_instance_id_, expr.get()));
        if (value_it != facts_.constexpr_values.end()) {
            ExprPtr folded = ctvalue_to_expr(value_it->second, expr, expr ? expr->type : nullptr);
            if (folded) {
                if (expr_structurally_equal(expr, folded)) {
                    return expr;
                }
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
    to->annotations = from->annotations;
    to->scope_instance_id = from->scope_instance_id;
}

TypePtr Residualizer::expected_elem_type(TypePtr type) {
    if (!type || type->kind != Type::Kind::Array) return nullptr;
    return type->element_type;
}

ExprPtr Residualizer::ctvalue_to_expr(const CTValue& value, const ExprPtr& origin, TypePtr expected_type) const {
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
        TypePtr elem_expected = expected_elem_type(expected_type);
        for (const auto& elem : array->elements) {
            ExprPtr elem_expr = ctvalue_to_expr(elem, origin, elem_expected);
            if (!elem_expr) {
                return nullptr;
            }
            elems.push_back(elem_expr);
        }
        result = Expr::make_array(std::move(elems), origin ? origin->location : SourceLocation());
    } else if (std::holds_alternative<std::shared_ptr<CTComposite>>(value)) {
        auto comp = std::get<std::shared_ptr<CTComposite>>(value);
        if (!comp) return nullptr;

        std::vector<std::string> field_order;
        const bool is_tuple =
            !comp->type_name.empty() &&
            comp->type_name.rfind(TUPLE_TYPE_PREFIX, 0) == 0;

        if (is_tuple || comp->type_name.empty()) {
            std::vector<std::pair<int, std::string>> indexed_fields;
            indexed_fields.reserve(comp->fields.size());
            for (const auto& entry : comp->fields) {
                const std::string& name = entry.first;
                if (name.rfind(MANGLED_PREFIX, 0) != 0) continue;
                const std::string index_str = name.substr(std::char_traits<char>::length(MANGLED_PREFIX));
                if (index_str.empty()) continue;
                bool numeric = true;
                for (char c : index_str) {
                    if (c < '0' || c > '9') {
                        numeric = false;
                        break;
                    }
                }
                if (!numeric) continue;
                indexed_fields.emplace_back(std::stoi(index_str), name);
            }
            std::sort(indexed_fields.begin(), indexed_fields.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            field_order.reserve(indexed_fields.size());
            for (const auto& entry : indexed_fields) {
                field_order.push_back(entry.second);
            }
        } else {
            auto ordered_it = type_field_order_.find(comp->type_name);
            if (ordered_it != type_field_order_.end()) {
                field_order = ordered_it->second;
            } else {
                field_order.reserve(comp->fields.size());
                for (const auto& entry : comp->fields) {
                    field_order.push_back(entry.first);
                }
                std::sort(field_order.begin(), field_order.end());
            }
        }

        std::vector<ExprPtr> elems;
        elems.reserve(field_order.size());
        const auto field_types_it =
            comp->type_name.empty() ? type_field_types_.end() : type_field_types_.find(comp->type_name);
        for (const auto& name : field_order) {
            auto it = comp->fields.find(name);
            if (it == comp->fields.end()) {
                return nullptr;
            }
            TypePtr field_expected = nullptr;
            if (field_types_it != type_field_types_.end()) {
                auto fit = field_types_it->second.find(name);
                if (fit != field_types_it->second.end()) {
                    field_expected = fit->second;
                }
            }
            ExprPtr elem_expr = ctvalue_to_expr(it->second, origin, field_expected);
            if (!elem_expr) {
                return nullptr;
            }
            elems.push_back(elem_expr);
        }

        if (is_tuple || comp->type_name.empty()) {
            result = Expr::make_tuple(std::move(elems), origin ? origin->location : SourceLocation());
        } else {
            ExprPtr callee = Expr::make_identifier(comp->type_name, origin ? origin->location : SourceLocation());
            result = Expr::make_call(callee, std::move(elems), origin ? origin->location : SourceLocation());
        }
    } else {
        return nullptr;
    }

    copy_expr_meta(origin, result);
    if (expected_type) {
        result->type = expected_type;
    } else if (origin) {
        result->type = origin->type;
    }
    return result;
}

std::optional<bool> Residualizer::constexpr_condition(const ExprPtr& cond, const Expr* original) const {
    if (original) {
        auto it = facts_.constexpr_conditions.find(expr_fact_key(current_instance_id_, original));
        if (it != facts_.constexpr_conditions.end()) {
            return it->second;
        }
    }

    if (cond) {
        auto it = facts_.constexpr_conditions.find(expr_fact_key(current_instance_id_, cond.get()));
        if (it != facts_.constexpr_conditions.end()) {
            return it->second;
        }
    }

    if (cond) {
        auto value_it = facts_.constexpr_values.find(expr_fact_key(current_instance_id_, cond.get()));
        if (value_it != facts_.constexpr_values.end()) {
            bool out = false;
            if (cte_scalar_to_bool(value_it->second, out)) {
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
        case Expr::Kind::Assignment:
        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
        case Expr::Kind::Process:
            return false;
        case Expr::Kind::Call: {
            if (!expr->operand || expr->operand->kind != Expr::Kind::Identifier) {
                return false;
            }
            return true;
        }

        default:
            return true;
    }
}

} // namespace vexel
