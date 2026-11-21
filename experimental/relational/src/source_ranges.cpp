#include "source_demand.hpp"
#include "source_numeric.hpp"
#include <algorithm>
#include <ostream>

namespace vx::relational {
namespace {
using Range=InterfaceDemand::Range;
using Bound=InterfaceDemand::Bound;
Range domain(unsigned bits,bool sign) {
    return integerDomain(bits,sign);
}
Bound native(const Ref& type) {
    auto f=scalarFormat(type);
    if(!f || !f->bits || f->bits>32 || (f->kind!=ScalarFormat::IntegerKind && f->kind!=ScalarFormat::Bool)) return {};
    return domain(f->bits,f->sign);
}
Bound meet(Bound a,Bound b) {
    if(!a) return b;
    if(!b) return a;
    Range r{std::max(a->lower,b->lower),std::min(a->upper,b->upper)};
    // Do not derive reachability from inconsistent/empty abstract facts.
    return r.lower<=r.upper ? Bound(r) : a;
}
Bound join(Bound a,Bound b) {
    if(!a || !b) return {};
    return Range{std::min(a->lower,b->lower),std::max(a->upper,b->upper)};
}
Bound converted(Bound value,unsigned bits,bool sign) {
    return convertedIntegerRange(std::move(value),bits,sign);
}
Bound converted(Bound value,const Ref& type) {
    if(!native(type)) return {};
    return converted(value,scalarBits(type),scalarSigned(type));
}
bool exact(const Bound& b) {return b && b->lower==b->upper;}
std::vector<Ref> flatten(const Ref& v) {
    std::vector<Ref> result;
    std::function<void(const Ref&)> visit=[&](const Ref& value) {
        if(value->name=="Values") for(const auto& child:value->arguments) visit(child);
        else if(value->name=="RecordValue") visit(value->arguments.at(1));
        else result.push_back(value);
    };visit(v);return result;
}
}

Id InterfaceDemand::rangeExpression(const Ref& value) {
    return expressionIdentities_.intern(value);
}

Id InterfaceDemand::rangeEnvironment(Owner owner,RangeContext& context) {
    if(context.environment) return *context.environment;
    std::vector<Integer> key{owner ? Integer(*owner)+1 : Integer(0)};
    auto append=[&](const Bound& b) {
        key.push_back(b ? 1 : 0);if(b) {key.push_back(b->lower);key.push_back(b->upper);}
    };
    for(const auto* values:{&context.arguments,&context.captures}) {
        key.push_back(values->size());for(const auto& value:*values) append(value);
    }
    key.push_back(context.assumptions.size());
    for(const auto& [expression,bound]:context.assumptions) {key.push_back(expression);append(bound);}
    auto [found,inserted]=rangeEnvironments_.try_emplace(std::move(key),rangeEnvironments_.size());
    (void)inserted;context.environment=found->second;
    peakRangeEnvironments_=std::max(peakRangeEnvironments_,static_cast<unsigned>(rangeEnvironments_.size()));
    return found->second;
}

InterfaceDemand::Bound InterfaceDemand::range(const Ref& value,Owner owner) {
    // The induction checker can install and reject readiness hypotheses after
    // interval closure. A query answered under one hypothesis set must never
    // escape into another. Only top-level queries cross that proof boundary;
    // recursive queries see a fixed set. Keep the existing work budget.
    if(rangeReady_!=ready_) {
        rangeCache_.clear();rangeContexts_.clear();rangeEnvironments_.clear();rangeQueries_.clear();
        rangeReady_=ready_;
    }
    return range(value,owner,nullptr);
}

InterfaceDemand::Bound InterfaceDemand::range(const Ref& value,Owner owner,RangeContext* context) {
    if(value->kind==Term::Number) return Range{value->number,value->number};
    if(value->arguments.empty()) return {};
    auto expression=rangeExpression(value);
    auto& cache=context ? rangeQueries_[rangeEnvironment(owner,*context)] : rangeCache_[owner];
    if(auto found=cache.find(expression);found!=cache.end()) {++rangeQueryHits_;return found->second;}
    if(context) {
        if(contextNodes_>=maxContextNodes) {contextBudgetHit_=true;return {};}
        ++contextNodes_;peakContextNodes_=std::max(peakContextNodes_,contextNodes_);
    }
    ++rangeQueryMisses_;
    const auto& a=value->arguments;const auto& op=value->name;Bound result;
    auto get=[&](std::size_t i){return range(a.at(i),owner,context);};
    if(op=="Runtime") result=meet(Range{a[1]->number,a[2]->number},get(0));
    else if(op=="After") result=get(1);
    else if((op=="FrameArg" || op=="CaptureArg") && owner) {
        const auto& fn=functions_.at(*owner);auto i=a[0]->number.convert_to<std::size_t>();
        result=context ? (op=="FrameArg" ? context->arguments : context->captures).at(i)
                       : (op=="FrameArg" ? fn.argumentRanges : fn.captureRanges).at(i);
    } else if(op=="CallValue") {
        if(auto found=calls_.find(a[0]->number.convert_to<Id>());found!=calls_.end())
            result=callRange(found->first,a[1]->number.convert_to<std::size_t>(),owner,context);
    } else if(op=="LoopSlotValue") {
        auto found=loopRanges_.find({a[0]->number.convert_to<Id>(),a[1]->number.convert_to<Id>(),a[2]->number.convert_to<std::size_t>()});
        if(found!=loopRanges_.end()) result=found->second;
    } else if(op=="LoopReady") {
        result=rangeReady_.contains({a[0]->number.convert_to<Id>(),a[1]->number.convert_to<Id>()}) ? Range{1,1} : Range{0,1};
    } else if(op=="Cast" || op=="SignedCast") result=converted(get(1),a[0]->number.convert_to<unsigned>(),op=="SignedCast");
    else if(op=="Select") {
        auto test=get(0);
        if(test && (test->upper<0 || test->lower>0)) result=get(1);
        else if(exact(test) && test->lower==0) result=get(2);
        else {
            auto yes=branchRange(a[1],a[0],true,owner,context);
            auto no=branchRange(a[2],a[0],false,owner,context);
            result=yes.first && no.first ? join(yes.second,no.second)
                : yes.first ? yes.second : no.first ? no.second : join(get(1),get(2));
        }
    } else if(op=="Not" || op=="And") {
        result=Range{0,1};auto left=get(0);
        if(op=="Not") {
            if(exact(left) && left->lower==0) result=Range{1,1};
            else if(left && (left->upper<0 || left->lower>0)) result=Range{0,0};
        } else {
            auto right=get(1);
            if((exact(left) && left->lower==0) || (exact(right) && right->lower==0)) result=Range{0,0};
            else if(left && right && (left->upper<0 || left->lower>0) && (right->upper<0 || right->lower>0)) result=Range{1,1};
        }
    } else if(op=="Compare" || op=="SignedCompare") {
        bool sign=op=="SignedCompare";auto bits=sign ? a[1]->number.convert_to<unsigned>() : 32;
        auto left=converted(get(sign ? 2 : 1),bits,sign),right=converted(get(sign ? 3 : 2),bits,sign);
        result=Range{0,1};const auto& cmp=a[0]->name;
        if(left && right) {
            std::optional<bool> answer;
            if(cmp==">" || cmp==">=") std::swap(left,right);
            if(cmp=="<" || cmp==">") {
                if(left->upper<right->lower) answer=true;
                else if(left->lower>=right->upper) answer=false;
            } else if(cmp=="<=" || cmp==">=") {
                if(left->upper<=right->lower) answer=true;
                else if(left->lower>right->upper) answer=false;
            } else if(cmp=="==" || cmp=="!=") {
                if(left->upper<right->lower || right->upper<left->lower) answer=cmp=="!=";
                else if(exact(left) && exact(right)) answer=(left->lower==right->lower)==(cmp=="==");
            }
            if(answer) result=Range{*answer ? 1 : 0,*answer ? 1 : 0};
        }
    } else if(op=="Arithmetic" || op=="SignedArithmetic") {
        bool sign=op=="SignedArithmetic";auto bits=a[1]->number.convert_to<unsigned>();
        if(bits && bits<=32) {
            result=arithmeticIntegerRange(a[0]->name,get(2),get(3),bits,sign);
            // Unknown domains retain their full range. A tighter envelope
            // alone does not discharge an operation's evaluation obligations.
        }
    }
    if(context) if(auto found=context->assumptions.find(expression);found!=context->assumptions.end())
        result=meet(result,found->second);
    cache.emplace(expression,result);return result;
}

bool InterfaceDemand::refineRange(Ref value,Bound bound,Owner owner,RangeContext& context) {
    if(!bound) return true;
    for(;;) {
        auto previous=range(value,owner,&context);
        if(previous) {
            bound=Range{std::max(previous->lower,bound->lower),std::min(previous->upper,bound->upper)};
            if(bound->lower>bound->upper) return false;
        }
        context.assumptions[rangeExpression(value)]=bound;
        context.environment.reset();
        const auto& a=value->arguments;const auto& op=value->name;
        if(a.empty()) break;
        if(op=="Runtime" || op=="After") value=a[op=="Runtime" ? 0 : 1];
        else if(op=="Cast" || op=="SignedCast") {
            auto input=range(a[1],owner,&context);
            // A wrapping conversion is many-to-one. Its result constraint is
            // valid, but cannot be imposed on the unconverted input.
            if(!input || converted(input,a[0]->number.convert_to<unsigned>(),op=="SignedCast")!=input) break;
            value=a[1];
        } else break;
    }
    return true;
}

bool InterfaceDemand::assumeRange(const Ref& condition,bool truth,Owner owner,RangeContext& context,unsigned depth) {
    if(depth>=maxContextDepth || contextNodes_>=maxContextNodes) {contextBudgetHit_=true;return true;}
    auto known=range(condition,owner,&context);
    if(known && (truth ? exact(known) && known->lower==0 : known->lower>0 || known->upper<0)) return false;
    const auto& a=condition->arguments;const auto& op=condition->name;
    if(a.empty()) return true;
    auto assume=[&](const Ref& value,bool answer){return assumeRange(value,answer,owner,context,depth+1);};
    if(op=="Runtime" || op=="After") return assume(a[op=="Runtime" ? 0 : 1],truth);
    if(op=="Not") return assume(a[0],!truth);
    if(op=="Select") {
        auto possible=[&](const Ref& arm) {
            auto b=range(arm,owner,&context);
            return !b || (truth ? !exact(b) || b->lower!=0 : b->lower<=0 && b->upper>=0);
        };
        bool yes=possible(a[1]),no=possible(a[2]);
        if(!yes && !no) return false;
        // Short-circuit &&/|| are selections too. An arm that cannot produce
        // the requested truth value forces the other path, without guessing
        // between two possible paths or evaluating their effects here.
        if(yes!=no) return assume(a[0],yes) && assume(a[yes ? 1 : 2],truth);
    }
    if(op=="And") {
        if(truth) return assume(a[0],true) && assume(a[1],true);
        auto left=range(a[0],owner,&context),right=range(a[1],owner,&context);
        if(left && (left->lower>0 || left->upper<0)) return assume(a[1],false);
        if(right && (right->lower>0 || right->upper<0)) return assume(a[0],false);
        return true; // A disjunction requires separate environments.
    }
    if(op!="Compare" && op!="SignedCompare") {
        if(!truth) return refineRange(condition,Range{0,0},owner,context);
        if(known && known->lower==0) return refineRange(condition,Range{1,known->upper},owner,context);
        if(known && known->upper==0) return refineRange(condition,Range{known->lower,-1},owner,context);
        return true;
    }
    bool sign=op=="SignedCompare";unsigned bits=sign ? a[1]->number.convert_to<unsigned>() : 32;
    if(!bits || bits>32) return true;
    auto lhs=a[sign ? 2 : 1],rhs=a[sign ? 3 : 2];auto cmp=a[0]->name;
    if(!truth) {
        if(cmp=="<") cmp=">=";else if(cmp=="<=") cmp=">";
        else if(cmp==">") cmp="<=";else if(cmp==">=") cmp="<";
        else if(cmp=="==") cmp="!=";else if(cmp=="!=") cmp="==";
        else return true;
    }
    if(cmp==">" || cmp==">=") {std::swap(lhs,rhs);cmp=cmp==">" ? "<" : "<=";}
    auto rawLeft=range(lhs,owner,&context),rawRight=range(rhs,owner,&context);
    auto left=converted(rawLeft,bits,sign),right=converted(rawRight,bits,sign);
    if(cmp=="<" || cmp=="<=") {
        Integer delta=cmp=="<" ? 1 : 0;
        Integer hi=right->upper-delta,lo=left->lower+delta;
        left->upper=std::min(left->upper,hi);right->lower=std::max(right->lower,lo);
    } else if(cmp=="==") {
        Range common{std::max(left->lower,right->lower),std::min(left->upper,right->upper)};
        left=common;right=common;
    } else if(cmp=="!=") {
        auto exclude=[](Bound& value,const Bound& other) {
            if(!exact(other)) return;
            if(value->lower==other->lower) ++value->lower;
            if(value->upper==other->lower) --value->upper;
        };
        auto oldLeft=left;exclude(left,right);exclude(right,oldLeft);
    } else return true;
    if(left->lower>left->upper || right->lower>right->upper) return false;
    // Comparisons convert their operands too. Narrow the original expression
    // only when that conversion preserves its numeric value throughout.
    if(rawLeft && converted(rawLeft,bits,sign)==rawLeft && !refineRange(lhs,left,owner,context)) return false;
    if(rawRight && converted(rawRight,bits,sign)==rawRight && !refineRange(rhs,right,owner,context)) return false;
    return refineRange(condition,Range{truth ? 1 : 0,truth ? 1 : 0},owner,context);
}

std::pair<bool,InterfaceDemand::Bound> InterfaceDemand::branchRange(const Ref& value,const Ref& condition,bool truth,Owner owner,RangeContext* parent,const Leaf* preserved) {
    auto evaluate=[&](RangeContext* context) {
        return preserved ? changingRange(value,*preserved,owner,context) : std::make_pair(true,range(value,owner,context));
    };
    if(branchDepth_>=maxContextDepth || contextNodes_>=maxContextNodes) {
        contextBudgetHit_=true;return evaluate(parent);
    }
    ++branchDepth_;peakBranchDepth_=std::max(peakBranchDepth_,branchDepth_);
    struct Leave {unsigned& depth;~Leave(){--depth;}} scope{branchDepth_};
    RangeContext branch;
    if(parent) {branch.arguments=parent->arguments;branch.captures=parent->captures;branch.assumptions=parent->assumptions;}
    else if(owner) {branch.arguments=functions_.at(*owner).argumentRanges;branch.captures=functions_.at(*owner).captureRanges;}
    if(!assumeRange(condition,truth,owner,branch)) return {false,{}};
    return evaluate(&branch);
}

std::pair<bool,InterfaceDemand::Bound> InterfaceDemand::changingRange(Ref value,const Leaf& preserved,Owner owner,RangeContext* context) {
    // Identity transfers preserve any established envelope. Other transfers
    // must establish their range under guards from this same observation time.
    while(value->name=="Runtime") value=value->arguments.at(0);
    const auto& a=value->arguments;
    if(value->name=="LoopSlotValue" && Leaf{a[0]->number.convert_to<Id>(),a[1]->number.convert_to<Id>(),a[2]->number.convert_to<std::size_t>()}==preserved)
        return {false,{}};
    if(value->name!="Select") return {true,range(value,owner,context)};
    auto condition=range(a[0],owner,context);
    if(condition && (condition->lower>0 || condition->upper<0)) return changingRange(a[1],preserved,owner,context);
    if(exact(condition) && condition->lower==0) return changingRange(a[2],preserved,owner,context);
    auto yes=branchRange(a[1],a[0],true,owner,context,&preserved);
    auto no=branchRange(a[2],a[0],false,owner,context,&preserved);
    if(!yes.first) return no;
    if(!no.first) return yes;
    return {true,join(yes.second,no.second)};
}

void InterfaceDemand::resetRangeContexts() {
    rangeCache_.clear();rangeContexts_.clear();rangeEnvironments_.clear();rangeQueries_.clear();contextNodes_=0;
}

InterfaceDemand::CallInputs InterfaceDemand::incomingRanges(const Call& call) {
    const auto& fn=functions_.at(call.callee);CallInputs result;
    result.arguments.resize(fn.argumentTypes.size());result.captures.resize(fn.captureTypes.size());
    for(const auto& occurrence:call.occurrences) {
        RangeContext context;
        if(call.owner) {
            context.arguments=functions_.at(*call.owner).argumentRanges;
            context.captures=functions_.at(*call.owner).captureRanges;
        }
        bool possible=true;
        for(const auto& [guard,truth]:occurrence.guards)
            if(!assumeRange(guard,truth,call.owner,context)) {possible=false;break;}
        if(!possible) continue;
        for(unsigned kind=0;kind<2;++kind) {
            const auto& types=kind==0 ? fn.argumentTypes : fn.captureTypes;
            auto& values=kind==0 ? result.arguments : result.captures;
            auto actual=flatten(occurrence.trace->arguments[kind+2]);
            for(std::size_t i=0;i<types.size();++i) {
                auto b=converted(range(actual[i],call.owner),types[i]);
                if(!occurrence.guards.empty()) b=meet(b,converted(range(actual[i],call.owner,&context),types[i]));
                values[i]=result.possible ? join(values[i],b) : b;
            }
        }
        result.possible=true;
    }
    return result;
}

InterfaceDemand::Bound InterfaceDemand::callRange(Id site,std::size_t leaf,Owner owner,RangeContext* context) {
    const auto& call=calls_.at(site);const auto& fn=functions_.at(call.callee);
    auto fallback=fn.resultRanges.at(leaf);
    // Recursion retains the already valid global envelope. Separate sequential
    // invocations may query the same function with different environments.
    if(activeRangeFunctions_.contains(call.callee) || activeRangeCalls_.contains(site)) return fallback;
    if(activeRangeCalls_.size()>=maxContextDepth || contextNodes_>=maxContextNodes) {
        contextBudgetHit_=true;return fallback;
    }
    activeRangeCalls_.insert(site);
    struct Erase {std::set<Id>& set;Id id;~Erase(){set.erase(id);}} siteScope{activeRangeCalls_,site};
    RangeContext inputs;std::vector<Integer> key{Integer(call.callee)};
    for(unsigned kind=0;kind<2;++kind) {
        auto actual=flatten(call.trace->arguments[kind+2]);
        const auto& types=kind==0 ? fn.argumentTypes : fn.captureTypes;
        auto& values=kind==0 ? inputs.arguments : inputs.captures;
        for(std::size_t i=0;i<types.size();++i) {
            auto b=converted(range(actual.at(i),call.owner,owner==call.owner ? context : nullptr),types[i]);
            values.push_back(b);key.push_back(b ? 1 : 0);
            if(b) {key.push_back(b->lower);key.push_back(b->upper);}
        }
    }
    auto found=rangeContexts_.find(key);
    if(found==rangeContexts_.end()) {
        if(rangeContexts_.size()>=maxRangeContexts) {contextBudgetHit_=true;return fallback;}
        found=rangeContexts_.emplace(std::move(key),std::move(inputs)).first;
        peakRangeContexts_=std::max(peakRangeContexts_,static_cast<unsigned>(rangeContexts_.size()));
    }
    activeRangeFunctions_.insert(call.callee);
    Erase functionScope{activeRangeFunctions_,call.callee};
    return meet(fallback,converted(range(fn.results.at(leaf),call.callee,&found->second),fn.resultTypes.at(leaf)));
}

void InterfaceDemand::resolveRanges() {
    // Readiness is part of the proof environment, including later induction
    // hypotheses. Top-level queries invalidate answers when that set changes.
    rangeReady_=ready_;
    for(auto& [id,fn]:functions_) {
        (void)id;
        for(auto [types,ranges]:{std::pair{&fn.argumentTypes,&fn.argumentRanges},std::pair{&fn.captureTypes,&fn.captureRanges},std::pair{&fn.resultTypes,&fn.resultRanges}})
            for(const auto& type:*types) ranges->push_back(native(type));
    }
    for(const auto& [id,loop]:loops_) {
        std::size_t at=0;
        for(const auto& slot:loop.slots) for(std::size_t i=0;i<slot.count;++i)
            loopRanges_.emplace(Leaf{id,slot.root,i},native(loop.trace->arguments[10]->arguments.at(at++)));
    }
    // Descend from valid native domains. Every refinement is sound already;
    // reaching the iteration budget only loses precision, never correctness.
    // Recursive return ranges describe successful returns, not termination.
    constexpr unsigned maxRounds=32;
    for(;rangeRounds_<maxRounds;) {
        ++rangeRounds_;resetRangeContexts();bool changed=false;
        callInputs_.clear();
        for(const auto& [site,call]:calls_) callInputs_.emplace(site,incomingRanges(call));
        std::map<Id,std::array<std::vector<Bound>,3>> next;
        for(const auto& [id,fn]:functions_) {
            auto& n=next[id];n={fn.argumentRanges,fn.captureRanges,fn.resultRanges};
            for(unsigned kind=0;kind<2;++kind) {
                const auto& types=kind==0 ? fn.argumentTypes : fn.captureTypes;
                std::vector<Bound> joined(types.size());bool first=true;
                for(const auto& [site,call]:calls_) if(call.callee==id) {
                    const auto& inputs=callInputs_.at(site);
                    if(!inputs.possible) continue;
                    const auto& actual=kind==0 ? inputs.arguments : inputs.captures;
                    for(std::size_t i=0;i<types.size();++i) {
                        auto b=actual.at(i);
                        joined[i]=first ? b : join(joined[i],b);
                    }
                    first=false;
                }
                if(!first) for(std::size_t i=0;i<types.size();++i) n[kind][i]=meet(n[kind][i],joined[i]);
            }
            for(std::size_t i=0;i<fn.results.size();++i)
                n[2][i]=meet(n[2][i],converted(range(fn.results[i],id),fn.resultTypes[i]));
        }
        auto loopNext=loopRanges_;
        for(const auto& [id,loop]:loops_) {
            const auto& a=loop.trace->arguments;
            std::array<std::vector<Ref>,3> updates{flatten(a[1]),flatten(a[4]),flatten(a[6])};std::size_t at=0;
            for(std::size_t s=0;s<loop.slots.size();++s) {
                const auto& slot=loop.slots[s];bool ready=true;
                for(unsigned j=0;j<3;++j) {auto b=range(a[12+j]->arguments[s],loop.owner);ready=ready && exact(b) && b->lower==1;}
                for(std::size_t i=0;i<slot.count;++i,++at) {
                    if(!ready) continue; // An unwritten slot is not an entry anchor.
                    auto type=a[10]->arguments[at];Leaf key{id,slot.root,i};
                    auto all=converted(range(updates[0].at(at),loop.owner),type);
                    for(unsigned j=1;j<3;++j) {
                        auto change=changingRange(updates[j].at(at),key,loop.owner,nullptr);
                        if(change.first) all=join(all,converted(change.second,type));
                    }
                    loopNext[key]=meet(loopRanges_.at(key),all);
                }
            }
        }
        for(auto& [id,n]:next) {
            auto& fn=functions_.at(id);
            changed=changed || n[0]!=fn.argumentRanges || n[1]!=fn.captureRanges || n[2]!=fn.resultRanges;
            fn.argumentRanges=std::move(n[0]);fn.captureRanges=std::move(n[1]);fn.resultRanges=std::move(n[2]);
        }
        changed=changed || loopNext!=loopRanges_;loopRanges_=std::move(loopNext);
        if(!changed) {rangesConverged_=true;break;}
    }
    resetRangeContexts();
    callInputs_.clear();
    for(const auto& [site,call]:calls_) callInputs_.emplace(site,incomingRanges(call));
    for(const auto& [site,call]:calls_) {
        auto& values=callRanges_[site];
        for(std::size_t i=0;i<functions_.at(call.callee).resultTypes.size();++i)
            values.push_back(range(terms_.make("CallValue",{terms_.number(site),terms_.number(i)}),call.owner));
    }
    rangesReady_=true;
}

void InterfaceDemand::reportRanges(std::ostream& out) const {
    out<<"{\"rounds\":"<<rangeRounds_<<",\"converged\":"<<(rangesConverged_ ? "true" : "false")<<",\"functions\":[";
    auto bounds=[&](const auto& ranges,const auto& types) {
        out<<'[';bool first=true;
        for(std::size_t i=0;i<ranges.size();++i) if(ranges[i] && ranges[i]!=native(types[i])) {
            if(!first) out<<',';
            first=false;out<<"{\"leaf\":"<<i<<",\"min\":"<<ranges[i]->lower<<",\"max\":"<<ranges[i]->upper<<"}";
        }
        out<<']';
    };
    bool first=true;
    for(const auto& [id,fn]:functions_) {
        if(!first) out<<',';
        first=false;out<<"{\"id\":"<<id<<",\"arguments\":";bounds(fn.argumentRanges,fn.argumentTypes);
        out<<",\"captures\":";bounds(fn.captureRanges,fn.captureTypes);
        out<<",\"results\":";bounds(fn.resultRanges,fn.resultTypes);out<<'}';
    }
    out<<"],\"carried\":[";first=true;
    for(const auto& [id,loop]:loops_) {
        std::size_t at=0;
        for(const auto& slot:loop.slots) for(std::size_t i=0;i<slot.count;++i,++at) {
            const auto& b=loopRanges_.at({id,slot.root,i});
            if(!b || b==native(loop.trace->arguments[10]->arguments[at])) continue;
            if(!first) out<<',';
            first=false;out<<"{\"loop\":"<<id<<",\"root\":"<<slot.root<<",\"leaf\":"<<i<<",\"min\":"<<b->lower<<",\"max\":"<<b->upper<<"}";
        }
    }
    out<<"],\"contexts\":{\"limit\":"<<maxRangeContexts<<",\"node_limit\":"<<maxContextNodes
       <<",\"depth_limit\":"<<maxContextDepth<<",\"peak\":"<<peakRangeContexts_
       <<",\"node_peak\":"<<peakContextNodes_<<",\"environment_peak\":"<<peakRangeEnvironments_
       <<",\"query_hits\":"<<rangeQueryHits_<<",\"query_misses\":"<<rangeQueryMisses_
       <<",\"branch_depth_limit\":"<<maxContextDepth<<",\"branch_depth_peak\":"<<peakBranchDepth_
       <<",\"budget_hit\":"<<(contextBudgetHit_ ? "true" : "false")<<"},\"call_results\":[";
    first=true;
    for(const auto& [site,ranges]:callRanges_) {
        const auto& call=calls_.at(site);const auto& general=functions_.at(call.callee).resultRanges;
        for(std::size_t i=0;i<ranges.size();++i) if(ranges[i] && ranges[i]!=general[i]) {
            if(!first) out<<',';
            first=false;out<<"{\"site\":"<<site<<",\"function\":"<<call.callee<<",\"leaf\":"<<i
                <<",\"min\":"<<ranges[i]->lower<<",\"max\":"<<ranges[i]->upper<<"}";
        }
    }
    out<<"],\"call_inputs\":[";first=true;
    for(const auto& [site,inputs]:callInputs_) {
        const auto& call=calls_.at(site);const auto& fn=functions_.at(call.callee);
        auto guarded=std::count_if(call.occurrences.begin(),call.occurrences.end(),[](const auto& occurrence){return !occurrence.guards.empty();});
        if(!guarded) continue;
        if(!first) out<<',';
        first=false;out<<"{\"site\":"<<site<<",\"function\":"<<call.callee<<",\"paths\":"<<call.occurrences.size()
            <<",\"guarded_paths\":"<<guarded<<",\"excluded\":"<<(inputs.possible ? "false" : "true")<<",\"arguments\":";
        bounds(inputs.arguments,fn.argumentTypes);out<<",\"captures\":";bounds(inputs.captures,fn.captureTypes);out<<'}';
    }
    out<<"]}";
}
}
