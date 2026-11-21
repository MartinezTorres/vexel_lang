#include "source_demand.hpp"
#include <algorithm>
#include <deque>
#include <stdexcept>

namespace vx::relational {
namespace {
// Type descriptors and record names are metadata, not executable terms. A
// user's record/field named Invoke or CallValue must never become a call edge.
std::pair<std::size_t,std::size_t> operands(const Ref& value) {
    if(value->name=="Record" || value->name=="Array" || value->name=="Fields" || value->name=="Captures") return {0,0};
    if(value->name=="RecordValue" || value->name=="After") return {1,2};
    return {0,value->arguments.size()};
}
std::vector<Ref> flatten(const Ref& value) {
    std::vector<Ref> result;
    std::function<void(const Ref&)> visit=[&](const Ref& v) {
        if(v->name=="Values") for(const auto& child:v->arguments) visit(child);
        else if(v->name=="RecordValue") visit(v->arguments.at(1));
        else result.push_back(v);
    };visit(value);return result;
}
}

void InterfaceDemand::function(Id id,Function fn) {
    if(fn.results.size()!=fn.resultTypes.size()) throw std::logic_error("interface result type mismatch");
    if(closed_ || !functions_.emplace(id,std::move(fn)).second)
        throw std::logic_error("duplicate or late interface-demand function");
}
void InterfaceDemand::root(Ref trace,std::vector<Ref> observations) {
    if(closed_) throw std::logic_error("late result-demand root");
    roots_.emplace_back(std::move(trace),std::move(observations));
}
void InterfaceDemand::close() {
    if(closed_) throw std::logic_error("result demand already closed");
    enum Kind {Result,CallResult,Argument,Capture,Carried,Ready,Index};
    using Key=std::tuple<Kind,Id,std::size_t,std::size_t>;
    using Keys=std::map<Key,unsigned>;
    // Identity edges and expression transfers form one finite demand lattice:
    // absent < low byte < low word < full word. A stronger caller requirement
    // revisits the producer instead of trusting a previous narrow decision.
    std::map<Key,std::set<Key>> graph;
    struct Transfer {Ref value;Owner owner;};
    std::map<Key,std::vector<Transfer>> transfers;
    Keys live;std::deque<Key> work;
    auto keyWidth=[&](const Key& key) {
        auto [kind,id,index,leaf]=key;Ref type;
        if(kind==Result) type=functions_.at(id).resultTypes.at(index);
        else if(kind==CallResult) type=functions_.at(calls_.at(id).callee).resultTypes.at(index);
        else if(kind==Argument) type=functions_.at(id).argumentTypes.at(index);
        else if(kind==Capture) type=functions_.at(id).captureTypes.at(index);
        else if(kind==Carried) {
            const auto& loop=loops_.at(id);std::size_t at=0;
            for(const auto& slot:loop.slots) {
                if(slot.root==index) {type=loop.trace->arguments[10]->arguments.at(at+leaf);break;}
                at+=slot.count;
            }
        }
        if(type) if(auto f=scalarFormat(type);f && f->bits && f->bits<=32)
            return f->bits<=8 ? 8u : f->bits<=16 ? 16u : 32u;
        return 32u;
    };
    auto require=[&](const Key& key,unsigned bits=32) {
        bits=std::min(bits,keyWidth(key));
        auto& previous=live[key];
        if(bits>previous) {previous=bits;work.push_back(key);}
    };
    // Discover every definition before building edges. Recursive declarations
    // and backedges are ordinary graph edges, independent of discovery order.
    // Incoming parameters survive a loop. Facts over loop/call snapshots do
    // not cross its publication/re-entry boundary without another proof.

    auto acrossLoop=[&](Guards guards) {
        std::erase_if(guards,[&](const auto& guard){return !stableAcrossLoop(guard.first);});return guards;
    };
    auto guarded=[&](Guards guards,const Ref& test,bool truth) {
        if(guards.size()<maxContextDepth) guards.emplace_back(test,truth);
        else contextBudgetHit_=true; // Dropping a premise only loses precision.
        return guards;
    };
    std::function<bool(const Ref&,Owner,Guards)> discover=[&](const Ref& value,Owner owner,Guards guards) {
        const auto& a=value->arguments;const auto& op=value->name;
        if(op=="Sequence") {
            bool crossed=false;
            for(const auto& child:a) if(discover(child,owner,guards)) {guards=acrossLoop(std::move(guards));crossed=true;}
            return crossed;
        }
        if(op=="If") {
            bool yes=discover(a[1],owner,guarded(guards,a[0],true));
            bool no=discover(a[2],owner,guarded(guards,a[0],false));return yes || no;
        }
        if(op=="Loop") {
            auto id=a.at(0)->number.convert_to<Id>();Loop loop;loop.trace=value;loop.owner=owner;
            for(const auto& slot:a.at(11)->arguments)
                loop.slots.push_back({slot->arguments.at(0)->number.convert_to<Id>(),slot->arguments.at(1)->number.convert_to<std::size_t>(),{},false});
            auto [it,inserted]=loops_.emplace(id,std::move(loop));
            if(!inserted && it->second.trace!=value) throw std::logic_error("loop identity collision");
            guards=acrossLoop(std::move(guards));
            // Condition effects precede its truth test. Body actuals already
            // express state updates performed by that test.
            discover(a[2],owner,guards);
            discover(a[5],owner,guarded(guards,a[3],true));return true;
        }
        if(op=="Invoke") {
            auto site=a.at(0)->number.convert_to<Id>(),callee=a.at(1)->number.convert_to<Id>();
            const auto& fn=functions_.at(callee);
            if(a.at(4)->number!=fn.results.size() || flatten(a[2]).size()!=fn.argumentTypes.size() || flatten(a[3]).size()!=fn.captureTypes.size())
                throw std::logic_error("interface-demand call layout mismatch");
            auto [it,inserted]=calls_.emplace(site,Call{callee,owner,value,{}});
            if(!inserted && (it->second.callee!=callee || it->second.owner!=owner)) throw std::logic_error("result-demand call identity collision");
            it->second.occurrences.push_back({value,std::move(guards)});
        }
        return false;
    };
    for(const auto& [id,fn]:functions_) discover(fn.trace,id,{});
    for(const auto& [trace,values]:roots_) {(void)values;discover(trace,{},{});}

    resolveFacts(false);
    resolveRanges();
    normalized_.clear();
    resolveFacts();
    resolveExecutions();

    // Formal inputs have function-local identities; call results and loops
    // already have occurrence identities. Never mix facts from two frames.
    std::map<Owner,std::map<std::pair<const Term*,unsigned>,Keys>> cache;
    std::function<const Keys&(const Ref&,Owner,unsigned)> dependencies=[&](const Ref& value,Owner owner,unsigned bits)->const Keys& {
        auto& memo=cache[owner];
        const auto identity=std::pair{value.get(),bits};
        if(auto found=memo.find(identity);found!=memo.end()) return found->second;
        if(value->arguments.empty()) return memo.emplace(identity,Keys{}).first->second;
        Keys out;
        auto singleton=[&](Key key)->const Keys& {return memo.emplace(identity,Keys{{key,bits}}).first->second;};
        const auto& a=value->arguments;const auto& op=value->name;
        if(op=="FrameArg" || op=="CaptureArg") {
            if(!owner) throw std::logic_error("formal argument outside its interface");
            auto index=a.at(0)->number.convert_to<std::size_t>();const auto& fn=functions_.at(*owner);
            if(index>=(op=="FrameArg" ? fn.argumentTypes.size() : fn.captureTypes.size())) throw std::logic_error("formal outside interface");
            return singleton({op=="FrameArg" ? Argument : Capture,*owner,index,0});
        }
        if(op=="LoopSlotValue") return singleton({Carried,a.at(0)->number.convert_to<Id>(),a.at(1)->number.convert_to<Id>(),a.at(2)->number.convert_to<std::size_t>()});
        if(op=="LoopReady") return singleton({Ready,a.at(0)->number.convert_to<Id>(),a.at(1)->number.convert_to<Id>(),0});
        if(op=="LoopIndex") return singleton({Index,a.at(0)->number.convert_to<Id>(),0,0});
        if(value->name=="CallValue") {
            auto site=value->arguments.at(0)->number.convert_to<Id>();
            if(auto call=calls_.find(site);call!=calls_.end()) {
                auto index=value->arguments.at(1)->number.convert_to<std::size_t>();
                const auto& fn=functions_.at(call->second.callee);
                if(index>=fn.results.size()) throw std::logic_error("result-demand leaf outside interface");
                Key result={CallResult,site,index,0};
                graph[result].insert({Result,call->second.callee,index,0});
                return singleton(result);
            } // Foreign calls and persistent loads retain their full ABI.
        }
        auto [begin,end]=operands(value);
        for(auto i=begin;i<end;++i) {
            unsigned needed=32;
            if(op=="Runtime" || op=="After" || op=="RecordValue" || op=="Values") needed=bits;
            else if(op=="Select" && i!=0) needed=bits;
            else if((op=="Cast" || op=="SignedCast") && i==1)
                needed=std::min(bits,a[0]->number.convert_to<unsigned>());
            else if((op=="Arithmetic" || op=="SignedArithmetic") && i>=2) {
                const auto& operation=a[0]->name;
                const auto width=a[1]->number.convert_to<unsigned>();
                if(operation=="+" || operation=="-" || operation=="*" || operation=="&" || operation=="|" ||
                   operation=="^" || (operation=="<<" && i==2))
                    needed=std::min(bits,width);
                if(width && width<=32 && (operation=="&" || operation=="|")) {
                    const auto& other=a[i==2 ? 3 : 2];
                    if(other->kind==Term::Number) {
                        const Integer mask=(Integer(1)<<std::min(bits,width))-1;
                        Integer observed=(operation=="&" ? other->number : ~other->number)&mask;
                        needed=0;while(observed!=0) {++needed;observed>>=1;}
                    }
                }
                if(i==2 && width && width<=32 && a[3]->kind==Term::Number) {
                    const auto& constant=a[3]->number;
                    if((operation=="<<" || operation==">>") && constant>=0 && constant<width) {
                        const auto count=constant.convert_to<unsigned>();
                        needed=operation=="<<" ? (std::min(bits,width)>count ? std::min(bits,width)-count : 0u) :
                            std::min(width,bits+count);
                    } else if((operation=="/" || operation=="%") && constant>0 && constant<(Integer(1)<<width) &&
                              (constant&(constant-1))==0) {
                        unsigned count=0;for(Integer n=constant;n>1;n>>=1) ++count;
                        if(operation=="/" && op=="Arithmetic") needed=std::min(width,bits+count);
                        // A signed remainder also has its numerator's low k
                        // bits modulo 2^k; its sign matters above those bits.
                        if(operation=="%" && (op=="Arithmetic" || bits<=count)) needed=std::min(bits,count);
                    }
                }
            }
            // Native storage has byte/word/dword choices. Rounding upward is
            // conservative and bounds each interface's fixed-point height.
            needed=needed<=8 ? 8u : needed<=16 ? 16u : 32u;
            const auto& child=dependencies(a.at(i),owner,needed);
            for(auto [key,width]:child) out[key]=std::max(out[key],width);
        }
        return memo.emplace(identity,std::move(out)).first->second;
    };
    auto demand=[&](const Ref& v,Owner owner){for(auto [key,bits]:dependencies(v,owner,32)) require(key,bits);};
    auto edge=[&](Key key,const Ref& value,Owner owner) {
        graph.try_emplace(key);transfers[key].push_back({value,owner});
    };
    std::function<void(const Ref&,Owner)> execution=[&](const Ref& value,Owner owner) {
        const auto& a=value->arguments;const auto& op=value->name;
        if(op=="Sequence") {for(const auto& child:a) execution(child,owner);return;}
        if(op=="If") {demand(a[0],owner);execution(a[1],owner);execution(a[2],owner);return;}
        if(op=="Evaluate") {if(!discardableIn(a[0],owner)) demand(a[0],owner);return;}
        if(op=="Invoke") {
            auto callee=a[1]->number.convert_to<Id>();
            for(unsigned kind=2;kind<=3;++kind) {
                auto actual=flatten(a[kind]);
                for(std::size_t i=0;i<actual.size();++i) {
                    Key formal={kind==2 ? Argument : Capture,callee,i,0};edge(formal,actual[i],owner);
                    // Keep an evaluation-bearing actual in its original call
                    // position. Removing transport must not move a possible
                    // trap across another argument evaluation or a branch.
                    if(!discardableIn(actual[i],owner)) require(formal);
                }
            }
            return;
        }
        if(op=="Loop") {
            auto id=a[0]->number.convert_to<Id>();const auto& loop=loops_.at(id);
            for(unsigned update=0;update<3;++update) {
                auto values=flatten(a[update==0 ? 1 : update==1 ? 4 : 6]);std::size_t at=0;
                for(std::size_t s=0;s<loop.slots.size();++s) {
                    const auto& slot=loop.slots[s];auto ready=a[12+update]->arguments.at(s);
                    Key readiness={Ready,id,slot.root,0};edge(readiness,ready,owner);
                    if(!discardableIn(ready,owner)) require(readiness);
                    for(std::size_t i=0;i<slot.count;++i) {
                        Key carried={Carried,id,slot.root,i};const auto& rhs=values.at(at++);
                        edge(carried,rhs,owner);if(!slot.alwaysReady) graph[carried].insert(readiness);
                        if(!discardableIn(rhs,owner)) require(carried);
                    }
                }
                if(at!=values.size()) throw std::logic_error("loop demand layout mismatch");
            }
            // Control and completion remain obligatory. Return payloads are
            // conservative consumers; this pass trims carried state only.
            for(unsigned index:{3u,7u,8u,9u}) demand(a[index],owner);
            execution(a[2],owner);execution(a[5],owner);return;
        }
        demand(value,owner); // Foreign ABI, storage, checks and other effects.
    };
    for(const auto& [trace,values]:roots_) {execution(trace,{});for(const auto& v:values) demand(v,{});}
    for(const auto& [id,fn]:functions_) {
        execution(fn.trace,id);
        for(std::size_t i=0;i<fn.results.size();++i) {
            edge({Result,id,i,0},fn.results[i],id);
            if(!fn.discardableResults.at(i)) demand(fn.results[i],id);
        }
    }
    // One least fixed point for both directions across calls and loop cycles.
    // A forwarding cycle with no consumer supplies no demand by itself.
    std::set<std::pair<Key,Key>> traversed;
    while(!work.empty()) {
        auto key=work.front();work.pop_front();const auto bits=live.at(key);
        for(const auto& next:graph[key]) {require(next,bits);traversed.emplace(key,next);}
        for(const auto& transfer:transfers[key])
            for(auto [next,width]:dependencies(transfer.value,transfer.owner,bits)) {
                require(next,width);traversed.emplace(key,next);
            }
    }
    for(const auto& [site,call]:calls_) {
        const auto& fn=functions_.at(call.callee);
        if(!fn.discardableExecution) continue;
        bool observed=false;
        for(std::size_t i=0;i<fn.results.size();++i) observed|=live.contains({CallResult,site,i,0});
        if(observed) continue;
        // Actuals still have their original evaluation position. Proving a
        // callee harmless does not permit erasing a trapping argument.
        bool total=true;
        for(const auto& occurrence:call.occurrences) {
            auto trace=normalize(occurrence.trace,call.owner);
            for(unsigned kind=2;kind<=3;++kind) for(const auto& value:flatten(trace->arguments.at(kind)))
                total=total && discardableIn(value,call.owner);
        }
        if(total) discardedCalls_.insert(site);
    }
    nodes=graph.size();required=live.size();edges=traversed.size();
    for(auto& [id,fn]:functions_) {
        auto width=[&](Kind kind,std::size_t i) {auto found=live.find({kind,id,i,0});return found==live.end() ? 0u : found->second;};
        for(std::size_t i=0;i<fn.results.size();++i) {
            fn.resultBits.push_back(width(Result,i));
            if(fn.resultBits.back()) {auto offset=fn.slots.size();fn.slots.emplace(i,offset);}
        }
        for(std::size_t i=0;i<fn.argumentTypes.size();++i) {
            fn.argumentBits.push_back(width(Argument,i));if(fn.argumentBits.back()) fn.arguments.insert(i);
        }
        for(std::size_t i=0;i<fn.captureTypes.size();++i) {
            fn.captureBits.push_back(width(Capture,i));if(fn.captureBits.back()) fn.captures.insert(i);
        }
    }
    for(auto& [id,loop]:loops_) {
        loop.index=live.contains({Index,id,0,0});
        for(auto& slot:loop.slots) {
            slot.ready=live.contains({Ready,id,slot.root,0});
            for(std::size_t i=0;i<slot.count;++i) {
                auto found=live.find({Carried,id,slot.root,i});loop.bits.push_back(found==live.end() ? 0u : found->second);
                if(loop.bits.back()) {auto offset=slot.leaves.size();slot.leaves.emplace(i,offset);}
            }
        }
    }
    planStorage();
    closed_=true;
}
Ref InterfaceDemand::rewrite(const Ref& source,Owner owner) {
    if(!closed_) throw std::logic_error("unclosed result demand");
    auto& cache=rewritten_[owner];
    if(auto found=cache.find(source);found!=cache.end()) return found->second;
    auto value=normalize(source,owner);
    if(value->arguments.empty()) {cache.emplace(source,value);return value;}
    auto rewrite=[&](const Ref& v){return this->rewrite(v,owner);};
    auto result=value;
    if(value->name=="Evaluate" && discardableIn(value->arguments.at(0),owner)) result=terms_.make("Done");
    else if(value->name=="After") result=rewrite(value->arguments.at(1));
    else if(value->name=="Invoke" && discardedCalls_.contains(value->arguments.at(0)->number.convert_to<Id>()))
        result=terms_.make("Done");
    else if(value->name=="Invoke") {
        auto args=value->arguments;const auto& fn=functions_.at(args.at(1)->number.convert_to<Id>());
        for(unsigned kind=2;kind<=3;++kind) {
            auto actual=flatten(args[kind]);std::vector<Ref> kept;
            for(auto i:kind==2 ? fn.arguments : fn.captures)
                kept.push_back(narrowTransport(rewrite(actual.at(i)),
                    (kind==2 ? fn.argumentTypes : fn.captureTypes).at(i),
                    (kind==2 ? fn.argumentStorageTypes : fn.captureStorageTypes).at(i)));
            args[kind]=terms_.make("Values",kept);
        }
        args.at(4)=terms_.number(fn.slots.size());std::vector<Ref> types;
        for(const auto& [logical,physical]:fn.slots) {(void)physical;types.push_back(fn.resultStorageTypes.at(logical));}
        args.push_back(terms_.make("Types",types));result=terms_.make("Invoke",args);
    } else if(value->name=="Loop") {
        auto args=value->arguments;const auto& loop=loops_.at(args[0]->number.convert_to<Id>());
        std::vector<Ref> slots,types;std::size_t at=0;
        for(const auto& slot:loop.slots) {
            if(slot.ready || !slot.leaves.empty()) slots.push_back(terms_.make("Slot",{terms_.number(slot.root),terms_.number(slot.leaves.size()),terms_.number(slot.alwaysReady ? 0 : 1)}));
            for(std::size_t i=0;i<slot.count;++i,++at) if(slot.leaves.contains(i)) types.push_back(loop.storageTypes.at(at));
        }
        for(unsigned update=0;update<3;++update) {
            auto position=update==0 ? 1u : update==1 ? 4u : 6u;
            auto flat=flatten(args[position]);std::vector<Ref> values,ready;at=0;
            for(std::size_t s=0;s<loop.slots.size();++s) {
                const auto& slot=loop.slots[s];
                if(slot.ready || !slot.leaves.empty()) ready.push_back(rewrite(args[12+update]->arguments.at(s)));
                for(std::size_t i=0;i<slot.count;++i,++at) if(slot.leaves.contains(i))
                    values.push_back(narrowTransport(rewrite(flat.at(at)),loop.trace->arguments[10]->arguments.at(at),loop.storageTypes.at(at)));
            }
            args[position]=terms_.make("Values",values);args[12+update]=terms_.make("Ready",ready);
        }
        for(unsigned index:{2u,3u,5u,7u,8u,9u}) args[index]=rewrite(args[index]);
        args[10]=terms_.make("Types",types);args[11]=terms_.make("Slots",slots);
        args.push_back(terms_.number(loop.index ? 1 : 0));result=terms_.make("Loop",args);
    } else if(value->name=="LoopSlotValue") {
        auto args=value->arguments;const auto& loop=loops_.at(args[0]->number.convert_to<Id>());
        auto root=args[1]->number.convert_to<Id>();auto index=args[2]->number.convert_to<std::size_t>();
        auto slot=std::find_if(loop.slots.begin(),loop.slots.end(),[&](const auto& s){return s.root==root;});
        if(slot==loop.slots.end()) throw std::logic_error("missing carried slot");
        args[3]=carriedStorage(args[0]->number.convert_to<Id>(),root,index);
        args[2]=terms_.number(slot->leaves.at(index));result=terms_.make("LoopSlotValue",args);
    }
    else if(!value->arguments.empty()) {
        auto args=value->arguments;
        auto [begin,end]=operands(value);
        for(auto i=begin;i<end;++i) args.at(i)=rewrite(args.at(i));
        if(value->name=="CallValue") {
            auto site=args.at(0)->number.convert_to<Id>();
            if(auto call=calls_.find(site);call!=calls_.end()) {
                auto index=args.at(1)->number.convert_to<std::size_t>();
                const auto& fn=functions_.at(call->second.callee);
                result=terms_.make("CResult",{args[0],terms_.number(fn.slots.at(index)),fn.resultStorageTypes.at(index)});
                cache.emplace(source,result);return result;
            }
        }
        if(args!=value->arguments) result=terms_.make(value->name,args);
    }
    cache.emplace(source,result);return result;
}
}
