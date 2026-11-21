#include "source_demand.hpp"
#include "source_algebra.hpp"
#include "source_numeric.hpp"
#include <algorithm>

namespace vx::relational {
namespace {
bool immutable(const Ref& value) {
    return value->kind==Term::Number || value->name=="Input" ||
           value->name=="FrameArg" || value->name=="CaptureArg";
}
bool constant(const Ref& value,const Integer& number) {
    return value->kind==Term::Number && value->number==number;
}
std::vector<Ref> flatten(const Ref& value) {
    std::vector<Ref> out;
    std::function<void(const Ref&)> visit=[&](const Ref& v) {
        if(v->name=="Values") for(const auto& child:v->arguments) visit(child);
        else if(v->name=="RecordValue") visit(v->arguments.at(1));
        else out.push_back(v);
    };visit(value);return out;
}
Integer wrap(Integer value,unsigned bits,bool sign) {
    Integer modulus=Integer(1)<<bits;value&=modulus-1;
    if(sign && value>=(modulus>>1)) value-=modulus;
    return value;
}
}

std::optional<std::pair<Integer,Integer>> InterfaceDemand::bounds(const Ref& v,Owner owner) const {
    if(v->kind==Term::Number) return {{v->number,v->number}};
    const auto& a=v->arguments;const auto& op=v->name;
    if(op=="Runtime") return {{a.at(1)->number,a.at(2)->number}};
    if(op=="After") return bounds(a.at(1),owner);
    Ref type;
    if((op=="FrameArg" || op=="CaptureArg") && owner) {
        const auto& fn=functions_.at(*owner);auto i=a.at(0)->number.convert_to<std::size_t>();
        type=(op=="FrameArg" ? fn.argumentTypes : fn.captureTypes).at(i);
    } else if(op=="LoopSlotValue") type=a.at(3);
    else if(op=="CallValue") {
        auto found=calls_.find(a.at(0)->number.convert_to<Id>());
        if(found!=calls_.end()) type=functions_.at(found->second.callee).resultTypes.at(a.at(1)->number.convert_to<std::size_t>());
    }
    unsigned bits=0;bool sign=false;
    if(type && nativeScalar(type)) {bits=scalarBits(type);sign=scalarSigned(type);}
    else if(op=="Cast" || op=="SignedCast") {bits=a.at(0)->number.convert_to<unsigned>();sign=op=="SignedCast";}
    if(!bits || bits>32) return {};
    if(sign) return {{-(Integer(1)<<(bits-1)),(Integer(1)<<(bits-1))-1}};
    return {{0,(Integer(1)<<bits)-1}};
}

// Value proofs never substitute a mutable
// snapshot for a result: doing so would read it at the consumer's time instead
// of the call's time. It also never moves arithmetic out of a materialized call.
Ref InterfaceDemand::normalize(const Ref& value,Owner owner) {
    // Bare names (including a user's parameter named Runtime or Select) are
    // metadata, not applications of the residual expression constructors.
    if(value->arguments.empty()) return value;
    auto& cache=normalized_[owner];
    if(auto found=cache.find(value);found!=cache.end()) return found->second;
    const auto& original=value->arguments;const auto& op=value->name;Ref result=value;
    auto norm=[&](const Ref& v){return normalize(v,owner);};
    if(op=="Runtime") result=norm(original.at(0));
    else if(op=="After") result=norm(original.at(1)); // Execution has its own trace.
    else if(op=="LoopReady") {
        if(ready_.contains({original.at(0)->number.convert_to<Id>(),original.at(1)->number.convert_to<Id>()})) result=terms_.number(1);
    } else if(op=="LoopSlotValue") {
        auto found=invariants_.find({original.at(0)->number.convert_to<Id>(),original.at(1)->number.convert_to<Id>(),original.at(2)->number.convert_to<std::size_t>()});
        if(found!=invariants_.end()) result=found->second;
        else if(auto alias=loopAliases_.find({original[0]->number.convert_to<Id>(),original[1]->number.convert_to<Id>(),original[2]->number.convert_to<std::size_t>()});alias!=loopAliases_.end()) {
            auto [loop,root,index]=alias->second;
            result=norm(terms_.make("LoopSlotValue",{terms_.number(loop),terms_.number(root),terms_.number(index),original.at(3)}));
        }
    } else if(op=="CallValue") {
        auto site=original.at(0)->number.convert_to<Id>(),index=original.at(1)->number.convert_to<std::size_t>();
        if(auto call=calls_.find(site);call!=calls_.end()) {
            const auto& fn=functions_.at(call->second.callee);
            while(fn.resultAliases.contains(index)) index=fn.resultAliases.at(index);
            if(index!=original[1]->number) result=terms_.make("CallValue",{original[0],terms_.number(index)});
            if(auto known=fn.knownResults.find(index);known!=fn.knownResults.end()) {
                auto candidate=known->second;
                if(candidate->name=="FrameArg" || candidate->name=="CaptureArg") {
                    auto argument=candidate->name=="FrameArg";auto i=candidate->arguments.at(0)->number.convert_to<std::size_t>();
                    auto actual=flatten(call->second.trace->arguments.at(argument ? 2 : 3)).at(i);
                    const auto& type=(argument ? fn.argumentTypes : fn.captureTypes).at(i);
                    // Include the conversion at the actual/formal boundary.
                    candidate=normalize(terms_.make(scalarSigned(type) ? "SignedCast" : "Cast",{terms_.number(scalarBits(type)),actual}),call->second.owner);
                }
                if(immutable(candidate)) result=candidate;
            }
        }
    } else if(op!="Record" && op!="Array" && op!="Fields" && op!="Captures") {
        auto a=original;
        for(std::size_t i=op=="RecordValue" ? 1 : 0;i<a.size();++i) a[i]=norm(a[i]);
        if(a!=original) result=terms_.make(op,a);
        if(op=="Cast" || op=="SignedCast") {
            unsigned bits=a.at(0)->number.convert_to<unsigned>();bool sign=op=="SignedCast";
            if(bits && bits<=32) {
                if(a[1]->kind==Term::Number) result=terms_.number(wrap(a[1]->number,bits,sign));
                else {
                    Integer lo=sign ? -(Integer(1)<<(bits-1)) : Integer(0);
                    Integer hi=(Integer(1)<<(sign ? bits-1 : bits))-1;
                    auto fits=[&](const Ref& v){auto b=bounds(v,owner);return b && b->first>=lo && b->second<=hi;};
                    if(fits(original[1]) || fits(a[1])) result=a[1];
                }
            }
        } else if(op=="Select" || op=="If") {
            if(a[0]->kind==Term::Number) result=a[a[0]->number!=0 ? 1 : 2];
            else if(op=="Select" && identical(a[1],a[2]) && discardable(a[0])) result=a[1];
        } else if(op=="Not" && a[0]->kind==Term::Number) result=terms_.number(a[0]->number==0 ? 1 : 0);
        else if(op=="And") {
            if(constant(a[0],0) || (constant(a[1],0) && discardable(a[0]))) result=terms_.number(0);
            else if(a[0]->kind==Term::Number && a[1]->kind==Term::Number) result=terms_.number(1);
        } else if(op=="Compare" || op=="SignedCompare") {
            bool sign=op=="SignedCompare";auto lhs=a[sign ? 2 : 1],rhs=a[sign ? 3 : 2];
            const auto& cmp=a[0]->name;
            if((lhs->kind==Term::Number && rhs->kind==Term::Number) || (identical(lhs,rhs) && discardable(lhs))) {
                Integer l=0,r=0;
                if(lhs->kind==Term::Number && rhs->kind==Term::Number) {
                    auto bits=sign ? a[1]->number.convert_to<unsigned>() : 32;
                    l=wrap(lhs->number,bits,sign);r=wrap(rhs->number,bits,sign);
                }
                if(cmp=="==" || cmp=="!=" || cmp=="<" || cmp==">" || cmp=="<=" || cmp==">=")
                    result=terms_.number((cmp=="==" ? l==r : cmp=="!=" ? l!=r : cmp=="<" ? l<r : cmp==">" ? l>r : cmp=="<=" ? l<=r : l>=r) ? 1 : 0);
            }
        } else if((op=="Arithmetic" || op=="SignedArithmetic") && discardable(result)) {
            const auto& operation=a[0]->name;auto lhs=a[2],rhs=a[3];auto bits=a[1]->number.convert_to<unsigned>();
            bool sign=op=="SignedArithmetic";
            if(bits && bits<=32) {
                if(lhs->kind==Term::Number && rhs->kind==Term::Number) {
                    Integer l=wrap(lhs->number,bits,sign),r=wrap(rhs->number,bits,sign),v;
                    if(operation=="+") v=l+r;else if(operation=="-") v=l-r;else if(operation=="*") v=l*r;
                    else if(operation=="&") v=l&r;else if(operation=="|") v=l|r;else if(operation=="^") v=l^r;
                    else if(operation=="/") v=l/r;else if(operation=="%") v=l%r;
                    else if(operation=="<<") v=l<<wrap(rhs->number,32,false).convert_to<unsigned>();
                    else if(operation==">>") v=l>>wrap(rhs->number,32,false).convert_to<unsigned>();
                    else throw std::logic_error("unknown total arithmetic operation");
                    result=terms_.number(wrap(v,bits,sign));
                } else if((operation=="-" || operation=="^") && identical(lhs,rhs)) result=terms_.number(0);
                else if((operation=="*" || operation=="&") && (constant(lhs,0) || constant(rhs,0))) result=terms_.number(0);
                else {
                    Ref identity;
                    if((operation=="+" || operation=="-" || operation=="|" || operation=="^") && constant(rhs,0)) identity=original[2];
                    else if((operation=="+" || operation=="|" || operation=="^") && constant(lhs,0)) identity=original[3];
                    else if(operation=="*" && constant(rhs,1)) identity=original[2];
                    else if(operation=="*" && constant(lhs,1)) identity=original[3];
                    if(identity) result=norm(terms_.make(sign ? "SignedCast" : "Cast",{a[1],identity}));
                }
            }
        }
    }
    // Algebra supplies only constants or a single atom with its word conversion.
    // It does not choose a different expanded computation to emit. Snapshot
    // identity, call execution and readiness retain their existing boundaries.
    if((result->name=="Arithmetic" || result->name=="SignedArithmetic" ||
        result->name=="Cast" || result->name=="SignedCast") && discardable(result)) {
        auto identity=expressionIdentities_.intern(result);
        auto found=algebraCache_.find(identity);
        if(found==algebraCache_.end()) {
            auto reduction=modularReduction(terms_,result,expressionIdentities_);
            if(algebraCache_.size()>=4096) algebraCache_.clear();
            found=algebraCache_.emplace(identity,std::move(reduction)).first;
        }
        auto reduced=found->second;
        if(reduced) result=norm(*reduced);
    }
    // Range proofs describe successful evaluations. Only total expressions can
    // be erased here; producing calls, readiness checks and effects keep their
    // original execution trace even when their result becomes constant.
    if(rangesReady_ && discardable(value)) if(auto fact=range(value,owner);fact && fact->lower==fact->upper)
        result=terms_.number(fact->lower);
    cache.emplace(value,result);return result;
}

void InterfaceDemand::resolveFacts(bool publish) {
    // Results grow from established facts. Recursion without an anchor cannot
    // manufacture a result or prove completion. Calls remain in the trace.
    for(bool changed=true;changed;) {
        changed=false;
        for(auto& [id,fn]:functions_) for(std::size_t i=0;i<fn.results.size();++i) if(!fn.knownResults.contains(i)) {
            const auto& type=fn.resultTypes[i];
            auto value=normalize(terms_.make(scalarSigned(type) ? "SignedCast" : "Cast",{terms_.number(scalarBits(type)),fn.results[i]}),id);
            if(immutable(value)) {fn.knownResults.emplace(i,value);normalized_.clear();changed=true;}
        }
        struct Candidate {Owner owner;Ref value;std::vector<Ref> transfers;};
        std::map<Leaf,Candidate> values;
        std::map<std::pair<Id,Id>,Candidate> flags;
        // Entry anchors are evaluated using only established facts, before any
        // hypotheses are installed. All three transfers must preserve them.
        for(const auto& [id,loop]:loops_) {
            const auto& a=loop.trace->arguments;
            std::vector<std::vector<Ref>> updates={flatten(a[1]),flatten(a[4]),flatten(a[6])};std::size_t at=0;
            for(std::size_t s=0;s<loop.slots.size();++s) {
                const auto& slot=loop.slots[s];auto key=std::make_pair(id,slot.root);
                std::vector<Ref> transfers={a[12]->arguments.at(s),a[13]->arguments.at(s),a[14]->arguments.at(s)};
                if(!ready_.contains(key) && constant(normalize(transfers[0],loop.owner),1) && std::all_of(transfers.begin(),transfers.end(),discardable))
                    flags.emplace(key,Candidate{loop.owner,terms_.number(1),transfers});
                for(std::size_t i=0;i<slot.count;++i,++at) {
                    Leaf leaf={id,slot.root,i};
                    if(invariants_.contains(leaf) || (!ready_.contains(key) && !flags.contains(key))) continue;
                    transfers={updates[0].at(at),updates[1].at(at),updates[2].at(at)};
                    const auto& type=a[10]->arguments.at(at);
                    for(auto& transfer:transfers) transfer=terms_.make(scalarSigned(type) ? "SignedCast" : "Cast",{terms_.number(scalarBits(type)),transfer});
                    auto initial=normalize(transfers[0],loop.owner);
                    if(immutable(initial) && std::all_of(transfers.begin(),transfers.end(),discardable))
                        values.emplace(leaf,Candidate{loop.owner,initial,transfers});
                }
            }
        }
        for(const auto& [key,candidate]:flags) {(void)candidate;ready_.insert(key);}
        for(const auto& [key,candidate]:values) invariants_.emplace(key,candidate.value);
        normalized_.clear();
        // Simultaneous induction: remove failed hypotheses, then recheck their
        // dependants. A mutually preserved set is valid only with entry anchors.
        for(bool rejected=true;rejected;) {
            rejected=false;
            auto preserved=[&](const Candidate& c){return std::all_of(c.transfers.begin(),c.transfers.end(),[&](const Ref& transfer){return identical(normalize(transfer,c.owner),c.value);});};
            for(auto it=flags.begin();it!=flags.end();) {
                if(preserved(it->second)) {++it;continue;}
                ready_.erase(it->first);it=flags.erase(it);normalized_.clear();rejected=true;
            }
            for(auto it=values.begin();it!=values.end();) {
                auto [id,root,index]=it->first;(void)index;
                if(ready_.contains({id,root}) && preserved(it->second)) {++it;continue;}
                invariants_.erase(it->first);it=values.erase(it);normalized_.clear();rejected=true;
            }
        }
        if(!flags.empty() || !values.empty()) changed=true;
        if(resolveEqualities()) changed=true;
    }
    if(!publish) return;
    for(const auto& [leaf,target]:loopAliases_) {
        auto [loop,root,index]=leaf;auto representative=target;
        while(loopAliases_.contains(representative)) representative=loopAliases_.at(representative);
        auto [targetLoop,targetRoot,targetIndex]=representative;
        if(loop!=targetLoop) throw std::logic_error("carried equality crosses loop clocks");
        loops_.at(loop).aliases.emplace(std::make_pair(root,index),std::make_pair(targetRoot,targetIndex));
    }
    for(auto& [id,loop]:loops_) for(auto& slot:loop.slots) {
        slot.alwaysReady=ready_.contains({id,slot.root});
        for(std::size_t i=0;i<slot.count;++i) if(invariants_.contains({id,slot.root,i})) ++slot.invariantLeaves;
    }
    // Publish a separate physical view. The selected semantic model, including
    // all readiness and completion evidence, remains unchanged.
    for(auto& [id,fn]:functions_) {
        for(auto& [leaf,target]:fn.resultAliases) {(void)leaf;while(fn.resultAliases.contains(target)) target=fn.resultAliases.at(target);}
        fn.trace=normalize(fn.trace,id);for(auto& value:fn.results) value=normalize(value,id);
    }
    for(auto& [id,loop]:loops_) {(void)id;loop.trace=normalize(loop.trace,loop.owner);}
    for(auto& [trace,values]:roots_) {trace=normalize(trace,{});for(auto& value:values) value=normalize(value,{});}
}
}
