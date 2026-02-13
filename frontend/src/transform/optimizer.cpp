#include "optimizer.h"

#include "expr_access.h"
#include "typechecker.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vexel {

namespace {

CTValue clone_value(const CTValue& value) {
    if (std::holds_alternative<CTUninitialized>(value)) {
        return CTUninitialized{};
    }
    if (std::holds_alternative<std::shared_ptr<CTComposite>>(value)) {
        auto src = std::get<std::shared_ptr<CTComposite>>(value);
        if (!src) {
            return std::shared_ptr<CTComposite>();
        }
        auto dst = std::make_shared<CTComposite>();
        dst->type_name = src->type_name;
        for (const auto& entry : src->fields) {
            dst->fields[entry.first] = clone_value(entry.second);
        }
        return dst;
    }
    if (std::holds_alternative<std::shared_ptr<CTArray>>(value)) {
        auto src = std::get<std::shared_ptr<CTArray>>(value);
        if (!src) {
            return std::shared_ptr<CTArray>();
        }
        auto dst = std::make_shared<CTArray>();
        dst->elements.reserve(src->elements.size());
        for (const auto& elem : src->elements) {
            dst->elements.push_back(clone_value(elem));
        }
        return dst;
    }
    return value;
}

bool ctvalue_equal(const CTValue& a, const CTValue& b) {
    if (a.index() != b.index()) return false;

    if (std::holds_alternative<int64_t>(a)) {
        return std::get<int64_t>(a) == std::get<int64_t>(b);
    }
    if (std::holds_alternative<uint64_t>(a)) {
        return std::get<uint64_t>(a) == std::get<uint64_t>(b);
    }
    if (std::holds_alternative<double>(a)) {
        return std::get<double>(a) == std::get<double>(b);
    }
    if (std::holds_alternative<bool>(a)) {
        return std::get<bool>(a) == std::get<bool>(b);
    }
    if (std::holds_alternative<std::string>(a)) {
        return std::get<std::string>(a) == std::get<std::string>(b);
    }
    if (std::holds_alternative<CTUninitialized>(a)) {
        return true;
    }
    if (std::holds_alternative<std::shared_ptr<CTComposite>>(a)) {
        auto ca = std::get<std::shared_ptr<CTComposite>>(a);
        auto cb = std::get<std::shared_ptr<CTComposite>>(b);
        if (!ca || !cb) return ca == cb;
        if (ca->type_name != cb->type_name) return false;
        if (ca->fields.size() != cb->fields.size()) return false;
        for (const auto& field : ca->fields) {
            auto it = cb->fields.find(field.first);
            if (it == cb->fields.end()) return false;
            if (!ctvalue_equal(field.second, it->second)) return false;
        }
        return true;
    }
    if (std::holds_alternative<std::shared_ptr<CTArray>>(a)) {
        auto aa = std::get<std::shared_ptr<CTArray>>(a);
        auto ab = std::get<std::shared_ptr<CTArray>>(b);
        if (!aa || !ab) return aa == ab;
        if (aa->elements.size() != ab->elements.size()) return false;
        for (size_t i = 0; i < aa->elements.size(); ++i) {
            if (!ctvalue_equal(aa->elements[i], ab->elements[i])) return false;
        }
        return true;
    }

    return false;
}

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

bool is_scalar_ctvalue(const CTValue& value) {
    return std::holds_alternative<int64_t>(value) ||
           std::holds_alternative<uint64_t>(value) ||
           std::holds_alternative<bool>(value) ||
           std::holds_alternative<double>(value);
}

struct CollectedExpr {
    ExprPtr expr;
    int instance_id = -1;
    ExprFactKey key;
};

class ExprCollector {
public:
    explicit ExprCollector(TypeChecker* checker) : type_checker_(checker) {}

