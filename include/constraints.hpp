#pragma once

#include "vexel.hpp"
#include <functional>

namespace vx {
struct RelationKernel;

// Finite relations are obligations in a source resolution context. A trail
// reverses candidate-local domain reductions; the relation graph is shared.
struct ConstraintProblem {
    struct Variable {
        std::string name;
        std::vector<Big> values;
        std::vector<unsigned char> active;
        std::vector<Id> watchers;
        std::size_t remaining = 0;
        bool decision = false;
    };
    struct Relation {
        std::vector<Id> variables;
        std::vector<std::vector<std::size_t>> tuples;
        Span source;
        std::string label;
    };
    struct Removal { Id variable; std::size_t value; Id reason; };
    struct Event {
        std::string kind;
        std::size_t depth;
        Id subject = none;
        std::string detail;
    };
    struct Statistics {
        std::size_t propagations = 0, tupleChecks = 0, decisions = 0;
        std::size_t conflicts = 0, backtracks = 0, solutions = 0, maxDepth = 0;
        std::size_t structuralTrials = 0, structuralConflicts = 0;
    } stats;
    using Proposal = AdviceRecord;
    std::vector<Proposal> proposals;
    std::shared_ptr<RelationKernel> kernel;
    std::string kernelStatus = "disabled", kernelDirectory;
    enum class Outcome { Unique, Ambiguous, Unsatisfiable, Exhausted, Unresolved };
    struct Result {
        Outcome outcome = Outcome::Unresolved;
        std::vector<Big> assignment;
        std::vector<Big> alternative;
        Id conflict = none;
    };

    Options options;
    std::string advisorContext;
    std::vector<Variable> variables;
    std::vector<Relation> relations;
    std::vector<Event> events;
    Result result;
    explicit ConstraintProblem(Options o,std::string context = {}) : options(std::move(o)),advisorContext(std::move(context)) {}
    Id variable(std::string name, std::vector<Big> domain, bool decision = false);
    Id relation(std::vector<Id> vars, std::vector<std::vector<std::size_t>> tuples,
                Span source, std::string label);
    void require(Id variable, const Big& value, Span source, std::string label);
    Result solve(const std::vector<Id>& projection);
    std::string json() const;
    std::optional<Big> adviseStructure(const Graph& graph,const ChoiceRequired& request);
private:
    std::vector<Removal> trail;
    std::vector<Id> agenda;
    std::vector<unsigned char> queued;
    std::mt19937 random{0x435350};
    bool exhausted = false;
    Id lastConflict = none;
    std::string problemKey;
    std::map<std::string, std::string> replay;
    void prepareAdvisor();
    std::optional<std::pair<Id, std::size_t>> advise(std::size_t depth, Id fallback,const std::string& context = {});
    void wake(Id relation);
    void remove(Id variable, std::size_t value, Id reason);
    void restore(std::size_t mark);
    bool propagate(std::size_t depth);
    bool search(std::size_t depth, const std::vector<Id>& projection);
    void event(std::string kind, std::size_t depth, Id subject, std::string detail = {});
};
} // namespace vx
