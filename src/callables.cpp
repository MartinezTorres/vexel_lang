#include "vexel.hpp"
#include <algorithm>

namespace vx {
std::optional<Solver::Place> Solver::place(Id site, Id scopeId, Id state) {
    std::vector<Id> path;
    Id base = site;
    while (g.nodes[base].syntax == Syntax::Member || g.nodes[base].syntax == Syntax::Index) {
        path.push_back(base); base = g.nodes[base].children[0];
    }
    if (g.nodes[base].syntax != Syntax::Name) return {};
    Id root = binding(g.nodes[base].text,scopeId);
    if (root == none || (g.nodes[root].rule!=Rule::Global && g.nodes[root].readonly)) return {};
    std::vector<Id> offsets;
    if (g.nodes[root].rule == Rule::Alias) { offsets = g.nodes[root].inputs; root = g.nodes[root].slot; }
    Id value = load(root,state,site);
    for (Id offset : offsets) value = g.semantic(Rule::Index,site,{value,offset},scopeId);
    std::reverse(path.begin(),path.end());
    for (Id component : path) {
        const auto& p = g.nodes[component]; Id offset;
        if (p.syntax == Syntax::Member) {
            offset = g.semantic(Rule::FieldIndex,component,{value},scopeId); g.nodes[offset].text = p.text;
        } else {
            Built index = build(p.children[1],scopeId,state); state = index.state; offset = index.value;
            // Index evaluation may change the containing value.
            value = load(root,state,site);
            for (Id previous : offsets) value = g.semantic(Rule::Index,site,{value,previous},scopeId);
        }
        offsets.push_back(offset);
        value = g.semantic(Rule::Index,component,{value,offset},scopeId);
        state = finishExpression(value,state).state;
    }
    return Place{root,value,state,std::move(offsets)};
}
Built Solver::callExpression(Id site, Id scopeId, Id state) {
    const Node& s = g.nodes[site]; Id initial = state;
    Id iteration=none,bodyBindings=scopeId;
    if(s.key=="fallback-iterate") {
        iteration=g.semantic(Rule::CustomIteration,site,{},scopeId);g.nodes[iteration].previous=state;
        bodyBindings=g.scope(scopeId,site,g.identity(site)+"/iteration-body-bindings");
        g.scopes[bodyBindings].loop=iteration;
    }
    auto candidates = functions(s.text,scopeId);
    std::erase_if(candidates,[&](const auto& fn) {
        const auto& declaration = g.nodes[fn.declaration];
        return declaration.children.size() != s.children.size() || declaration.receiverCount != s.receiverCount;
    });
    std::vector<bool> deferred(s.children.size(),false);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& fn = g.nodes[candidates[i].declaration];
        for (std::size_t j = 0; j < deferred.size(); ++j) {
            bool mode = g.nodes[fn.children[j]].expression;
            if (i && mode != deferred[j]) fail(site,"overloads must agree on expression parameter positions");
            deferred[j] = mode;
        }
    }
    std::vector<Id> args, receivers;
    for (std::size_t i = 0; i < s.children.size(); ++i) {
        if (i < s.receiverCount) {
            auto receiver = place(s.children[i],scopeId,state);
            bool temporaryReceiver=!receiver;
            if (!receiver) {
                Built expression = build(s.children[i],scopeId,state); state = expression.state;
                Id temporary = g.semantic(Rule::Slot,site,{expression.value},scopeId);
                Id store = g.semantic(Rule::Store,site,{state,expression.value},scopeId);
                g.nodes[store].slot = temporary; g.nodes[store].previous = state; state = store;
                receiver = Place{temporary,expression.value,state,{}};
            }
            state = receiver->state; args.push_back(receiver->value);
            Id alias = g.semantic(Rule::Alias,site,receiver->offsets,scopeId);
            g.nodes[alias].slot = receiver->slot; receivers.push_back(alias);
            if(temporaryReceiver) g.nodes[alias].key="temporary";
        } else if (deferred[i]) {
            Id argument = g.semantic(Rule::Deferred,s.children[i],{},snapshot(bodyBindings,site));
            g.nodes[argument].previous = state; args.push_back(argument);
        } else { Built arg = build(s.children[i],scopeId,state); state = arg.state; args.push_back(arg.value); }
    }
    Id invocation = g.semantic(Rule::Invoke,site,std::move(args),snapshot(scopeId,site));
    auto& call = g.nodes[invocation]; call.text = s.text; call.previous = state;
    call.receiverCount = s.receiverCount; call.receivers = std::move(receivers);
    if (!candidates.empty()) call.lexicalScope = candidates[0].scope;
    for (const auto& fn : candidates) call.candidates.push_back(fn.declaration);
    if (s.syntax == Syntax::MethodProbe) {
        call.rule = Rule::CallProbe;
        return finishExpression(invocation,initial);
    }
    Id completed=g.semantic(Rule::StateOf,site,{invocation},scopeId);
    if(iteration!=none) {
        g.nodes[iteration].inputs={completed};
        g.nodes[iteration].endState=resume(completed,iteration,"loop",site,scopeId);
        return {iteration,g.semantic(Rule::StateOf,site,{iteration},scopeId)};
    }
    return {invocation,completed};
}
} // namespace vx