    void collect_module(const Module& mod) {
        if (mod.top_level_instance_ids.size() != mod.top_level.size()) {
            throw CompileError("Internal error: optimizer requires top-level instance IDs aligned with merged module",
                               mod.location);
        }

        for (size_t i = 0; i < mod.top_level.size(); ++i) {
            collect_stmt(mod.top_level[i], mod.top_level_instance_ids[i], true);
        }
    }

    const std::vector<CollectedExpr>& all_exprs() const { return all_exprs_; }
    const std::vector<CollectedExpr>& context_roots() const { return context_roots_; }
    const std::vector<std::pair<StmtFactKey, ExprFactKey>>& var_init_candidates() const {
        return var_init_candidates_;
    }
    const std::vector<std::pair<const Symbol*, ExprFactKey>>& global_constant_candidates() const {
        return global_constant_candidates_;
    }
    const std::unordered_set<ExprFactKey, ExprFactKeyHash>& condition_keys() const {
        return condition_keys_;
    }
    const std::unordered_set<const Symbol*>& function_symbols() const { return function_symbols_; }
    const std::unordered_map<const Symbol*, ExprFactKey>& function_body_keys() const {
        return function_body_keys_;
    }

private:
    TypeChecker* type_checker_ = nullptr;

    std::vector<CollectedExpr> all_exprs_;
    std::vector<CollectedExpr> context_roots_;
    std::vector<std::pair<StmtFactKey, ExprFactKey>> var_init_candidates_;
    std::vector<std::pair<const Symbol*, ExprFactKey>> global_constant_candidates_;
    std::unordered_set<ExprFactKey, ExprFactKeyHash> condition_keys_;
    std::unordered_set<ExprFactKey, ExprFactKeyHash> seen_expr_keys_;
    std::unordered_set<ExprFactKey, ExprFactKeyHash> seen_context_roots_;
    std::unordered_set<const Symbol*> function_symbols_;
    std::unordered_map<const Symbol*, ExprFactKey> function_body_keys_;

    void add_expr(const ExprPtr& expr, int instance_id, bool is_condition_expr) {
        if (!expr) return;
        ExprFactKey key = expr_fact_key(instance_id, expr.get());
        if (is_condition_expr) {
            condition_keys_.insert(key);
        }
        if (seen_expr_keys_.insert(key).second) {
            all_exprs_.push_back(CollectedExpr{expr, instance_id, key});
        }
    }

    void add_context_root(const ExprPtr& expr, int instance_id) {
        if (!expr) return;
        ExprFactKey key = expr_fact_key(instance_id, expr.get());
        if (seen_context_roots_.insert(key).second) {
            context_roots_.push_back(CollectedExpr{expr, instance_id, key});
        }
    }

    void collect_stmt(const StmtPtr& stmt, int instance_id, bool top_level) {
        if (!stmt) return;

        switch (stmt->kind) {
            case Stmt::Kind::FuncDecl: {
                Symbol* sym = type_checker_ ? type_checker_->binding_for(instance_id, stmt.get()) : nullptr;
                if (sym) {
                    function_symbols_.insert(sym);
                }
                if (stmt->body) {
                    add_context_root(stmt->body, instance_id);
                    if (sym) {
                        function_body_keys_[sym] = expr_fact_key(instance_id, stmt->body.get());
                    }
                    collect_expr(stmt->body, instance_id, false);
                }
                break;
            }
            case Stmt::Kind::VarDecl:
                if (stmt->var_init) {
                    ExprFactKey init_key = expr_fact_key(instance_id, stmt->var_init.get());
                    var_init_candidates_.push_back({stmt_fact_key(instance_id, stmt.get()), init_key});
                    add_context_root(stmt->var_init, instance_id);
                    collect_expr(stmt->var_init, instance_id, false);

                    if (top_level) {
                        Symbol* sym = type_checker_ ? type_checker_->binding_for(instance_id, stmt.get()) : nullptr;
                        if (sym && !sym->is_local && sym->kind == Symbol::Kind::Constant) {
                            global_constant_candidates_.push_back({sym, init_key});
                        }
                    }
                }
                break;
            case Stmt::Kind::Expr:
                if (stmt->expr) {
                    add_context_root(stmt->expr, instance_id);
                    collect_expr(stmt->expr, instance_id, false);
                }
                break;
            case Stmt::Kind::Return:
                collect_expr(stmt->return_expr, instance_id, false);
                break;
            case Stmt::Kind::ConditionalStmt:
                collect_expr(stmt->condition, instance_id, true);
                collect_stmt(stmt->true_stmt, instance_id, false);
                break;
            default:
                break;
        }
    }

