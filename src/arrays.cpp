#include "vexel.hpp"
#include <algorithm>

namespace vx {
void Solver::broadcastRule(Id id, bool typesOnly) {
    auto& n = g.nodes[id];
    const Type* left = needType(id, n.inputs[0]); const Type* right = needType(id, n.inputs[1]);
    if (!left || !right) return;
    if (n.expansion == none) {
        auto candidates=functions("."+n.text.substr(1),n.scope);
        std::erase_if(candidates,[&](const auto& candidate) {
            const auto& function=g.nodes[candidate.declaration];
            return function.receiverCount!=1 || function.children.size()!=2;
        });
        Id state=n.previous;
        if(state==none) state=g.semantic(Rule::Entry,n.site,{},n.scope);
        auto rank = [](Type t) { std::size_t result = 0; while (t.kind == Type::Array) { ++result; t = Type(*t.element); } return result; };
        std::function<Id(Id,const Type&,Id,const Type&)> expand = [&](Id a, const Type& ta, Id b, const Type& tb) -> Id {
            if (ta.kind != Type::Array && tb.kind != Type::Array) {
                if(!candidates.empty()) {
                    Id frame=g.scope(n.scope,n.site,n.identity+"/element/"+std::to_string(g.nodes.size()));
                    Id slot=g.semantic(Rule::Slot,n.site,{a},frame);
                    Id store=g.semantic(Rule::Store,n.site,{state,a},frame);g.nodes[store].slot=slot;g.nodes[store].previous=state;
                    Id receiver=g.semantic(Rule::Alias,n.site,{},frame);g.nodes[receiver].slot=slot;g.nodes[receiver].key="temporary";
                    Node source;source.syntax=Syntax::Call;source.span=n.span;source.text="."+n.text.substr(1);
                    source.key="fallback-binary";source.identity=n.identity+"/element-call/"+std::to_string(g.nodes.size());
                    Id site=g.add(std::move(source));
                    Id operation=g.semantic(Rule::Invoke,site,{a,b},frame);
                    auto& call=g.nodes[operation];call.text="."+n.text.substr(1);call.previous=store;
                    call.receiverCount=1;call.receivers={receiver};call.lexicalScope=candidates[0].scope;
                    for(const auto& candidate:candidates) call.candidates.push_back(candidate.declaration);
                    state=g.semantic(Rule::StateOf,site,{operation},frame);
                    return operation;
                }
                Id operation = g.semantic(Rule::Binary, n.site, {a,b}, n.scope);
                g.nodes[operation].text = n.text.substr(1); return operation;
            }
            auto ra = rank(ta), rb = rank(tb);
            bool indexA = ra >= rb, indexB = rb >= ra;
            auto na = indexA ? ta.size : 1, nb = indexB ? tb.size : 1;
            if (na != nb && na != 1 && nb != 1) fail(id, "incompatible broadcast shapes: " + ta.str() + " and " + tb.str());
            auto count = na == 1 ? nb : na;
            std::vector<Id> items;
            for (std::size_t i = 0; i < count; ++i) {
                auto item = [&](Id base, bool indexed, std::size_t size) {
                    if (!indexed) return base;
                    Id offset = literal(n.site, Type::simple(Type::Exact), Value::integer(size == 1 ? 0 : i));
                    return g.semantic(Rule::Index, n.site, {base,offset}, n.scope);
                };
                items.push_back(expand(item(a,indexA,na), indexA ? *ta.element : ta,
                    item(b,indexB,nb), indexB ? *tb.element : tb));
            }
            Id array = g.semantic(Rule::Array, n.site, std::move(items), n.scope);
            if (count == 0) {
                Id sampleA = indexA ? literal(n.site,*ta.element,Value::runtime()) : a;
                Id sampleB = indexB ? literal(n.site,*tb.element,Value::runtime()) : b;
                // Empty shapes retain a type obligation, never an evaluated
                // sample element (e.g. an empty division by zero does not trap).
                Id before=state;
                g.nodes[array].annotation = expand(sampleA,indexA ? *ta.element : ta,sampleB,indexB ? *tb.element : tb);
                state=before;
            }
            return array;
        };
        n.expansion = expand(n.inputs[0], *left, n.inputs[1], *right);
        n.endState=state;
    }
    if (typesOnly) { if (const Type* t = needType(id,n.expansion)) publishType(id,*t); }
    else {
        // Evaluate each operand once even for an empty result shape.
        const Fact* a = need(id,n.inputs[0]); const Fact* b = need(id,n.inputs[1]);
        const Fact* result = need(id,n.expansion);
        const Fact* state=need(id,n.endState);
        if (a && b && result && state) {
            Fact f = *result; f.effect = std::max(std::max(f.effect,state->effect),std::max(a->effect,b->effect)); publish(id,std::move(f));
        }
    }
}
} // namespace vx
