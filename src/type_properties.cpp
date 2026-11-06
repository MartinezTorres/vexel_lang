#include "vexel.hpp"
#include "constraints.hpp"
#include <algorithm>

namespace vx {
namespace {
struct TypeDemand {
    bool& state;bool previous;
    explicit TypeDemand(bool& value):state(value),previous(value) {state=true;}
    ~TypeDemand(){state=previous;}
};
struct StepCharge {
    std::size_t& total;const std::size_t& used;
    ~StepCharge(){total+=used;}
};
Type replaced(Type type,const std::vector<std::size_t>& path,std::size_t at,const Type& property) {
    if(at==path.size()) return property;
    if(type.kind==Type::Array) type.element=std::make_shared<Type>(replaced(*type.element,path,at+1,property));
    else type.fields.at(path[at])=replaced(type.fields.at(path[at]),path,at+1,property);
    return type;
}
}
bool Solver::inferTypeProperties() {
    TypeDemand typeDemand(processingType);
    std::map<PropertyKey,Type> candidates;
    std::map<Id,Type> templates;
    std::map<PropertyKey,Id> pendingProperties;
    const auto queued=queue.size();
    const auto count=g.nodes.size();
    for(Id owner=0;owner<count;++owner) {
        const auto& node=g.nodes[owner];
        if(node.fact || !node.inferredType || node.inferredType->settled() || (!node.demanded && !node.typeDemanded) || equationChecks.contains(owner)) continue;
        Id supplied=none,annotation=none,scope=node.scope,state=node.previous;
        if(node.rule==Rule::Input && node.key=="record-field" && node.annotation!=none) {
            const auto& requirement=g.nodes[node.annotation];
            if(requirement.rule!=Rule::Type || requirement.key=="alias-join") continue;
            supplied=node.inputs.at(0);annotation=requirement.site;scope=requirement.scope;state=requirement.previous;
        } else if(node.rule==Rule::Global && node.signature==none) {
            const auto& declaration=g.nodes[node.site];
            if(declaration.children.empty()) continue;
            prepareGlobal(owner);supplied=node.annotation;annotation=declaration.annotation;
        }
        if(supplied==none || annotation==none || g.nodes[annotation].body==none || !g.nodes[annotation].children.empty()) continue;
        Type known=resolvedType(*node.inferredType);
        if(known.kind!=Type::Record && known.kind!=Type::Array) continue;
        templates[owner]=known;
        std::function<void(const Type&,std::vector<std::size_t>)> discover=[&](const Type& property,std::vector<std::size_t> path) {
            if(property.kind==Type::Array) {path.push_back(0);discover(*property.element,path);return;}
            if(property.kind==Type::Record) {
                for(std::size_t i=0;i<property.fields.size();++i) {auto child=path;child.push_back(i);discover(property.fields[i],child);}return;
            }
            if(property.kind!=Type::Unknown || property.obligation==none) return;
            const auto& pending=g.nodes[property.obligation];
            const bool wanted=node.demanded || pending.demanded || pending.typeDemanded;
            PropertyKey key{owner,path};
            if(propertyProofs.contains(key) || propertyAssumptions.contains(key)) return;
            pendingProperties[key]=property.obligation;
            auto project=[&](Id value) {
                for(std::size_t position:path) {
                    Id index=literal(node.site,Type::simple(Type::Exact),Value::integer(position));
                    value=g.semantic(Rule::Index,node.site,{value,index},node.scope);
                }
                return value;
            };
            if(!propertyNodes.contains(key)) {
                Id original=g.semantic(Rule::Type,annotation,{},scope);g.nodes[original].previous=state;
                if(node.rule==Rule::Input) {
                    const auto& requirement=g.nodes[node.annotation];
                    g.nodes[original].key=requirement.key;g.nodes[original].slot=requirement.slot;
                }
                Id check=g.semantic(Rule::TypeJoin,node.site,{project(original)},node.scope);
                propertyNodes[key]={project(supplied),check};
            }
            if(!wanted) return;
            const Type* witness=needType(owner,propertyNodes.at(key).witness);
            if(!witness) return;
            Type candidate=resolvedType(*witness);
            if(!candidate.settled()) {
                try {requireTypeProperties(candidate);}
                catch(const TypePropertiesRequired& waiting) {
                    for(Id part:waiting.types) (void)needType(owner,part);
                    // A property proof must not turn into a demand for a
                    // whole record's captured schema merely to find a seed.
                }
                return;
            }
            if(candidate.kind!=Type::Empty && stableEquationType(candidate)) candidates[key]=candidate;
        };
        try {discover(known,{});}
        catch(const Error& error) {
            if(error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw;
            if(node.scope!=none && g.scopes[node.scope].guard.symbolic()) requestChoice(g.scopes[node.scope].guard,owner);
            throw;
        }
    }
    if(queue.size()!=queued) return true;
    if(candidates.empty()) return false;
    auto install=[&](Solver& target,const std::map<PropertyKey,Type>& values) {
        std::map<Id,Type> interfaces;
        for(const auto& [key,value]:values) {
            if(!interfaces.contains(key.first)) interfaces[key.first]=templates.at(key.first);
            interfaces.at(key.first)=replaced(interfaces.at(key.first),key.second,0,value);
            target.propertyAssumptions.insert(key);
        }
        for(const auto& [owner,interface]:interfaces) {
            if(target.propertyTrial) {
                auto& node=target.g.nodes[owner];
                Id descriptor=target.literal(node.site,interface,{});
                if(node.rule==Rule::Input) node.annotation=descriptor;
                else {
                    node.signature=descriptor;
                    Id requirement=target.type(target.g.nodes[node.site].annotation,node.scope,node.previous);
                    target.g.nodes[requirement].key="alias-join";target.g.nodes[requirement].slot=descriptor;
                }
            }
            target.publishType(owner,interface);
        }
    };
    for(;;) {
        auto next=candidates;std::optional<Error> rejected;Id rejectedOwner=none,inexact=none;
        std::set<PropertyKey> needed;
        for(const auto& [key,candidate]:candidates) { (void)candidate;needed.insert(key); }
        auto reject=[&](const PropertyKey& key,const Error& error) {
            if(error.category==Diagnostic::Exhausted || error.category==Diagnostic::InvalidReplay || error.category==Diagnostic::SearchAmbiguous) throw error;
            if(!rejected) {rejected=Error(g.nodes[key.first].span,"type property equation: "+std::string(error.what()),error.category);rejectedOwner=key.first;}
        };
        auto join=[&](const PropertyKey& key,const Type& value) {
            try {
                if(auto prior=next.find(key);prior!=next.end()) prior->second=common(prior->second,value,key.first);
                else next[key]=value;
            } catch(const Error& error) {reject(key,error);}
        };
        auto prove=[&](const PropertyKey& key, bool witness)->std::optional<Type> {
            Graph graph=g;
            for(auto& [puzzle,problem]:graph.puzzles) {(void)puzzle;problem=std::make_shared<ConstraintProblem>(*problem);}
            for(auto& node:graph.nodes) {
                node.queued=node.typeQueued=node.demanded=node.typeDemanded=false;
                node.users.clear();node.typeUsers.clear();node.valueTypeUsers.clear();
                if(node.inferredType && !node.inferredType->settled()) node.inferredType.reset();
            }
            Solver trial(graph);StepCharge charge{steps,trial.steps};
            copyContext(trial);trial.equationChecks.clear();trial.propertyProofs.clear();trial.propertyTrial=true;
            install(trial,candidates);
            if(steps>=g.options.maxSteps) fail(key.first,"type property proof step budget exhausted",Diagnostic::Exhausted);
            graph.options.maxSteps=g.options.maxSteps-steps;
            const auto& property=propertyNodes.at(key);
            Id check=witness ? graph.semantic(Rule::TypeJoin,g.nodes[key.first].site,{property.witness},g.nodes[key.first].scope) : property.check;
            trial.roots={{witness ? "type property witness" : "type property equation",check,false,false}};
            std::optional<Type> result;
            try {
                trial.solve();const Type& required=graph.nodes[check].fact->type;
                if(!stableEquationType(required) || !required.settled() || required.kind==Type::Empty)
                    fail(key.first,"type property equation did not establish its requested interface",Diagnostic::Unresolved);
                result=required;
            } catch(const Error& error) {reject(key,error);}
            // Selection under assumptions can expose another original property
            // even after its equation has acquired a concrete provisional type.
            // Its initializer still contributes a bound to the joint proof.
            for(const auto& [dependency,obligation]:pendingProperties) {
                const auto& node=graph.nodes[obligation];
                if(node.demanded || node.typeDemanded || !node.typeUsers.empty()) needed.insert(dependency);
            }
            return result;
        };
        for(const auto& [key,candidate]:candidates) {
            if(auto required=prove(key,false)) {
                if(!(*required==candidate)) inexact=key.first;
                join(key,*required);
            }
        }
        std::set<PropertyKey> witnessed;
        while(witnessed.size()!=needed.size()) {
            PropertyKey key=*std::find_if(needed.begin(),needed.end(),[&](const PropertyKey& part) {return !witnessed.contains(part);});
            witnessed.insert(key);
            if(auto supplied=prove(key,true)) join(key,*supplied);
        }
        if(next!=candidates) {candidates=std::move(next);continue;}
        Id failed=rejected ? rejectedOwner : inexact;
        if(failed!=none) {
            Id scope=g.nodes[failed].scope;
            if(scope!=none && g.scopes[scope].guard.symbolic()) requestChoice(g.scopes[scope].guard,failed);
            if(rejected) throw *rejected;
            fail(failed,"type property requirements do not admit their supplied interfaces");
        }
        install(*this,candidates);
        for(const auto& [key,candidate]:candidates) {
            propertyAssumptions.erase(key);propertyProofs[key]={propertyNodes.at(key).check,candidate};
            enqueueType(key.first);if(g.nodes[key.first].demanded) enqueue(key.first);
        }
        return true;
    }
}
bool Solver::verifyTypeProperties() {
    TypeDemand typeDemand(processingType);const auto queued=queue.size();Id pending=none;
    for(const auto& [key,proof]:propertyProofs) {
        const Fact* required=need(key.first,proof.check);
        if(!required) pending=key.first;
        else if(!(required->type==proof.candidate)) fail(key.first,"type property does not satisfy its original equation");
        const Type* witness=needType(key.first,propertyNodes.at(key).witness);
        if(!witness) pending=key.first;
        else {
            Type supplied=resolvedType(*witness);
            if(!supplied.settled()) pending=key.first;
            else if(!(common(proof.candidate,supplied,key.first)==proof.candidate))
                fail(key.first,"type property does not admit its initializer witness");
        }
    }
    if(queue.size()==queued && pending!=none) fail(pending,"unresolved type property equation validation",Diagnostic::Unresolved);
    return queue.size()!=queued;
}
} // namespace vx