    void collect_expr(const ExprPtr& expr, int instance_id, bool is_condition_expr) {
        if (!expr) return;
        add_expr(expr, instance_id, is_condition_expr);

        switch (expr->kind) {
            case Expr::Kind::Binary:
            case Expr::Kind::Assignment:
            case Expr::Kind::Range:
                collect_expr(expr->left, instance_id, false);
                collect_expr(expr->right, instance_id, false);
                break;
            case Expr::Kind::Unary:
            case Expr::Kind::Cast:
            case Expr::Kind::Length:
            case Expr::Kind::Member:
                collect_expr(expr->operand, instance_id, false);
                break;
            case Expr::Kind::Call:
                collect_expr(expr->operand, instance_id, false);
                for (const auto& rec : expr->receivers) {
                    collect_expr(rec, instance_id, false);
                }
                for (const auto& arg : expr->args) {
                    collect_expr(arg, instance_id, false);
                }
                break;
            case Expr::Kind::Index:
                collect_expr(expr->operand, instance_id, false);
                for (const auto& arg : expr->args) {
                    collect_expr(arg, instance_id, false);
                }
                break;
            case Expr::Kind::ArrayLiteral:
            case Expr::Kind::TupleLiteral:
                for (const auto& elem : expr->elements) {
                    collect_expr(elem, instance_id, false);
                }
                break;
            case Expr::Kind::Block:
                for (const auto& st : expr->statements) {
                    collect_stmt(st, instance_id, false);
                }
                collect_expr(expr->result_expr, instance_id, false);
                break;
            case Expr::Kind::Conditional:
                collect_expr(expr->condition, instance_id, true);
                collect_expr(expr->true_expr, instance_id, false);
                collect_expr(expr->false_expr, instance_id, false);
                break;
            case Expr::Kind::Iteration:
                collect_expr(loop_subject(expr), instance_id, false);
                collect_expr(loop_body(expr), instance_id, false);
                break;
            case Expr::Kind::Repeat:
                collect_expr(loop_subject(expr), instance_id, true);
                collect_expr(loop_body(expr), instance_id, false);
                break;
            default:
                break;
        }
    }
};

using ExprPtrSet = std::unordered_set<const Expr*>;

void collect_root_expr_nodes_stmt(const StmtPtr& stmt, ExprPtrSet& out);

void collect_root_expr_nodes_expr(const ExprPtr& expr, ExprPtrSet& out) {
    if (!expr) return;
    out.insert(expr.get());

    switch (expr->kind) {
        case Expr::Kind::Binary:
        case Expr::Kind::Assignment:
        case Expr::Kind::Range:
            collect_root_expr_nodes_expr(expr->left, out);
            collect_root_expr_nodes_expr(expr->right, out);
            break;
        case Expr::Kind::Unary:
        case Expr::Kind::Cast:
        case Expr::Kind::Length:
        case Expr::Kind::Member:
            collect_root_expr_nodes_expr(expr->operand, out);
            break;
        case Expr::Kind::Call:
            collect_root_expr_nodes_expr(expr->operand, out);
            for (const auto& rec : expr->receivers) {
                collect_root_expr_nodes_expr(rec, out);
            }
            for (const auto& arg : expr->args) {
                collect_root_expr_nodes_expr(arg, out);
            }
            break;
        case Expr::Kind::Index:
            collect_root_expr_nodes_expr(expr->operand, out);
            for (const auto& arg : expr->args) {
                collect_root_expr_nodes_expr(arg, out);
            }
            break;
        case Expr::Kind::ArrayLiteral:
        case Expr::Kind::TupleLiteral:
            for (const auto& elem : expr->elements) {
                collect_root_expr_nodes_expr(elem, out);
            }
            break;
        case Expr::Kind::Block:
            for (const auto& st : expr->statements) {
                collect_root_expr_nodes_stmt(st, out);
            }
            collect_root_expr_nodes_expr(expr->result_expr, out);
            break;
        case Expr::Kind::Conditional:
            collect_root_expr_nodes_expr(expr->condition, out);
            collect_root_expr_nodes_expr(expr->true_expr, out);
            collect_root_expr_nodes_expr(expr->false_expr, out);
            break;
        case Expr::Kind::Iteration:
        case Expr::Kind::Repeat:
            collect_root_expr_nodes_expr(loop_subject(expr), out);
            collect_root_expr_nodes_expr(loop_body(expr), out);
            break;
        default:
            break;
    }
}

void collect_root_expr_nodes_stmt(const StmtPtr& stmt, ExprPtrSet& out) {
    if (!stmt) return;
    switch (stmt->kind) {
        case Stmt::Kind::VarDecl:
            collect_root_expr_nodes_expr(stmt->var_init, out);
            break;
        case Stmt::Kind::Expr:
            collect_root_expr_nodes_expr(stmt->expr, out);
            break;
        case Stmt::Kind::Return:
            collect_root_expr_nodes_expr(stmt->return_expr, out);
            break;
        case Stmt::Kind::ConditionalStmt:
            collect_root_expr_nodes_expr(stmt->condition, out);
            collect_root_expr_nodes_stmt(stmt->true_stmt, out);
            break;
        case Stmt::Kind::FuncDecl:
            // Root filtering is lexical. Nested function bodies are separate roots.
            break;
        default:
            break;
    }
}

ExprPtrSet collect_root_expr_nodes(const ExprPtr& root) {
    ExprPtrSet out;
    collect_root_expr_nodes_expr(root, out);
    return out;
}

class CTEFixpointScheduler {
public:
    CTEFixpointScheduler(TypeChecker* checker, const Module& mod)
        : type_checker_(checker), collector_(checker) {
        collector_.collect_module(mod);
        expr_enqueued_.assign(collector_.all_exprs().size(), false);
        root_enqueued_.assign(collector_.context_roots().size(), false);

        for (size_t i = 0; i < collector_.all_exprs().size(); ++i) {
            expr_index_by_key_[collector_.all_exprs()[i].key] = i;
            enqueue_expr(i);
        }
        for (size_t i = 0; i < collector_.context_roots().size(); ++i) {
            enqueue_root(i);
        }
        for (const auto& candidate : collector_.global_constant_candidates()) {
            tracked_symbols_.insert(candidate.first);
        }
    }

