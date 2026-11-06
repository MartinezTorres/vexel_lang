#include "constraints.hpp"
#include <algorithm>

namespace vx {
struct Solver::Checkpoint {
    Graph graph;
    Solver solver;
    explicit Checkpoint(const Solver& source) : graph(source.g), solver(graph) {
        for (auto& [context,problem] : graph.puzzles) problem = std::make_shared<ConstraintProblem>(*problem);
        source.copyContext(solver);
    }
};
void Solver::copyContext(Solver& destination) const {
    destination.globals = globals; destination.pureCalls = pureCalls;
    destination.annotations = annotations; destination.scopedAnnotations = scopedAnnotations;
    destination.parameterAnnotations = parameterAnnotations;
    destination.loads = loads; destination.checkpoints = checkpoints;
    destination.propertyLoads=propertyLoads;
    destination.propertyValues=propertyValues;
    destination.typeEquations=typeEquations;destination.equationChecks=equationChecks;
    destination.equationWitnesses=equationWitnesses;
    destination.typeAliasProofs=typeAliasProofs;
    destination.propertyNodes=propertyNodes;destination.propertyProofs=propertyProofs;
    destination.propertyAssumptions=propertyAssumptions;destination.propertyTrial=propertyTrial;
    destination.typeEquationTrial=typeEquationTrial;
    destination.structuralTrials = structuralTrials;
    destination.runtimeParameters = runtimeParameters;
    destination.runtimeCaptures = runtimeCaptures;
    destination.externalRecursion = externalRecursion;
    destination.recursiveInterfaces = recursiveInterfaces;
    destination.recursiveRuntimeInstances = recursiveRuntimeInstances;
    destination.specializedSignatures = specializedSignatures;
}
void Solver::solve() {
    // Widening changes a specialization assumption. Restore the whole derived
    // graph, rather than changing facts already consumed by another obligation.
    Checkpoint initial(*this);
    auto restore = [&]() {
        auto parameters = runtimeParameters; auto captures = runtimeCaptures; auto effects = externalRecursion;
        auto interfaces = recursiveInterfaces;
        auto instances = recursiveRuntimeInstances;
        auto signatures = specializedSignatures;
        auto equations=typeEquations;
        g = initial.graph;
        for (auto& [context,problem] : g.puzzles) problem = std::make_shared<ConstraintProblem>(*problem);
        initial.solver.copyContext(*this);
        runtimeParameters = std::move(parameters); runtimeCaptures = std::move(captures); externalRecursion = std::move(effects);
        recursiveInterfaces = std::move(interfaces);
        recursiveRuntimeInstances = std::move(instances);
        specializedSignatures = std::move(signatures);
        typeEquations=std::move(equations);
        queue.clear(); fifoHead = 0; processingType = false; ownedPuzzles.clear();
        for (Id i = 0; i < g.nodes.size(); ++i) {
            auto& node = g.nodes[i]; node.queued = node.typeQueued = false;
            if (node.demanded) enqueue(i);
            if (node.typeDemanded) enqueueType(i);
        }
    };
    for (;;) {
        try { solveWorklist(); return; }
        catch (const RecursionWidening& request) {
            if (request.target < initial.graph.nodes.size() && initial.graph.nodes[request.target].expansion != none) throw;
            auto widened = runtimeParameters;
            auto captures = runtimeCaptures;
            bool instance = recursiveRuntimeInstances.insert(g.nodes[request.target].key).second;
            if (!(request.capture ? captures.insert({request.function,request.parameter}).second :
                    widened.insert({request.function,request.parameter}).second) && !instance)
                throw std::logic_error("recursive parameter widening made no progress");
            runtimeParameters = std::move(widened); runtimeCaptures = std::move(captures); restore();
        } catch (const RecursionEffectRequired& request) {
            if (request.target < initial.graph.nodes.size() && initial.graph.nodes[request.target].expansion != none) throw;
            if (!externalRecursion.insert(request.function).second) throw std::logic_error("recursive effect closure made no progress");
            restore();
        } catch (const RecursionTypeRequired& request) {
            if (request.target < initial.graph.nodes.size() && initial.graph.nodes[request.target].expansion != none) throw;
            recursiveInterfaces[request.key] = request.type; restore();
        } catch (const TypeEquationsRequired& request) {
            for(const auto& [declaration,witness]:request.witnesses)
                if(declaration>=initial.graph.nodes.size()) throw;
            // A nested optional/structural checkpoint may already contain
            // consumers of these interfaces. Only the checkpoint preceding
            // those observations owns their revision, just as for recursive
            // call specialization. Never retrofit a candidate into that prefix.
            auto observed=[&](Id declaration) {
                for(const auto& node:initial.graph.nodes)
                    if(node.rule==Rule::Global && node.site==declaration &&
                       (node.expanded || node.signature!=none || node.inferredType || node.fact ||
                        node.key=="global-alias-checked")) return true;
                return false;
            };
            for(const auto& [declaration,witness]:request.witnesses)
                if(observed(declaration)) throw;
            auto previous=typeEquations;
            reviseTypeEquations(request,initial.graph,initial.solver);
            // An independent new component need not invalidate previously
            // checked components. If it revises one, its earlier observers
            // must be rebuilt by that component's owning checkpoint as well.
            for(const auto& [declaration,candidate]:typeEquations)
                if(auto old=previous.find(declaration);old!=previous.end() && !(candidate==old->second) && observed(declaration)) {
                    TypeEquationsRequired revised{typeEquations};
                    typeEquations=std::move(previous);throw revised;
                }
            restore();
        } catch (const SignatureRevision& request) {
            if (request.target < initial.graph.nodes.size() && initial.graph.nodes[request.target].expansion != none) throw;
            if (!specializedSignatures.insert(request.key).second) throw std::logic_error("signature specialization made no progress");
            restore();
        }
    }
}
void Solver::checkpoint(Id context) {
    ownedPuzzles.insert(context);
    checkpoints[context] = std::make_shared<Checkpoint>(*this);
}
[[noreturn]] void Solver::requestChoice(const Value& value, Id at) const {
    Id context = none;
    std::function<void(const Value&)> find = [&](const Value& v) {
        if (v.kind == Value::Symbolic) context = v.context;
        for (const auto& item : v.elements) find(item);
    };
    find(value);
    if (context == none) fail(at,"structural property requires known pure information");
    Id scope = g.nodes[at].scope;
    if (scope != none && g.scopes[scope].puzzle != none && g.scopes[scope].puzzle != context)
        fail(at,"nested solve cannot capture unresolved choices from its parent");
    const auto& p = *g.puzzles.at(context);
    const ConstraintProblem::Variable* choice = nullptr;
    for (const auto& v : p.variables) if (v.decision && v.values.size() > 1 && !g.options.structuralAssumptions.contains(v.name))
        if (!choice || v.values.size() < choice->values.size() || (v.values.size() == choice->values.size() && v.name < choice->name)) choice = &v;
    if (!choice) fail(at,"structural property remains unresolved without a finite decision");
    throw ChoiceRequired{context,choice->name,choice->values};
}
void Solver::branch(const ChoiceRequired& request) {
    auto saved = checkpoints.at(request.context);
    const Options originalOptions = g.options;
    if (originalOptions.structuralAssumptions.size() >= 32) fail(request.context,"structural revision depth budget exhausted",Diagnostic::Exhausted);
    auto originalProblem = g.puzzles.at(request.context);
    std::shared_ptr<Checkpoint> winner;
    std::optional<Fact> answer;
    std::optional<Error> unresolved;
    std::set<std::string> dependencies = g.dependencies;
    std::string lastFailure;
    std::size_t trials = 0, conflicts = 0;
    auto alternatives = request.domain;
    if (g.options.branchOrder == "reverse") std::reverse(alternatives.begin(),alternatives.end());
    else if (g.options.branchOrder == "random") std::shuffle(alternatives.begin(),alternatives.end(),rng);
    if(auto preferred=originalProblem->adviseStructure(g,request))
        std::rotate(alternatives.begin(),std::find(alternatives.begin(),alternatives.end(),*preferred),alternatives.end());
    auto record = [&](const std::string& kind,const Big& value) {
        if (originalProblem->events.size() < g.options.maxElements)
            originalProblem->events.push_back({kind,originalOptions.structuralAssumptions.size(),none,request.name+"="+value.str()});
    };
    for (const auto& alternative : alternatives) {
        if (originalOptions.searchBudget->decisions >= originalOptions.maxDecisions || steps >= originalOptions.maxSteps) {
            originalProblem->result.outcome = ConstraintProblem::Outcome::Exhausted;
            fail(request.context,"structural search budget exhausted; remaining revisions are unresolved",Diagnostic::Exhausted);
        }
        ++*structuralTrials; ++originalOptions.searchBudget->decisions;
        ++trials; record("assume-structure",alternative);
        Graph candidate = saved->graph;
        // Constraint contexts are mutable and must not be shared by revisions.
        for (auto& [context,problem] : candidate.puzzles) problem = std::make_shared<ConstraintProblem>(*problem);
        candidate.options.structuralAssumptions = originalOptions.structuralAssumptions;
        candidate.options.structuralAssumptions[request.name] = alternative;
        candidate.options.maxSteps = originalOptions.maxSteps - steps;
        for (auto& node : candidate.nodes) {
            node.demanded = node.queued = node.typeDemanded = node.typeQueued = false;
        }
        Solver trial(candidate); saved->solver.copyContext(trial);
        trial.roots.push_back({"structural candidate",request.context,false,false});
        try {
            trial.solve();
            steps += trial.steps; invocations += trial.invocations; cacheHits += trial.cacheHits;
            dependencies.insert(candidate.dependencies.begin(),candidate.dependencies.end());
            const Fact& value = *candidate.nodes[request.context].fact;
            if (!answer) { answer = value; winner = std::make_shared<Checkpoint>(trial); }
            else if (!(answer->type == value.type) || !(answer->value == value.value)) {
                originalProblem->result.outcome = ConstraintProblem::Outcome::Ambiguous;
                originalProblem->stats.structuralTrials = trials;
                originalProblem->stats.structuralConflicts = conflicts;
                fail(request.context,"constraint result is ambiguous: structural revisions produce distinct observable answers",Diagnostic::SearchAmbiguous);
            }
        } catch (const Error& error) {
            std::string message = error.what();
            if (error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay) {
                originalProblem->result.outcome = ConstraintProblem::Outcome::Exhausted;
                throw;
            }
            if (error.category==Diagnostic::SearchAmbiguous) {
                originalProblem->result.outcome = ConstraintProblem::Outcome::Ambiguous; throw;
            }
            steps += trial.steps; invocations += trial.invocations; cacheHits += trial.cacheHits;
            dependencies.insert(candidate.dependencies.begin(),candidate.dependencies.end());
            if(error.category==Diagnostic::Unresolved) {
                if(!unresolved) unresolved=error;
                record("unresolved-structure",alternative);
            } else {
                ++conflicts;lastFailure=std::move(message);record("conflict-structure",alternative);
            }
        }
        record("backtrack-structure",alternative);
    }
    originalProblem->stats.structuralTrials = trials; originalProblem->stats.structuralConflicts = conflicts;
    if(unresolved) {
        originalProblem->result.outcome=ConstraintProblem::Outcome::Unresolved;
        throw *unresolved;
    }
    if (!winner) {
        originalProblem->result.outcome = ConstraintProblem::Outcome::Unsatisfiable;
        fail(request.context,"constraint system has no solution across structural revisions\n  last rejected revision: " + lastFailure);
    }
    auto& solved = *winner->graph.puzzles.at(request.context);
    solved.stats.structuralTrials += trials; solved.stats.structuralConflicts += conflicts;
    solved.proposals.insert(solved.proposals.begin(),originalProblem->proposals.begin(),originalProblem->proposals.end());
    for (const auto& event : originalProblem->events) if (solved.events.size() < g.options.maxElements) solved.events.push_back(event);
    // Restore demand in the stable checkpoint prefix. Later derived nodes from
    // the rejected revision are replaced; no stale queue handle can survive.
    for (Id i = 0; i < saved->graph.nodes.size(); ++i) {
        winner->graph.nodes[i].demanded = winner->graph.nodes[i].demanded || g.nodes[i].demanded;
        winner->graph.nodes[i].typeDemanded = winner->graph.nodes[i].typeDemanded || g.nodes[i].typeDemanded;
    }
    g = std::move(winner->graph); g.options = originalOptions;
    g.dependencies.insert(dependencies.begin(),dependencies.end());
    winner->solver.copyContext(*this);
    queue.clear(); fifoHead = 0;
    for (Id i = 0; i < g.nodes.size(); ++i) {
        auto& node = g.nodes[i]; node.queued = node.typeQueued = false;
        if (node.demanded) enqueue(i);
        if (node.typeDemanded) enqueueType(i);
    }
}
} // namespace vx
