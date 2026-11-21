#include "source_demand.hpp"
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace vx::relational {
namespace {
std::optional<std::pair<std::size_t,std::size_t>> discardableOperands(const Term& value) {
    if(value.kind==Term::Number) return {{0,0}};
    const auto& op=value.name;const auto& a=value.arguments;
    // A snapshot's producer still needs its own execution certificate.
    if(op=="Input" || op=="FrameArg" || op=="CaptureArg" || op=="CallValue" ||
       op=="LoopIndex" || op=="LoopValue" || op=="LoopSlotValue" ||
       op=="LoopReady" || op=="LoopFlag" || op=="LoopReturned") return {{0,0}};
    if(op=="Runtime" || op=="Not") return {{0,1}};
    // After's execution is emitted separately, exactly as in cExpression().
    if(op=="After" || op=="Cast" || op=="SignedCast" || op=="SignedAbsolute") return {{1,2}};
    if(op=="And" || op=="Select") return {{0,a.size()}};
    if(op=="Compare") return {{1,3}};
    if(op=="SignedCompare") return {{2,4}};
    if(op=="Arithmetic" || op=="SignedArithmetic") {
        const auto& operation=a.at(0)->name;
        if(operation=="+" || operation=="-" || operation=="*" || operation=="&" || operation=="|" || operation=="^" ||
           operation=="/" || operation=="%" || operation=="<<" || operation==">>")
            return {{2,4}};
    }
    // Arithmetic domains are checked separately. Indexing, conversions and
    // unfamiliar operations keep their evaluation.
    return {};
}

// Bounds of successful evaluations, using only immutable expression structure.
// These bounds cannot themselves prove completion: discardable() also visits
// every operand and checks the domain of every possibly trapping operation.
// A query owns its memo, so snapshot/owner identities never escape their terms.
using Bounds=std::unordered_map<const Term*,IntegerBound>;
IntegerBound expressionBound(const Term* root,Bounds& bounds) {
    std::vector<std::pair<const Term*,bool>> work{{root,false}};
    while(!work.empty()) {
        auto [node,ready]=work.back();work.pop_back();
        if(bounds.contains(node)) continue;
        const auto& op=node->name;const auto& a=node->arguments;
        if(node->kind==Term::Number) {bounds.emplace(node,IntegerRange{node->number,node->number});continue;}
        if(op=="Runtime") {bounds.emplace(node,IntegerRange{a.at(1)->number,a.at(2)->number});continue;}
        auto children=discardableOperands(*node);
        if(!children || children->first==children->second) {bounds.emplace(node,IntegerBound{});continue;}
        if(!ready) {
            work.emplace_back(node,true);
            for(auto i=children->first;i<children->second;++i) work.emplace_back(a.at(i).get(),false);
            continue;
        }
        auto child=[&](std::size_t i){return bounds.at(a.at(i).get());};
        IntegerBound result;
        if(op=="After") result=child(1);
        else if(op=="Cast" || op=="SignedCast")
            result=convertedIntegerRange(child(1),a[0]->number.convert_to<unsigned>(),op=="SignedCast");
        else if(op=="SignedAbsolute") result=convertedIntegerRange({},a[0]->number.convert_to<unsigned>(),true);
        else if(op=="Arithmetic" || op=="SignedArithmetic")
            result=arithmeticIntegerRange(a[0]->name,child(2),child(3),a[1]->number.convert_to<unsigned>(),op=="SignedArithmetic");
        else if(op=="Compare" || op=="SignedCompare" || op=="Not" || op=="And") result=IntegerRange{0,1};
        else if(op=="Select") {
            auto yes=child(1),no=child(2);
            if(yes && no) result=IntegerRange{std::min(yes->lower,no->lower),std::max(yes->upper,no->upper)};
        }
        bounds.emplace(node,std::move(result));
    }
    return bounds.at(root);
}
template<class BoundQuery>
bool completionDomain(const Term& node,BoundQuery&& boundQuery) {
    if(node.name!="Arithmetic" && node.name!="SignedArithmetic") return true;
    const auto& a=node.arguments;const auto& op=a[0]->name;
    if(op!="/" && op!="%" && op!="<<" && op!=">>") return true;
    unsigned bits=a[1]->number.convert_to<unsigned>();
    if(!bits || bits>32) return false;
    auto bound=boundQuery(a[3]);
    if(op=="/" || op=="%") {
        bound=convertedIntegerRange(bound,bits,node.name=="SignedArithmetic");
        // Signed minimum / -1 wraps; only a zero divisor traps.
        return bound && (bound->lower>0 || bound->upper<0);
    }
    bound=convertedIntegerRange(bound,32,false);
    return bound && bound->lower>=0 && bound->upper<bits;
}
template<class BoundQuery>
bool discardableWith(const Ref& value,BoundQuery&& boundQuery) {
    auto root=discardableOperands(*value);if(!root) return false;
    if(root->first==root->second) return true;
    // This is a conjunction over exact immutable nodes, not over exponentially
    // many paths through them. Query-local pointers cannot outlive their root
    // or carry an answer across another binding/normalization view.
    std::vector<const Term*> pending{value.get()};std::unordered_set<const Term*> visited;Bounds bounds;
    while(!pending.empty()) {
        auto node=pending.back();pending.pop_back();auto children=discardableOperands(*node);
        if(!children) return false;
        if(children->first==children->second || !visited.insert(node).second) continue;
        if(!completionDomain(*node,[&](const Ref& v){return boundQuery(v,bounds);})) return false;
        for(auto i=children->first;i<children->second;++i) pending.push_back(node->arguments.at(i).get());
    }
    return true;
}
}
bool InterfaceDemand::discardable(const Ref& value) {
    return discardableWith(value,[](const Ref& v,Bounds& bounds){return expressionBound(v.get(),bounds);});
}
bool InterfaceDemand::discardableIn(const Ref& value,Owner owner,const Guards& guards) {
    // Successful-value ranges justify an operation's domain only after every
    // operand independently completes. The traversal also retains producers'
    // execution obligations; a call snapshot cannot certify its own producer.
    auto intersect=[](Bound structural,Bound contextual) {
        if(!contextual) return structural;
        if(!structural) return contextual;
        IntegerRange intersection{std::max(structural->lower,contextual->lower),
                                  std::min(structural->upper,contextual->upper)};
        return intersection.lower<=intersection.upper ? IntegerBound(intersection) : structural;
    };
    if(discardableWith(value,[&](const Ref& v,Bounds& bounds) {
        auto structural=expressionBound(v.get(),bounds);
        if(!rangesReady_) return structural;
        return intersect(structural,range(v,owner));
    })) return true;
    if(!rangesReady_) return false;
    // Lazy arms have different premises. Reuse the existing bounded range
    // environments; every condition and reached operand still needs its own
    // completion certificate. No premise crosses an execution boundary.
    RangeContext initial;
    if(owner) {
        const auto& fn=functions_.at(*owner);
        initial.arguments=fn.argumentRanges;initial.captures=fn.captureRanges;
    }
    for(const auto& [condition,truth]:guards)
        if(!assumeRange(condition,truth,owner,initial)) return true;
    struct Pending {Ref value;RangeContext context;unsigned depth=0;};
    std::vector<Pending> pending{{value,std::move(initial)}};
    std::set<std::pair<Id,Id>> visited;Bounds bounds;
    unsigned steps=0;
    while(!pending.empty()) {
        auto item=std::move(pending.back());pending.pop_back();
        const auto& v=item.value;const auto& a=v->arguments;
        auto children=discardableOperands(*v);if(!children) return false;
        if(children->first==children->second) continue;
        if(++steps>maxContextNodes || item.depth>=maxContextDepth) return false;
        if(!visited.emplace(rangeExpression(v),rangeEnvironment(owner,item.context)).second) continue;
        if(!completionDomain(*v,[&](const Ref& operand) {
            return intersect(expressionBound(operand.get(),bounds),range(operand,owner,&item.context));
        })) return false;
        if(v->name=="Select" || v->name=="And") {
            auto branch=[&](bool truth,std::size_t arm) {
                auto context=item.context;
                if(assumeRange(a[0],truth,owner,context))
                    pending.push_back({a[arm],std::move(context),item.depth+1});
            };
            branch(true,1);if(v->name=="Select") branch(false,2);
            pending.push_back({a[0],std::move(item.context),item.depth});
        } else for(auto i=children->first;i<children->second;++i)
            pending.push_back({a[i],item.context,item.depth});
    }
    return true;
}
bool InterfaceDemand::stableAcrossLoop(const Ref& value) {
    if(value->arguments.empty()) return true;
    if(!discardable(value)) return false;
    std::vector<const Term*> pending{value.get()};std::unordered_set<const Term*> visited;
    while(!pending.empty()) {
        auto node=pending.back();pending.pop_back();
        if(node->arguments.empty() || !visited.insert(node).second) continue;
        const auto& op=node->name;
        if(op=="Input" || op=="FrameArg" || op=="CaptureArg") continue;
        if(op=="CallValue" || op=="After" || op.starts_with("Loop")) return false;
        if(!discardableOperands(*node)) return false;
        for(const auto& child:node->arguments) pending.push_back(child.get());
    }
    return true;
}

void InterfaceDemand::resolveExecutions() {
    std::map<Id,std::set<Id>> users;
    std::map<Id,std::size_t> pending;
    std::deque<Id> ready;
    for(auto& [id,fn]:functions_) {
        fn.discardableResults.clear();
        for(const auto& result:fn.results) fn.discardableResults.push_back(discardableIn(result,id));
        std::set<Id> callees;
        std::map<std::vector<Id>,bool> memo;
        unsigned steps=0;
        auto actual=[&](auto&& self,const Ref& value,const Guards& guards)->bool {
            if(value->name=="Values")
                return std::all_of(value->arguments.begin(),value->arguments.end(),[&](const Ref& v){return self(self,v,guards);});
            if(value->name=="RecordValue") return self(self,value->arguments.at(1),guards);
            return discardableIn(value,id,guards);
        };
        auto trace=[&](auto&& self,const Ref& value,const Guards& guards)->bool {
            std::vector<Id> key{rangeExpression(value)};
            for(const auto& [condition,truth]:guards) {key.push_back(rangeExpression(condition));key.push_back(truth ? 1 : 0);}
            if(auto found=memo.find(key);found!=memo.end()) return found->second;
            if(++steps>maxContextNodes || guards.size()>=maxContextDepth) return false;
            const auto& op=value->name;const auto& a=value->arguments;bool total=false;
            if(op=="Done") total=true;
            else if(op=="Sequence")
                total=std::all_of(a.begin(),a.end(),[&](const Ref& v){return self(self,v,guards);});
            else if(op=="If") {
                auto yes=guards,no=guards;yes.emplace_back(a[0],true);no.emplace_back(a[0],false);
                total=discardableIn(a.at(0),id,guards) && self(self,a.at(1),yes) && self(self,a.at(2),no);
            }
            else if(op=="Evaluate") total=discardableIn(a.at(0),id,guards);
            else if(op=="Invoke") {
                total=actual(actual,a.at(2),guards) && actual(actual,a.at(3),guards);
                if(total) callees.insert(a.at(1)->number.convert_to<Id>());
            }
            // Foreign calls, storage, checks, loops and unfamiliar execution
            // constructors retain their effects and completion obligations.
            memo.emplace(std::move(key),total);return total;
        };
        if(!std::all_of(fn.discardableResults.begin(),fn.discardableResults.end(),[](bool v){return v;}) || !trace(trace,fn.trace,{})) continue;
        pending.emplace(id,callees.size());
        for(Id callee:callees) users[callee].insert(id);
        if(callees.empty()) ready.push_back(id);
    }
    // A least fixed point needs a finite execution anchor. A recursive cycle
    // cannot certify its own completion just because its outputs are unused.
    while(!ready.empty()) {
        Id id=ready.front();ready.pop_front();functions_.at(id).discardableExecution=true;
        for(Id user:users[id]) if(--pending.at(user)==0) ready.push_back(user);
    }
}
}