    OptimizationFacts run() {
        OptimizationFacts facts;
        if (!type_checker_) {
            return facts;
        }

        static constexpr int kMaxCteFixpointIterations = 64;
        int iter = 0;
        while (iter < kMaxCteFixpointIterations) {
            bool progressed = false;

            progressed |= drain_root_queue();
            progressed |= drain_expr_queue();

            std::vector<const Symbol*> changed_symbols;
            progressed |= promote_global_constants(changed_symbols);
            if (!changed_symbols.empty()) {
                enqueue_dependents(changed_symbols);
                progressed = true;
            }

            if (!progressed) break;
            iter++;
        }
        if (iter >= kMaxCteFixpointIterations) {
            throw CompileError("Internal error: compile-time fact scheduler did not converge",
                               SourceLocation());
        }

        facts.constexpr_values = stable_values_;

        for (const auto& key : collector_.condition_keys()) {
            auto it = stable_values_.find(key);
            if (it == stable_values_.end()) continue;
            bool cond = false;
            if (!scalar_to_bool(it->second, cond)) continue;
            facts.constexpr_conditions[key] = cond;
        }

        for (const auto& candidate : collector_.var_init_candidates()) {
            const StmtFactKey& stmt_key = candidate.first;
            const ExprFactKey& expr_key = candidate.second;
            if (stable_values_.count(expr_key)) {
                facts.constexpr_inits.insert(stmt_key);
            }
        }

        finalize_foldable_functions(facts);

        return facts;
    }

private:
    TypeChecker* type_checker_ = nullptr;
    ExprCollector collector_;

