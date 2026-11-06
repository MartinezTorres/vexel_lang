#include "vexel.hpp"

namespace vx {
// A property may cross a state transition without executing it when the
// transition cannot change the requested automatic location. This is a
// conservative source proof: overloads contribute the union of their writes,
// and an unavailable lexical environment never supplies a negative answer.
// In particular, absence of a Store in an unexpanded graph proves nothing.
bool Solver::mayWrite(Id site, Id scope, Id variable) const {
    using Names = std::set<std::string>;
    struct Work { Id site, scope; std::shared_ptr<const Names> parameters; };
    auto empty = std::make_shared<const Names>();
    std::vector<Work> todo{{site,scope,empty}};
    std::set<std::pair<Id,Id>> calls, expressions;
    auto root = [&](Id location) {
        while(location!=none && g.nodes[location].rule==Rule::Alias) location=g.nodes[location].slot;
        return location;
    };
    variable=root(variable);
    auto writesPlace = [&](Id at, const Work& work) {
        while(g.nodes[at].syntax==Syntax::Member || g.nodes[at].syntax==Syntax::Index)
            at=g.nodes[at].children[0];
        const auto& base=g.nodes[at];
        if(base.syntax!=Syntax::Name || work.parameters->contains(base.text)) return false;
        return root(slot(base.text,work.scope))==variable;
    };
    while(!todo.empty()) {
        Work work=todo.back();todo.pop_back();
        if(work.site==none) continue;
        const auto& source=g.nodes[work.site];
        // A nested declaration can extend an overload set before its block is
        // instantiated. Do not confuse the outer set with that complete set.
        if(source.syntax==Syntax::Function || source.syntax==Syntax::Import) return true;
        if(source.syntax==Syntax::Global || source.syntax==Syntax::RecordDecl ||
           source.syntax==Syntax::Probe || source.syntax==Syntax::MethodProbe) continue;
        // Completion needs its own proof before a following read is reachable.
        if(source.syntax==Syntax::Return) return true;
        if(source.syntax==Syntax::Name && source.text.starts_with('$') &&
           !work.parameters->contains(source.text)) {
            bool found=false;
            for(Id frame=work.scope;frame!=none;frame=g.scopes[frame].parent) {
                auto binding=g.scopes[frame].expressions.find(source.text);
                if(binding==g.scopes[frame].expressions.end()) continue;
                const auto& expression=binding->second;
                if(expressions.emplace(expression.site,expression.scope).second)
                    todo.push_back({expression.site,expression.scope,empty});
                found=true;break;
            }
            if(!found) return true;
        }
        if(source.syntax==Syntax::Assign && writesPlace(source.children[0],work)) return true;
        if(source.syntax==Syntax::Declare && !work.parameters->contains(source.text) &&
           root(slot(source.text,work.scope))==variable) return true;
        if(source.syntax==Syntax::Destructure)
            for(std::size_t i=0;i+1<source.children.size();++i)
                if(writesPlace(source.children[i],work)) return true;

        std::vector<std::string> operations;
        std::size_t receivers=0;
        if(source.syntax==Syntax::Call) {operations.push_back(source.text);receivers=source.receiverCount;}
        else if(source.syntax==Syntax::Unary || source.syntax==Syntax::Binary || source.syntax==Syntax::Iterate) {
            operations.push_back("."+source.text);receivers=1;
            if(source.text.starts_with('.') && source.text!="..") operations.push_back(source.text);
        } else if(source.syntax==Syntax::Assign && source.text!="=") {
            std::string op=source.text.substr(0,source.text.size()-1);
            operations.push_back("."+op);receivers=1;
            if(op.starts_with('.') && op!="..") operations.push_back(op);
        }
        // Sorting can dispatch a comparison method even without a written '<'.
        if((source.syntax==Syntax::Iterate || source.syntax==Syntax::Loop) && source.text=="@@")
            operations.push_back(".<");
        for(const auto& name:operations) {
            auto candidates=functions(name,work.scope);
            if(candidates.empty() && source.syntax==Syntax::Call) return true;
            if(!candidates.empty()) {
                for(std::size_t i=0;i<receivers && i<source.children.size();++i)
                    if(writesPlace(source.children[i],work)) return true;
            }
            for(const auto& candidate:candidates) {
                const auto& fn=g.nodes[candidate.declaration];
                if(fn.external || !calls.emplace(candidate.declaration,candidate.scope).second) continue;
                auto parameters=std::make_shared<Names>();
                for(Id parameter:fn.children) parameters->insert(g.nodes[parameter].text);
                todo.push_back({fn.body,candidate.scope,std::move(parameters)});
            }
        }
        for(Id child:source.children) todo.push_back({child,work.scope,work.parameters});
        if(source.body!=none) todo.push_back({source.body,work.scope,work.parameters});
        if(source.annotation!=none) todo.push_back({source.annotation,work.scope,work.parameters});
    }
    return false;
}
} // namespace vx
