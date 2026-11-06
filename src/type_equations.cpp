#include "vexel.hpp"
#include "constraints.hpp"

namespace vx {
namespace {
bool hasEquationWitness(const Node& annotation) {
    return annotation.body!=none || (annotation.text!="(tuple)" && !annotation.children.empty());
}
struct TypeDemand {
    bool& active;
    bool previous;
    explicit TypeDemand(bool& state):active(state),previous(state) {active=true;}
    ~TypeDemand() {active=previous;}
};
}
bool Solver::stableEquationType(const Type& type) {
    if(type.kind==Type::Array) return stableEquationType(*type.element);
    // Record interfaces carry schema-environment handles. Those require an
    // explicit rebind operation before they can cross graph revisions.
    return type.kind!=Type::Record && type.kind!=Type::Unknown && type.kind!=Type::Empty;
}
Id Solver::typeEquationWitness(Id owner,Id supplied) {
    const auto& node=g.nodes[owner];
    const auto& declaration=g.nodes[node.site];
    const auto& annotation=g.nodes[declaration.annotation];
    if(annotation.body!=none) return supplied;
    if(auto found=equationWitnesses.find(owner);found!=equationWitnesses.end()) return found->second;
    // Keep the declared element family. Only the initializer's array extents
    // are provisional; ordinary queries later prove every original dimension.
    Id requirement=g.semantic(Rule::Type,declaration.annotation,{},node.scope);
    g.nodes[requirement].key="equation-witness";g.nodes[requirement].slot=supplied;
    g.nodes[requirement].previous=node.previous;
    Id witness=g.semantic(Rule::Input,node.site,{supplied},node.scope);
    g.nodes[witness].annotation=requirement;equationWitnesses[owner]=witness;
    return witness;
}
bool Solver::inferTypeEquations() {
    if(propertyTrial) return false;
    if(typeEquationTrial) return inferContextTypeEquations();
    TypeDemand demandType(processingType);
    TypeEquationsRequired request;
    const auto queued=queue.size();
    const auto count=g.nodes.size();
    for(Id id=0;id<count;++id) {
        auto& node=g.nodes[id];
        if(node.rule!=Rule::Global || node.scope!=none || (!node.demanded && !node.typeDemanded) || node.fact ||
           (node.inferredType && node.inferredType->settled()) || typeEquations.contains(node.site)) continue;
        const auto& declaration=g.nodes[node.site];
        if(declaration.annotation==none || declaration.children.empty() || !hasEquationWitness(g.nodes[declaration.annotation])) continue;
        prepareGlobal(id);
        const Type* witness=needType(id,typeEquationWitness(id,node.annotation));
        if(witness && witness->settled() && stableEquationType(*witness)) request.witnesses[node.site]=*witness;
    }
    // Finish discovering every currently needed witness before choosing the
    // candidate vector, so worklist scheduling does not choose its seed set.
    if(queue.size()!=queued) return true;
    if(!request.witnesses.empty()) throw request;
    return inferContextTypeEquations();
}
bool Solver::inferContextTypeEquations() {
    TypeDemand demandType(processingType);
    bool complete=true;
    for(const auto& root:roots) complete=complete && g.nodes[root.node].fact.has_value();
    for(const auto& [owner,check]:equationChecks) { (void)owner;complete=complete && g.nodes[check].fact.has_value(); }
    if(complete) return false;
    if(inferTypeAliases() || inferTypeProperties()) return true;
    const auto count=g.nodes.size();
    std::map<Id,Type> candidates;
    const auto contextQueued=queue.size();
    auto guardFailure=[&](Id owner,const Error& error) {
        Id scope=g.nodes[owner].scope;
        if(scope!=none && g.scopes[scope].guard.symbolic() && error.category!=Diagnostic::Exhausted &&
           error.category!=Diagnostic::InvalidReplay && error.category!=Diagnostic::SearchAmbiguous)
            requestChoice(g.scopes[scope].guard,owner);
    };
    // A queried field may already be available while its siblings remain open.
    // Candidate discovery must follow actual unresolved property demands.
    std::function<bool(const Type&)> wanted=[&](const Type& value) {
        if(value.kind==Type::Unknown && value.obligation!=none) {
            const auto& property=g.nodes[value.obligation];
            return property.demanded || property.typeDemanded;
        }
        if(value.kind==Type::Array) return wanted(*value.element);
        if(value.kind==Type::Record) {
            if(value.schemaObligation!=none && g.nodes[value.schemaObligation].demanded) return true;
            for(const auto& field:value.fields) if(wanted(field)) return true;
        }
        return false;
    };
    // Retain established parts of an annotation; only its open properties take
    // initializer witnesses. Their ordinary conversion remains a value check.
    std::function<std::optional<Type>(const Type&,const Type&)> seed=[&](const Type& known,const Type& supplied)->std::optional<Type> {
        if(known.kind==Type::Unknown) return supplied;
        if(known.kind==Type::Array) {
            if(supplied.kind!=Type::Array) return {};
            auto element=seed(*known.element,*supplied.element);if(!element) return {};
            return Type::array(*element,known.size);
        }
        if(known.kind==Type::Record) {
            if(supplied.kind!=Type::Record || known.declaration!=supplied.declaration || known.fieldNames!=supplied.fieldNames) return {};
            Type result=known;
            if(known.schemaObligation!=none) {
                result.schema=supplied.schema;result.schemaObligation=supplied.schemaObligation;result.origin=supplied.origin;
            }
            for(std::size_t i=0;i<result.fields.size();++i) {
                auto field=seed(known.fields[i],supplied.fields[i]);if(!field) return {};
                result.fields[i]=*field;
            }
            return result;
        }
        return known;
    };
    for(Id id=0;id<count;++id) {
        auto& node=g.nodes[id];
        if((!node.demanded && !node.typeDemanded) || node.fact ||
           (node.inferredType && node.inferredType->settled()) || equationChecks.contains(id)) continue;
        Id supplied=none;
        if(node.rule==Rule::Input && node.key=="record-field" && node.annotation!=none) {
            const auto& requirement=g.nodes[node.annotation];
            if(requirement.rule==Rule::Type && requirement.site!=none && g.nodes[requirement.site].body!=none)
                supplied=node.inputs.at(0);
        } else if(node.rule==Rule::Global && node.signature==none) {
            const auto& declaration=g.nodes[node.site];
            if(declaration.annotation!=none && !declaration.children.empty() && hasEquationWitness(g.nodes[declaration.annotation])) {
                prepareGlobal(id);supplied=typeEquationWitness(id,node.annotation);
            }
        }
        if(supplied==none) continue;
        Type known=node.inferredType ? resolvedType(*node.inferredType) : Type{};
        if(node.inferredType && !node.demanded && !wanted(known)) continue;
        bool available=true;
        try {requireTypeProperties(known);}
        catch(const TypePropertiesRequired& pending) {
            for(Id property:pending.types) try {(void)needType(id,property);} catch(const Error& error) {
                if(error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw;
                available=false;
            }
            for(Id schema:pending.schemas) (void)need(id,schema);
        }
        if(!available) continue;
        try {
            const Type* witness=needType(id,supplied);if(!witness) continue;
            known=node.inferredType ? resolvedType(*node.inferredType) : Type{};
            auto candidate=seed(known,*witness);if(!candidate) continue;
            try {requireTypeProperties(*candidate);}
            catch(const TypePropertiesRequired& pending) {
                for(Id property:pending.types) (void)needType(id,property);
                for(Id schema:pending.schemas) (void)need(id,schema);
                continue;
            }
            if(candidate->settled() && candidate->kind!=Type::Empty &&
               !(node.rule==Rule::Global && node.scope==none && stableEquationType(*candidate))) candidates[id]=*candidate;
        } catch(const Error& error) {guardFailure(id,error);throw;}
    }
    if(queue.size()!=contextQueued) return true;
    if(candidates.empty()) return false;
    // These owners have not published settled interfaces. Keep their lexical
    // environments in the snapshot and prove candidates before exposing any
    // of them to consumers. No schema handle crosses an owning-context reset.
    std::map<Id,Id> checks;
    for(const auto& [owner,candidate]:candidates) {
        const auto& node=g.nodes[owner];
        if(node.rule==Rule::Input) {
            // A cached expansion can still point at the original field
            // promises. Rebuild the query in its unchanged lexical environment.
            const auto& original=g.nodes[node.annotation];
            Id check=g.semantic(Rule::Type,original.site,{},original.scope);
            g.nodes[check].key=original.key;g.nodes[check].slot=original.slot;
            g.nodes[check].previous=original.previous;checks[owner]=check;
        } else {
            Id check=g.semantic(Rule::Type,g.nodes[node.site].annotation,{},node.scope);
            g.nodes[check].previous=node.previous;
            if(g.nodes[g.nodes[node.site].annotation].body==none) {
                // Close only genuine generic holes from the supplied element
                // interface, while evaluating the original dimension queries.
                g.nodes[check].key="argument-type";g.nodes[check].slot=node.annotation;
                Id specialized=g.semantic(Rule::Input,node.site,{node.annotation},node.scope);
                g.nodes[specialized].annotation=check;
                check=g.semantic(Rule::TypeJoin,node.site,{specialized},node.scope);
            }
            checks[owner]=check;
        }
    }
    auto install=[&](Solver& target,Id owner,const Type& candidate) {
        auto& node=target.g.nodes[owner];
        Id supplied=target.literal(node.site,candidate,{});
        if(node.rule==Rule::Input) node.annotation=supplied;
        else {
            node.signature=supplied;
            Id requirement=target.type(target.g.nodes[node.site].annotation,node.scope,node.previous);
            target.g.nodes[requirement].key="alias-join";target.g.nodes[requirement].slot=supplied;
            if(target.g.nodes[requirement].typeDemanded) target.enqueueType(requirement);
            if(target.g.nodes[requirement].demanded) target.enqueue(requirement);
        }
        target.publishType(owner,candidate);
    };
    for(;;) {
        auto next=candidates;std::optional<Error> rejected;Id rejectedOwner=none,inexact=none;
        for(const auto& [owner,candidate]:candidates) {
            Graph graph=g;
            for(auto& [puzzle,problem]:graph.puzzles) problem=std::make_shared<ConstraintProblem>(*problem);
            // Rebuild subscriptions for this proof only. Inherited consumers
            // include value roots that must not run on provisional types.
            for(auto& node:graph.nodes) {
                node.queued=node.typeQueued=node.demanded=node.typeDemanded=false;
                node.users.clear();node.typeUsers.clear();node.valueTypeUsers.clear();
                if(node.inferredType && !node.inferredType->settled()) node.inferredType.reset();
            }
            Solver trial(graph);copyContext(trial);trial.equationChecks.clear();
            for(const auto& [member,assumed]:candidates) install(trial,member,assumed);
            Id check=checks.at(owner);
            if(steps>=g.options.maxSteps) fail(owner,"contextual type step budget exhausted",Diagnostic::Exhausted);
            graph.options.maxSteps=g.options.maxSteps-steps;
            trial.roots={{"contextual type equation",check,false,false}};
            try {
                trial.solve();const Type& required=graph.nodes[check].fact->type;
                if(!required.settled() || required.kind==Type::Empty) fail(owner,"cyclic contextual equation requires a settled interface",Diagnostic::Unresolved);
                if(!(candidate==required)) inexact=owner;
                // common preserves the candidate's origins recursively. Never
                // export schema handles created only inside this proof graph.
                next[owner]=common(candidate,required,owner);
            } catch(const Error& error) {
                if(error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw;
                if(!rejected) {rejected=Error(g.nodes[owner].span,"cyclic contextual type requirement: "+std::string(error.what()),error.category);rejectedOwner=owner;}
            }
            steps+=trial.steps;
        }
        if(next!=candidates) {candidates=std::move(next);continue;}
        if(rejected) {
            Id scope=g.nodes[rejectedOwner].scope;
            if(scope!=none && g.scopes[scope].guard.symbolic()) requestChoice(g.scopes[scope].guard,rejectedOwner);
            throw *rejected;
        }
        if(inexact!=none) {
            Id scope=g.nodes[inexact].scope;
            if(scope!=none && g.scopes[scope].guard.symbolic()) requestChoice(g.scopes[scope].guard,inexact);
            fail(inexact,"cyclic requirements do not admit their supplied interfaces");
        }
        for(const auto& [owner,candidate]:candidates) {
            install(*this,owner,candidate);equationChecks[owner]=checks.at(owner);
            enqueueType(owner);if(g.nodes[owner].demanded) enqueue(owner);
        }
        return true;
    }
}
bool Solver::verifyTypeEquations() {
    TypeDemand demandType(processingType);
    const auto queued=queue.size();
    Id pending=none;
    for(const auto& [owner,check]:equationChecks) {
        const Fact* result=need(owner,check);
        const auto& node=g.nodes[owner];
        Id candidate=node.rule==Rule::Global ? node.signature : node.annotation;
        if(result && !(result->type==g.nodes[candidate].fact->type))
            fail(owner,"cyclic type candidate does not satisfy its original equation");
        if(!result) pending=owner;
    }
    if(queue.size()==queued && pending!=none) fail(pending,"unresolved cyclic type equation validation",Diagnostic::Unresolved);
    return queue.size()!=queued;
}
void Solver::reviseTypeEquations(const TypeEquationsRequired& request,const Graph& base,const Solver& context) {
    auto candidates=typeEquations;
    for(const auto& [declaration,witness]:request.witnesses) candidates[declaration]=witness;
    for(;;) {
        auto next=candidates;
        std::optional<Error> rejected;
        bool exact=true;
        for(const auto& [declaration,candidate]:candidates) {
            // Each original equation is checked independently under
            // the same candidate vector. A temporary error must not
            // prevent another equation from contributing a revision.
            Graph graph=base;
            for(auto& [puzzle,problem]:graph.puzzles) problem=std::make_shared<ConstraintProblem>(*problem);
            Solver trial(graph);context.copyContext(trial);
            trial.typeEquations=candidates;trial.equationChecks.clear();trial.typeEquationTrial=true;
            if(steps>=g.options.maxSteps) fail(declaration,"cyclic type resolution step budget exhausted",Diagnostic::Exhausted);
            graph.options.maxSteps=g.options.maxSteps-steps;
            Id check=graph.semantic(Rule::Type,graph.nodes[declaration].annotation);
            trial.roots={{graph.nodes[declaration].text,check,false,false}};
            try {
                trial.solve();
                const Type& required=graph.nodes[check].fact->type;
                if(!stableEquationType(required)) fail(declaration,"cyclic type equation requires a stable scalar or array interface",Diagnostic::Unresolved);
                exact=exact && candidate==required;
                next[declaration]=common(candidate,required,declaration);
            } catch(const Error& error) {
                if(error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw;
                if(!rejected) rejected=Error(g.nodes[declaration].span,"cyclic type requirement: "+std::string(error.what()),error.category);
            }
            steps+=trial.steps;
        }
        if(next!=candidates) {candidates=std::move(next);continue;}
        if(rejected) throw *rejected;
        if(!exact) fail(request.witnesses.begin()->first,"cyclic type requirements do not admit their initializer interfaces");
        typeEquations=std::move(candidates);return;
    }
}
} // namespace vx