    std::unordered_map<ExprFactKey, CTValue, ExprFactKeyHash> stable_values_;
    std::unordered_set<ExprFactKey, ExprFactKeyHash> unstable_values_;
    std::unordered_map<const Symbol*, CTValue> known_symbol_values_;
    std::unordered_set<const Symbol*> tracked_symbols_;

    std::unordered_map<ExprFactKey, size_t, ExprFactKeyHash> expr_index_by_key_;
    std::vector<bool> expr_enqueued_;
    std::vector<bool> root_enqueued_;
    std::queue<size_t> expr_queue_;
    std::queue<size_t> root_queue_;

    std::unordered_map<const Symbol*, std::unordered_set<size_t>> symbol_to_roots_;
    std::unordered_map<size_t, std::unordered_set<const Symbol*>> root_to_symbols_;
    std::unordered_map<const Symbol*, std::unordered_set<ExprFactKey, ExprFactKeyHash>> symbol_to_exprs_;
    std::unordered_map<ExprFactKey, std::unordered_set<const Symbol*>, ExprFactKeyHash> expr_to_symbols_;

    void seed_evaluator(CompileTimeEvaluator& evaluator) const {
        for (const auto& entry : known_symbol_values_) {
            evaluator.set_symbol_constant(entry.first, entry.second);
        }
    }

    static std::unordered_set<const Symbol*> normalize_symbol_set(
        const std::unordered_set<const Symbol*>& in,
        const std::unordered_set<const Symbol*>& allowed) {
        std::unordered_set<const Symbol*> out;
        for (const Symbol* sym : in) {
            if (sym && allowed.count(sym)) {
                out.insert(sym);
            }
        }
        return out;
    }

    void enqueue_expr(size_t idx) {
        if (idx >= expr_enqueued_.size()) return;
        if (expr_enqueued_[idx]) return;
        expr_enqueued_[idx] = true;
        expr_queue_.push(idx);
    }

    void enqueue_root(size_t idx) {
        if (idx >= root_enqueued_.size()) return;
        if (root_enqueued_[idx]) return;
        root_enqueued_[idx] = true;
        root_queue_.push(idx);
    }

    void update_root_dependencies(size_t root_idx,
                                  const std::unordered_set<const Symbol*>& symbols) {
        const auto normalized = normalize_symbol_set(symbols, tracked_symbols_);
        auto& old = root_to_symbols_[root_idx];
        for (const Symbol* sym : old) {
            if (!normalized.count(sym)) {
                auto sit = symbol_to_roots_.find(sym);
                if (sit != symbol_to_roots_.end()) {
                    sit->second.erase(root_idx);
                    if (sit->second.empty()) {
                        symbol_to_roots_.erase(sit);
                    }
                }
            }
        }
        for (const Symbol* sym : normalized) {
            symbol_to_roots_[sym].insert(root_idx);
        }
        old = normalized;
    }

