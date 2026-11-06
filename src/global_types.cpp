#include "vexel.hpp"
#include <algorithm>

namespace vx {
void Solver::prepareGlobal(Id id) {
    auto& n=g.nodes[id];const auto& declaration=g.nodes[n.site];
    if(n.expanded || declaration.children.empty()) return;
    n.expanded=true;n.text=declaration.text;
    Id frame=g.scope(n.scope,n.site,n.identity+"/global");
    Id entry=n.previous==none ? g.semantic(Rule::Entry,n.site,{},frame) : n.previous;
    Built initial=build(declaration.children[0],frame,entry);
    n.annotation=initial.value; // Unadapted initializer type is an independent anchor.
    Id value=declaration.annotation==none ? initial.value : adapt(initial.value,argumentType(declaration.annotation,n.scope,n.previous,initial.value),n.site);
    n.expansion=g.semantic(Rule::Finish,n.site,{value,initial.state},frame);
}
void Solver::globalTypeAliases(Id id) {
    auto& n=g.nodes[id];if(n.signature!=none || n.key=="global-alias-checked") return;
    if(n.scope==none) if(auto assumed=typeEquations.find(n.site);assumed!=typeEquations.end()) {
        const auto& definition=g.nodes[n.site];
        n.signature=literal(n.site,assumed->second,{});
        Id requirement=type(definition.annotation,n.scope,n.previous);
        g.nodes[requirement].key="alias-join";g.nodes[requirement].slot=n.signature;
        if(!typeEquationTrial) {
            Id check=g.semantic(Rule::Type,definition.annotation,{},n.scope);
            g.nodes[check].previous=n.previous;equationChecks[id]=check;
        }
        return;
    }
    n.key="global-alias-checked";
    const auto& definition=g.nodes[n.site];
    if(definition.annotation==none) return;
    const auto& ownAnnotation=g.nodes[definition.annotation];
    if(ownAnnotation.body==none || !ownAnnotation.children.empty()) return;
    // A lexical declaration group is complete before its obligations run.
    // Aliases from a nested group consume their outer group's answer; they
    // cannot change a parent declaration merely by becoming demanded later.
    std::set<Id> group;
    if(n.scope==none) {
        for(const auto& [name,definitions]:g.symbols) {
            (void)name;
            if(definitions.size()==1 && g.nodes[definitions[0]].syntax==Syntax::Global) group.insert(global(definitions[0]));
        }
    } else {
        for(const auto& [name,value]:g.scopes[n.scope].constants) {
            (void)name;
            if(g.nodes[value].rule==Rule::Global && g.nodes[value].scope==n.scope) group.insert(value);
        }
    }
    std::map<Id,Id> aliases;
    std::map<Id,std::vector<Id>> adjacent;
    for(Id member:group) {
        const auto& variable=g.nodes[member];const auto& declaration=g.nodes[variable.site];
        if(declaration.annotation==none) continue;
        const auto& annotation=g.nodes[declaration.annotation];
        if(annotation.body==none || !annotation.children.empty()) continue;
        Id query=annotation.body;
        while(g.nodes[query].syntax==Syntax::Group) query=g.nodes[query].children[0];
        if(g.nodes[query].syntax!=Syntax::Name) continue;
        Id target=binding(g.nodes[query].text,variable.scope);
        if(target==none || !group.contains(target)) continue;
        aliases[member]=target;adjacent[member].push_back(target);adjacent[target].push_back(member);
    }
    std::set<Id> component;std::vector<Id> pending{id};
    while(!pending.empty()) {
        Id member=pending.back();pending.pop_back();if(!component.insert(member).second) continue;
        for(Id neighbor:adjacent[member]) pending.push_back(neighbor);
    }
    if(std::any_of(component.begin(),component.end(),[&](Id member){return !aliases.contains(member);})) return;
    std::vector<Id> inputs;
    for(Id member:component) {
        if(g.nodes[g.nodes[member].site].children.empty()) continue;
        prepareGlobal(member);inputs.push_back(g.nodes[member].annotation);
    }
    if(inputs.empty()) return; // No witness: preserve the unresolved cycle diagnostic.
    Id joined=g.semantic(Rule::TypeJoin,n.site,std::move(inputs),n.scope);
    g.nodes[joined].key="alias-properties";
    for(Id member:component) {
        auto& variable=g.nodes[member];variable.signature=joined;variable.key="global-alias-checked";
        Id annotation=type(g.nodes[variable.site].annotation,variable.scope,variable.previous);
        g.nodes[annotation].key="alias-join";g.nodes[annotation].slot=joined;
        if(g.nodes[annotation].typeDemanded) enqueueType(annotation);
        if(g.nodes[annotation].demanded) enqueue(annotation);
    }
}
} // namespace vx
