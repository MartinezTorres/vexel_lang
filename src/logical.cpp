#include "vexel.hpp"

namespace vx {
bool Solver::logicalRule(Id id,bool typesOnly) {
    auto& n=g.nodes[id];
    if(n.key=="builtin-logical") return false;
    if(n.expansion==none) {
        auto candidates=functions("."+n.text,n.scope);
        if(candidates.empty()) {n.key="builtin-logical";return false;}
        const Type* left=needType(id,n.inputs[0]);if(!left) return true;
        std::vector<Scope::Callable> possible;
        for(const auto& candidate:candidates) {
            const auto& function=g.nodes[candidate.declaration];
            if(function.receiverCount!=1 || function.children.size()!=2) continue;
            Id annotation=g.nodes[function.children[0]].annotation;
            if(annotation!=none) {
                const Fact* target=need(id,type(annotation,candidate.scope,candidate.scope==none ? none : n.previous));
                if(!target) return true;
                try {
                    Type converted=conversionType(*left,target->type,false,id);
                    if(!n.receivers.empty() && g.nodes[n.receivers[0]].key!="temporary" && !(converted==*left)) continue;
                    if(g.nodes[n.inputs[0]].fact) (void)convert(*g.nodes[n.inputs[0]].fact,target->type,false,id);
                } catch(const Error&) {continue;}
            }
            possible.push_back(candidate);
        }
        if(possible.empty()) {n.key="builtin-logical";return false;}
        if(n.signature==none) {
            Id frame=g.scope(n.scope,n.site,n.identity+"/logical-arguments");
            Built right=build(g.nodes[n.site].children[1],frame,n.previous);
            n.environment={right.value,right.state};
            if(n.receivers.empty()) {
                Id slot=g.semantic(Rule::Slot,n.site,{n.inputs[0]},frame);
                Id store=g.semantic(Rule::Store,n.site,{n.previous,n.inputs[0]},frame);g.nodes[store].slot=slot;g.nodes[store].previous=n.previous;
                Id alias=g.semantic(Rule::Alias,n.site,{},frame);g.nodes[alias].slot=slot;g.nodes[alias].key="temporary";
                n.receivers={alias};n.previous=store;
            }
            Id probe=g.semantic(Rule::CallProbe,n.site,{n.inputs[0],right.value},n.scope);
            auto& match=g.nodes[probe];match.text="."+n.text;match.key="logical-dispatch";
            match.previous=n.previous;match.receiverCount=1;match.receivers=n.receivers;match.lexicalScope=possible[0].scope;
            for(const auto& candidate:possible) match.candidates.push_back(candidate.declaration);
            n.signature=probe;
        }
        const Fact* applicable=need(id,n.signature);if(!applicable) return true;
        if(applicable->value.number==0) {n.key="builtin-logical";return false;}
        const auto& match=g.nodes[n.signature];
        Id call=g.semantic(Rule::Invoke,n.site,match.inputs,n.scope);
        auto& invocation=g.nodes[call];invocation.text=match.text;invocation.body=match.body;
        invocation.candidates=match.candidates;invocation.lexicalScope=match.lexicalScope;
        invocation.receiverCount=1;invocation.receivers=n.receivers;invocation.previous=n.environment[1];
        n.expansion=call;n.key="overloaded-logical";
    }
    if(typesOnly) {
        if(const Type* type=needType(id,n.expansion)) {
            const auto& call=g.nodes[n.expansion];n.typeCompletion=call.fact ? call.completion : call.typeCompletion;
            publishType(id,*type);
        }
    } else if(const Fact* result=need(id,n.expansion)) {
        const auto& call=g.nodes[n.expansion];n.completion=call.completion;n.endState=call.endState;
        publish(id,*result);
    }
    return true;
}
} // namespace vx
