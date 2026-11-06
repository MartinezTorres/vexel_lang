#include "vexel.hpp"
#include "constraints.hpp"
#include <algorithm>

namespace vx {
bool Solver::inferTypeAliases() {
    // Discover equalities by ordinary interface evaluation with opaque type
    // variables. No initializer interface (including its nominal shape) is an
    // assumption in this proof. A conversion, dispatch, or value-dependent
    // operation that needs the variable's structure cannot prove an equality.
    std::map<Id,Id> requirements;
    std::set<std::pair<Rule,Id>> demandedGroups;
    for(Id id=0;id<g.nodes.size();++id) {
        const auto& node=g.nodes[id];
        if(node.fact || node.inferredType || equationChecks.contains(id) || typeAliasProofs.contains(id)) continue;
        Id annotation=none;
        if(node.rule==Rule::Input && node.key=="record-field" && node.annotation!=none) {
            const auto& requirement=g.nodes[node.annotation];
            if(requirement.rule==Rule::Type) annotation=requirement.site;
        } else if(node.rule==Rule::Global && node.signature==none) annotation=g.nodes[node.site].annotation;
        if(annotation==none || g.nodes[annotation].body==none || !g.nodes[annotation].children.empty()) continue;
        requirements[id]=annotation;
        if(node.demanded || node.typeDemanded) demandedGroups.insert({node.rule,node.scope});
    }
    if(demandedGroups.empty()) return false;
    std::erase_if(requirements,[&](const auto& item) {
        const auto& node=g.nodes[item.first];return !demandedGroups.contains({node.rule,node.scope});
    });
    std::map<Id,Id> aliases;
    for(const auto& [owner,annotation]:requirements) {
        Graph graph=g;
        for(auto& [puzzle,problem]:graph.puzzles) { (void)puzzle;problem=std::make_shared<ConstraintProblem>(*problem); }
        for(auto& node:graph.nodes) {
            node.queued=node.typeQueued=node.demanded=node.typeDemanded=false;
            node.users.clear();node.typeUsers.clear();node.valueTypeUsers.clear();
            if(node.inferredType && !node.inferredType->settled()) node.inferredType.reset();
        }
        Solver trial(graph);copyContext(trial);
        for(const auto& [member,source]:requirements) {
            (void)source;auto& node=graph.nodes[member];
            node.rule=Rule::Input;node.key="opaque-type";
            node.inputs={member};node.annotation=node.expansion=node.previous=none;
            node.typeError.reset();Type variable;variable.obligation=member;node.inferredType=variable;
        }
        const auto& original=g.nodes[owner];
        Id scope=original.scope,state=original.previous;
        if(original.rule==Rule::Input) {
            const auto& requirement=g.nodes[original.annotation];scope=requirement.scope;state=requirement.previous;
        }
        Id check=graph.semantic(Rule::Type,annotation,{},scope);graph.nodes[check].previous=state;
        if(steps>=g.options.maxSteps) fail(owner,"type equality proof step budget exhausted",Diagnostic::Exhausted);
        graph.options.maxSteps=g.options.maxSteps-steps;
        trial.enqueueType(check);
        try {
            // Drain propagation only. Other inference would replace the
            // opaque variables by witnesses and destroy the universal proof.
            trial.drainWorklist();
            for(const auto& node:graph.nodes) if(node.typeDemanded && node.typeError &&
                (node.typeError->category==Diagnostic::Exhausted || node.typeError->category==Diagnostic::InvalidReplay ||
                 node.typeError->category==Diagnostic::SearchAmbiguous)) throw *node.typeError;
            const auto& result=graph.nodes[check].inferredType;
            bool opaqueValues=false;
            for(const auto& [member,source]:requirements) { (void)source;opaqueValues=opaqueValues || graph.nodes[member].demanded; }
            if(!opaqueValues && !graph.nodes[check].typeError && result && result->kind==Type::Unknown && requirements.contains(result->obligation)) {
                const auto& target=g.nodes[result->obligation];
                if(original.rule==target.rule && original.scope==target.scope) aliases[owner]=result->obligation;
            }
        } catch(const Error& error) {
            if(error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw;
        } catch(const ChoiceRequired&) {
            // A choice-dependent interface belongs to ordinary branch search.
        } catch(const TypeEquationsRequired&) {
        } catch(const SignatureRevision&) {
        } catch(const RecursionTypeRequired&) {
        } catch(const RecursionWidening&) {
        } catch(const RecursionEffectRequired&) {
        }
        steps+=trial.steps;
    }
    std::map<Id,std::vector<Id>> adjacent;
    for(const auto& [owner,target]:aliases) {adjacent[owner].push_back(target);adjacent[target].push_back(owner);}
    std::set<Id> seen;bool progress=false;
    for(const auto& [start,target]:aliases) {
        (void)target;if(seen.contains(start)) continue;
        std::vector<Id> pending{start};std::set<Id> component;
        bool needed=false;
        while(!pending.empty()) {
            Id member=pending.back();pending.pop_back();if(!component.insert(member).second) continue;
            seen.insert(member);needed=needed || g.nodes[member].demanded || g.nodes[member].typeDemanded;
            for(Id neighbor:adjacent[member]) pending.push_back(neighbor);
        }
        if(!needed || std::any_of(component.begin(),component.end(),[&](Id member){return !aliases.contains(member);})) continue;
        // A closed equality component owns every initializer witness, even
        // when only one member is demanded. Leave its schema and each field as
        // separate obligations in the common-property join.
        std::vector<Id> inputs;
        for(Id member:component) {
            auto& node=g.nodes[member];
            if(node.rule==Rule::Global) {prepareGlobal(member);if(node.annotation!=none) inputs.push_back(node.annotation);}
            else inputs.push_back(node.inputs.at(0));
        }
        if(inputs.empty()) continue;
        Id joined=g.semantic(Rule::TypeJoin,g.nodes[start].site,std::move(inputs),g.nodes[start].scope);
        g.nodes[joined].key="alias-properties";
        for(Id member:component) {
            auto& node=g.nodes[member];Id requirement;
            if(node.rule==Rule::Global) {
                node.signature=joined;requirement=type(g.nodes[node.site].annotation,node.scope,node.previous);
            } else requirement=node.annotation;
            typeAliasProofs[member]={aliases.at(member),joined,requirement};
            g.nodes[requirement].key="alias-join";g.nodes[requirement].slot=joined;
            if(g.nodes[requirement].typeDemanded) enqueueType(requirement);
            if(g.nodes[requirement].demanded) enqueue(requirement);
            if(node.typeDemanded) enqueueType(member);
            if(node.demanded) enqueue(member);
        }
        progress=true;
    }
    return progress;
}
} // namespace vx