    void update_expr_dependencies(const ExprFactKey& key,
                                  const std::unordered_set<const Symbol*>& symbols) {
        const auto normalized = normalize_symbol_set(symbols, tracked_symbols_);
        auto& old = expr_to_symbols_[key];
        for (const Symbol* sym : old) {
            if (!normalized.count(sym)) {
                auto sit = symbol_to_exprs_.find(sym);
                if (sit != symbol_to_exprs_.end()) {
                    sit->second.erase(key);
                    if (sit->second.empty()) {
                        symbol_to_exprs_.erase(sit);
                    }
                }
            }
        }
        for (const Symbol* sym : normalized) {
            symbol_to_exprs_[sym].insert(key);
        }
        old = normalized;
    }

    bool observe_expr_value(const ExprFactKey& key, const CTValue& value) {
        if (!key.expr) return false;

        if (unstable_values_.count(key)) {
            return false;
        }

        auto it = stable_values_.find(key);
        if (it == stable_values_.end()) {
            stable_values_[key] = clone_value(value);
            return true;
        }

        if (ctvalue_equal(it->second, value)) {
            return false;
        }

        stable_values_.erase(it);
        unstable_values_.insert(key);
        return true;
    }

    bool drain_root_queue() {
        bool changed = false;
        while (!root_queue_.empty()) {
            size_t root_idx = root_queue_.front();
            root_queue_.pop();
            if (root_idx < root_enqueued_.size()) {
                root_enqueued_[root_idx] = false;
            }
            if (root_idx >= collector_.context_roots().size()) continue;

            const CollectedExpr& root = collector_.context_roots()[root_idx];
            auto scope = type_checker_->scoped_instance(root.instance_id);
            (void)scope;

            ExprPtrSet root_expr_nodes = collect_root_expr_nodes(root.expr);
            CompileTimeEvaluator evaluator(type_checker_);
            seed_evaluator(evaluator);

            std::unordered_map<ExprFactKey, CTValue, ExprFactKeyHash> local_stable;
            std::unordered_set<ExprFactKey, ExprFactKeyHash> local_unstable;
            std::unordered_set<const Symbol*> local_symbols;

            auto observe_local = [&](const ExprFactKey& key, const CTValue& value) {
                if (!key.expr || local_unstable.count(key)) return;
                auto it = local_stable.find(key);
                if (it == local_stable.end()) {
                    local_stable.emplace(key, clone_value(value));
                    return;
                }
                if (!ctvalue_equal(it->second, value)) {
                    local_stable.erase(it);
                    local_unstable.insert(key);
                }
            };
            evaluator.set_symbol_read_observer([&](const Symbol* sym) {
                if (!sym) return;
                local_symbols.insert(sym);
            });
            evaluator.set_value_observer([&](const Expr* expr, const CTValue& value) {
                if (!expr) return;
                if (!root_expr_nodes.count(expr)) return;
                observe_local(expr_fact_key(root.instance_id, expr), value);
            });

            CTEQueryResult query = evaluator.query(root.expr);
            update_root_dependencies(root_idx, local_symbols);
            if (query.status != CTEQueryStatus::Known) {
                continue;
            }

            for (const auto& key : local_unstable) {
                if (stable_values_.count(key)) {
                    stable_values_.erase(key);
                    unstable_values_.insert(key);
                    changed = true;
                } else if (!unstable_values_.count(key)) {
                    unstable_values_.insert(key);
                    changed = true;
                }
            }
            for (const auto& entry : local_stable) {
                if (observe_expr_value(entry.first, entry.second)) {
                    changed = true;
                }
            }
        }
        return changed;
    }

