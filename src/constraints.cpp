#include "constraints.hpp"
#include "kernel.hpp"
#include <algorithm>
#include <sstream>

namespace vx {
Id ConstraintProblem::variable(std::string name, std::vector<Big> domain, bool decision) {
    std::sort(domain.begin(), domain.end());
    domain.erase(std::unique(domain.begin(), domain.end()), domain.end());
    if (domain.empty()) throw Error({}, "finite choice requires a nonempty domain");
    if (domain.size() > options.maxElements) throw Error({}, "constraint domain exceeds element budget",Diagnostic::Exhausted);
    if (variables.size() >= options.maxNodes) throw Error({}, "constraint variable budget exhausted",Diagnostic::Exhausted);
    Variable v; v.name = std::move(name); v.values = std::move(domain);
    v.remaining = v.values.size(); v.active.assign(v.remaining, 1); v.decision = decision;
    variables.push_back(std::move(v)); return variables.size() - 1;
}
Id ConstraintProblem::relation(std::vector<Id> vars,
        std::vector<std::vector<std::size_t>> tuples, Span source, std::string label) {
    if (relations.size() >= options.maxNodes) throw Error(source, "constraint relation budget exhausted",Diagnostic::Exhausted);
    for (const auto& tuple : tuples) {
        if (tuple.size() != vars.size()) throw std::logic_error("relation tuple arity mismatch");
        for (std::size_t i = 0; i < vars.size(); ++i)
            if (tuple[i] >= variables.at(vars[i]).values.size()) throw std::logic_error("relation value outside domain");
    }
    // Repeated inputs describe one variable, never independent occurrences.
    std::erase_if(tuples, [&](const auto& tuple) {
        for (std::size_t i = 0; i < vars.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
                if (vars[i] == vars[j] && tuple[i] != tuple[j]) return true;
        return false;
    });
    Id id = relations.size(); relations.push_back({std::move(vars), std::move(tuples), source, std::move(label)});
    for (Id v : relations.back().variables) variables[v].watchers.push_back(id);
    return id;
}
void ConstraintProblem::require(Id v, const Big& value, Span source, std::string label) {
    const auto& domain = variables.at(v).values;
    auto it = std::lower_bound(domain.begin(), domain.end(), value);
    std::vector<std::vector<std::size_t>> tuples;
    if (it != domain.end() && *it == value) tuples.push_back({static_cast<std::size_t>(it - domain.begin())});
    relation({v}, std::move(tuples), source, std::move(label));
}
void ConstraintProblem::event(std::string kind, std::size_t depth, Id subject, std::string detail) {
    // Keep diagnostics bounded independently of the length of the search.
    if (events.size() < options.maxElements) events.push_back({std::move(kind), depth, subject, std::move(detail)});
}
void ConstraintProblem::wake(Id id) {
    if (!queued[id]) { queued[id] = 1; agenda.push_back(id); }
}
void ConstraintProblem::remove(Id id, std::size_t value, Id reason) {
    auto& v = variables[id];
    if (!v.active[value]) return;
    trail.push_back({id, value, reason}); v.active[value] = 0; --v.remaining;
    for (Id r : v.watchers) wake(r);
}
void ConstraintProblem::restore(std::size_t mark) {
    while (trail.size() > mark) {
        auto removal = trail.back(); trail.pop_back();
        auto& v = variables[removal.variable]; v.active[removal.value] = 1; ++v.remaining;
    }
    agenda.clear(); std::fill(queued.begin(), queued.end(), 0);
}
bool ConstraintProblem::propagate(std::size_t depth) {
    while (!agenda.empty()) {
        if (++stats.propagations > options.maxSteps) { exhausted = true; return false; }
        std::size_t index = options.schedule == "fifo" ? 0 : agenda.size() - 1;
        if (options.schedule == "random") index = std::uniform_int_distribution<std::size_t>(0, agenda.size()-1)(random);
        Id id = agenda[index];
        if (options.schedule == "fifo") agenda.erase(agenda.begin());
        else { agenda[index] = agenda.back(); agenda.pop_back(); }
        queued[id] = 0;
        const auto& r = relations[id];
        std::vector<std::vector<unsigned char>> supported;
        for (Id v : r.variables) supported.emplace_back(variables[v].values.size(), 0);
        std::size_t checks = 0, budget = options.maxConstraintChecks - stats.tupleChecks;
        if (options.searchBudget) budget = std::min(budget,options.maxConstraintChecks-options.searchBudget->checks);
        auto ordinary = [&](auto& support, std::size_t& used) {
            bool any = false;
            for (const auto& tuple : r.tuples) {
                if (used == budget) return -2;
                ++used;
                bool valid = true;
                for (std::size_t i = 0; i < tuple.size(); ++i)
                    if (!variables[r.variables[i]].active[tuple[i]]) { valid = false; break; }
                if (!valid) continue;
                any = true;
                for (std::size_t i = 0; i < tuple.size(); ++i) support[i][tuple[i]] = 1;
            }
            return any ? 1 : 0;
        };
        int status;
        if (kernel) {
            std::vector<const unsigned char*> active;
            std::vector<unsigned char*> support;
            for (std::size_t i = 0; i < r.variables.size(); ++i) {
                active.push_back(variables[r.variables[i]].active.data()); support.push_back(supported[i].data());
            }
            status = kernel->filter(id,active.data(),support.data(),budget,&checks);
            if (options.verifySearchKernel) {
                auto expected = supported;
                for (auto& column : expected) std::fill(column.begin(),column.end(),0);
                std::size_t used = 0; int reference = ordinary(expected,used);
                if (status != reference || checks != used || supported != expected)
                    throw std::logic_error("compiled relation filter disagrees with ordinary propagation");
            }
        } else status = ordinary(supported,checks);
        stats.tupleChecks += checks;
        if (options.searchBudget) options.searchBudget->checks += checks;
        if (status == -2) { exhausted = true; return false; }
        if (status == 0) {
            lastConflict = id; ++stats.conflicts;
            event("conflict", depth, id, r.label); return false;
        }
        for (std::size_t i = 0; i < r.variables.size(); ++i)
            for (std::size_t j = 0; j < supported[i].size(); ++j)
                if (!supported[i][j]) remove(r.variables[i], j, id);
    }
    return true;
}
bool ConstraintProblem::search(std::size_t depth, const std::vector<Id>& projection) {
    stats.maxDepth = std::max(stats.maxDepth, depth);
    if (depth > 512) { exhausted = true; return true; }
    if (!propagate(depth)) return exhausted;
    // After one witness, close branches whose entire observable projection is
    // fixed to that witness. Hidden choices cannot introduce output ambiguity.
    auto projectionFixed = [&]() {
        if (result.assignment.empty()) return false;
        bool equal = true;
        for (Id id : projection) {
            const auto& v = variables[id];
            if (v.remaining != 1) { equal = false; break; }
            auto at = std::find(v.active.begin(), v.active.end(), 1);
            if (v.values[static_cast<std::size_t>(at-v.active.begin())] != result.assignment[id]) { equal = false; break; }
        }
        return equal;
    };
    if (projectionFixed()) return false;
    Id choice = none;
    for (Id i = 0; i < variables.size(); ++i) {
        const auto& v = variables[i];
        if (v.remaining < 2) continue;
        if (choice == none || (v.decision && !variables[choice].decision) ||
            (v.decision == variables[choice].decision &&
             (v.remaining < variables[choice].remaining ||
              (v.remaining == variables[choice].remaining && v.name < variables[choice].name)))) choice = i;
    }
    if (choice == none) {
        std::vector<Big> assignment;
        for (const auto& v : variables) {
            if (!v.remaining) throw std::logic_error("empty domain without conflict");
            auto at = std::find(v.active.begin(), v.active.end(), 1);
            assignment.push_back(v.values[static_cast<std::size_t>(at-v.active.begin())]);
        }
        ++stats.solutions; event("solution", depth, none);
        if (stats.solutions == 1) { result.assignment = std::move(assignment); return false; }
        result.alternative = std::move(assignment); result.outcome = Outcome::Ambiguous; return true;
    }
    std::vector<std::size_t> alternatives;
    auto proposal = advise(depth, choice);
    if (proposal) choice = proposal->first;
    for (std::size_t i = 0; i < variables[choice].active.size(); ++i)
        if (variables[choice].active[i]) alternatives.push_back(i);
    if (options.branchOrder == "reverse") std::reverse(alternatives.begin(), alternatives.end());
    else if (options.branchOrder == "random") std::shuffle(alternatives.begin(), alternatives.end(), random);
    if (proposal) std::rotate(alternatives.begin(), std::find(alternatives.begin(), alternatives.end(), proposal->second), alternatives.end());
    const auto mark = trail.size();
    for (auto selected : alternatives) {
        if (stats.decisions >= options.maxDecisions ||
            (options.searchBudget && options.searchBudget->decisions >= options.maxDecisions)) { exhausted = true; return true; }
        ++stats.decisions;
        if (options.searchBudget) ++options.searchBudget->decisions;
        event("assume", depth, choice, variables[choice].values[selected].str());
        for (auto other : alternatives) if (other != selected) remove(choice, other, none);
        bool stop = search(depth+1, projection);
        restore(mark); ++stats.backtracks;
        event("backtrack", depth, choice);
        if (stop) return true;
        if (projectionFixed()) return false;
    }
    return false;
}
ConstraintProblem::Result ConstraintProblem::solve(const std::vector<Id>& projection) {
    stats = {}; events.clear(); result = {}; trail.clear(); agenda.clear();
    queued.assign(relations.size(), 0); exhausted = false; lastConflict = none;
    for (auto& v : variables) { v.active.assign(v.values.size(), 1); v.remaining = v.values.size(); }
    for (Id i = 0; i < relations.size(); ++i) wake(i);
    prepareAdvisor();
    kernel = compileRelations(*this);
    (void)search(0, projection);
    if (exhausted) result.outcome = Outcome::Exhausted;
    else if (result.outcome != Outcome::Ambiguous)
        result.outcome = stats.solutions ? Outcome::Unique : Outcome::Unsatisfiable;
    result.conflict = lastConflict;
    restore(0);
    return result;
}
std::string ConstraintProblem::json() const {
    std::ostringstream out;
    const char* status = result.outcome == Outcome::Unique ? "unique" : result.outcome == Outcome::Ambiguous ?
        "ambiguous" : result.outcome == Outcome::Unsatisfiable ? "unsatisfiable" :
        result.outcome == Outcome::Unresolved ? "unresolved" : "exhausted";
    out << "{\"status\":" << quote(status) << ",\"variables\":" << variables.size()
        << ",\"kernel\":" << quote(kernelStatus) << ",\"kernel_directory\":" << quote(kernelDirectory)
        << ",\"relations\":" << relations.size() << ",\"propagations\":" << stats.propagations
        << ",\"tuple_checks\":" << stats.tupleChecks << ",\"decisions\":" << stats.decisions
        << ",\"conflicts\":" << stats.conflicts << ",\"backtracks\":" << stats.backtracks
        << ",\"structural_trials\":" << stats.structuralTrials << ",\"structural_conflicts\":" << stats.structuralConflicts
        << ",\"solutions\":" << stats.solutions << ",\"max_depth\":" << stats.maxDepth << ",\"events\":[";
    bool first = true;
    for (const auto& e : events) {
        if (!first) out << ',';
        first = false;
        out << "{\"kind\":" << quote(e.kind) << ",\"depth\":" << e.depth;
        if (e.subject != none) out << ",\"subject\":" << e.subject;
        out << ",\"detail\":" << quote(e.detail) << '}';
    }
    out << "],\"proposals\":["; first = true;
    for (const auto& proposal : proposals) {
        if (!first) out << ',';
        first = false;
        out << "{\"request\":" << quote(proposal.request) << ",\"response\":" << quote(proposal.response)
            << ",\"status\":" << quote(proposal.status) << '}';
    }
    return out.str() + "]}";
}
} // namespace vx
