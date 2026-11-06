#include "vexel.hpp"
#include <algorithm>

namespace vx {
void Solver::sortRule(Id id) {
    auto& n=g.nodes[id];
    const Fact* input=need(id,n.inputs[0]);if(!input) return;
    if(input->type.kind!=Type::Array || !input->value.known()) fail(id,"sorted iteration requires compile-time array values");
    const Type itemType=*input->type.element;
    auto less=[&](const Value& left,const Value& right) {
        auto order=compareValues(itemType,left,right);
        if (!order) fail(id,"sorted iteration has an unordered NaN comparison");
        return *order<0;
    };
    auto candidates=functions(".<",n.scope);
    std::erase_if(candidates,[&](const auto& candidate) {
        const auto& declaration=g.nodes[candidate.declaration];
        return declaration.receiverCount!=1 || declaration.children.size()!=2;
    });
    if(candidates.empty()) {
        auto values=input->value.elements;
        std::stable_sort(values.begin(),values.end(),less);
        publish(id,{input->type,Value::array(std::move(values)),Effect::Pure});return;
    }
    auto inside=[&](Id scope) {
        std::set<Id> seen;
        while(scope!=none && seen.insert(scope).second) {
            if(scope==n.lexicalScope) return true;
            const auto& frame=g.scopes[scope];
            scope=g.nodes[frame.node].slot!=none ? frame.caller : frame.parent!=none ? frame.parent : frame.caller;
        }
        return false;
    };
    // Stable insertion order makes the comparison requests independent of the
    // worklist schedule. Each method call is an ordinary graph invocation on a
    // private receiver copy; source values and enclosing storage are unchanged.
    std::size_t cursor=n.key.empty() ? 0 : std::stoull(n.key);
    while(true) {
        if(n.expansion!=none) {
            const Fact* comparison=need(id,n.expansion);if(!comparison) return;
            if(comparison->value.symbolic()) requestChoice(comparison->value,id);
            if(comparison->type.kind!=Type::Boolean || comparison->value.kind!=Value::Number || comparison->effect>=Effect::Control)
                fail(id,"sorted comparison must produce a pure compile-time Boolean");
            for(Id at=n.annotation;at<g.nodes.size();++at) {
                const auto& operation=g.nodes[at];
                if(operation.rule==Rule::Store && operation.fact && operation.completion.normal && inside(operation.scope) && !inside(g.nodes[operation.slot].scope))
                    fail(id,"sorted comparison cannot modify enclosing storage");
            }
            if(g.nodes[n.expansion].rule==Rule::Binary)
                (void)less(g.nodes[n.body].fact->value,g.nodes[n.branches[cursor-1]].fact->value);
            bool before=comparison->value.number!=0;n.expansion=none;
            if(before) --cursor;
            else {
                n.branches.insert(n.branches.begin()+static_cast<std::ptrdiff_t>(cursor),n.body);
                n.body=none;++n.phase;
            }
        }
        if(n.phase==input->type.size) {
            std::vector<Value> result;
            for(Id element:n.branches) result.push_back(g.nodes[element].fact->value);
            publish(id,{input->type,Value::array(std::move(result)),Effect::Pure});return;
        }
        if(n.body==none) {
            n.body=literal(n.site,itemType,input->value.elements[n.phase]);cursor=n.branches.size();
        }
        if(cursor==0) {
            n.branches.insert(n.branches.begin(),n.body);n.body=none;++n.phase;continue;
        }
        n.annotation=g.nodes.size();
        n.key=std::to_string(cursor);
        Id frame=g.scope(n.scope,n.site,n.identity+"/comparison/"+std::to_string(n.phase)+"/"+std::to_string(cursor));
        n.lexicalScope=frame;
        Id entry=g.semantic(Rule::Entry,n.site,{},frame);g.nodes[entry].previous=n.previous;
        Id receiver=g.semantic(Rule::Slot,n.site,{n.body},frame);
        Id store=g.semantic(Rule::Store,n.site,{entry,n.body},frame);g.nodes[store].slot=receiver;g.nodes[store].previous=entry;
        Id alias=g.semantic(Rule::Alias,n.site,{},frame);g.nodes[alias].slot=receiver;g.nodes[alias].key="temporary";
        Node site;site.syntax=Syntax::Call;site.span=n.span;site.text=".<";site.key="fallback-binary";
        site.identity=n.identity+"/less/"+std::to_string(n.phase)+"/"+std::to_string(cursor);
        Id source=g.add(std::move(site));
        Id comparison=g.semantic(Rule::Invoke,source,{n.body,n.branches[cursor-1]},frame);
        auto& call=g.nodes[comparison];call.text=".<";call.receiverCount=1;call.receivers={alias};call.previous=store;
        call.lexicalScope=candidates[0].scope;
        for(const auto& candidate:candidates) call.candidates.push_back(candidate.declaration);
        n.expansion=comparison;(void)need(id,comparison);return;
    }
}
} // namespace vx