    bool drain_expr_queue() {
        bool changed = false;
        while (!expr_queue_.empty()) {
            size_t expr_idx = expr_queue_.front();
            expr_queue_.pop();
            if (expr_idx < expr_enqueued_.size()) {
                expr_enqueued_[expr_idx] = false;
            }
            if (expr_idx >= collector_.all_exprs().size()) continue;

            const CollectedExpr& item = collector_.all_exprs()[expr_idx];
            ExprFactKey key = item.key;
            auto scope = type_checker_->scoped_instance(item.instance_id);
            (void)scope;
            CompileTimeEvaluator evaluator(type_checker_);
            seed_evaluator(evaluator);

            std::unordered_set<const Symbol*> local_symbols;
            evaluator.set_symbol_read_observer([&](const Symbol* sym) {
                if (!sym) return;
                local_symbols.insert(sym);
            });

            CTEQueryResult query = evaluator.query(item.expr);
            update_expr_dependencies(key, local_symbols);
            if (query.status == CTEQueryStatus::Known) {
                if (observe_expr_value(key, query.value)) {
                    changed = true;
                }
            }
        }
        return changed;
    }

    bool promote_global_constants(std::vector<const Symbol*>& changed_symbols) {
        bool changed = false;
        for (const auto& candidate : collector_.global_constant_candidates()) {
            const Symbol* sym = candidate.first;
            const ExprFactKey& key = candidate.second;
            auto value_it = stable_values_.find(key);
            if (value_it == stable_values_.end()) {
                continue;
            }

            auto known_it = known_symbol_values_.find(sym);
            if (known_it == known_symbol_values_.end()) {
                known_symbol_values_[sym] = clone_value(value_it->second);
                changed_symbols.push_back(sym);
                changed = true;
                continue;
            }

            if (!ctvalue_equal(known_it->second, value_it->second)) {
                throw CompileError("Internal error: non-monotonic compile-time value for symbol '" + sym->name + "'",
                                   sym->declaration ? sym->declaration->location : SourceLocation());
            }
        }
        return changed;
    }

    void enqueue_dependents(const std::vector<const Symbol*>& changed_symbols) {
        for (const Symbol* sym : changed_symbols) {
            auto root_it = symbol_to_roots_.find(sym);
            if (root_it != symbol_to_roots_.end()) {
                for (size_t root_idx : root_it->second) {
                    enqueue_root(root_idx);
                }
            }

            auto expr_it = symbol_to_exprs_.find(sym);
            if (expr_it != symbol_to_exprs_.end()) {
                for (const auto& key : expr_it->second) {
                    auto idx_it = expr_index_by_key_.find(key);
                    if (idx_it != expr_index_by_key_.end()) {
                        enqueue_expr(idx_it->second);
                    }
                }
            }
        }
    }

    void finalize_foldable_functions(OptimizationFacts& facts) const {
        for (const Symbol* sym : collector_.function_symbols()) {
            if (!sym || sym->kind != Symbol::Kind::Function || !sym->declaration) {
                continue;
            }

            if (sym->is_external || !sym->declaration->body) {
                facts.fold_skip_reasons[sym] = "external-or-no-body";
                continue;
            }
            if (!sym->declaration->params.empty()) {
                facts.fold_skip_reasons[sym] = "parameterized";
                continue;
            }
            if (!sym->declaration->ref_params.empty()) {
                facts.fold_skip_reasons[sym] = "has-receivers";
                continue;
            }

            auto body_it = collector_.function_body_keys().find(sym);
            if (body_it == collector_.function_body_keys().end()) {
                facts.fold_skip_reasons[sym] = "missing-body-key";
                continue;
            }

            const ExprFactKey& body_key = body_it->second;
            if (unstable_values_.count(body_key)) {
                facts.fold_skip_reasons[sym] = "non-deterministic";
                continue;
            }
            auto value_it = stable_values_.find(body_key);
            if (value_it == stable_values_.end()) {
                facts.fold_skip_reasons[sym] = "evaluation-failed-or-runtime-dependent";
                continue;
            }
            if (!is_scalar_ctvalue(value_it->second)) {
                facts.fold_skip_reasons[sym] = "non-scalar-result";
                continue;
            }

            facts.foldable_functions.insert(sym);
        }
    }
};

} // namespace

OptimizationFacts Optimizer::run(const Module& mod) {
    CTEFixpointScheduler scheduler(type_checker, mod);
    return scheduler.run();
}

} // namespace vexel
