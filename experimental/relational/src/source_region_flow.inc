// Control flow for a region's physical realization. Values remain pure DAGs;
// only transfers, decisions and synchronous boundaries have program positions.
using Variables=std::set<std::size_t>;
using Facts=std::map<std::size_t,bool>;
struct Step {
    enum Kind {Move,Branch,Call,Check,Stop} kind=Move;
    Transfer moves;
    std::size_t value=0;
    std::vector<std::size_t> next;
    Variables uses,defs;
    std::string boundary;
    unsigned fault=0;
};
std::vector<Step> flow;
std::map<std::string,std::vector<std::size_t>> boundaryArgs,boundaryResults;
std::map<std::size_t,Variables> dependencies;
std::size_t step(Step s) {flow.push_back(std::move(s));return flow.size()-1;}
std::size_t move(Transfer t,std::size_t next) {
    Step s;s.moves=std::move(t);s.next={next};return step(std::move(s));
}
std::size_t branch(std::size_t value,std::size_t yes,std::size_t no) {
    Step s;s.kind=Step::Branch;s.value=value;s.next={yes,no};return step(std::move(s));
}
std::size_t literal(Integer value,Format f={}) {
    value&=(Integer(1)<<f.bits)-1;
    if(f.sign && value>=(Integer(1)<<(f.bits-1))) value-=Integer(1)<<f.bits;
    return graph.add({"constant",value.str(),f,value,value,{}});
}
const Variables& variables(std::size_t id) {
    if(auto it=dependencies.find(id);it!=dependencies.end()) return it->second;
    Variables out;const auto& n=graph.nodes[id];
    if(n.op=="input") out.insert(id);
    for(auto child:n.args) {const auto& v=variables(child);out.insert(v.begin(),v.end());}
    return dependencies.emplace(id,std::move(out)).first->second;
}
std::size_t simplify(std::size_t id,const Facts& facts,std::map<std::size_t,std::size_t>& cache,
                     const std::map<std::size_t,std::size_t>& aliases={}) {
    if(auto it=cache.find(id);it!=cache.end()) return it->second;
    Node n=graph.nodes[id];
    if(auto it=aliases.find(id);it!=aliases.end()) return cache[id]=it->second;
    if(auto it=facts.find(id);it!=facts.end())
        if(!it->second || (n.lo>=0 && n.hi<=1)) return cache[id]=literal(it->second ? 1 : 0,n.type);
    if(n.op=="Select") {
        if(auto it=facts.find(n.args[0]);it!=facts.end())
            return cache[id]=graph.convert(simplify(n.args[it->second ? 1 : 2],facts,cache,aliases),n.type);
    }
    for(auto& child:n.args) child=simplify(child,facts,cache,aliases);
    auto constant=[&](unsigned i){return graph.nodes[n.args[i]].op=="constant";};
    auto number=[&](unsigned i)->Integer{return graph.nodes[n.args[i]].lo;};
    if(n.op=="Select" && (constant(0) || n.args[1]==n.args[2]))
        return cache[id]=graph.convert(n.args[constant(0) && number(0)==0 ? 2 : 1],n.type);
    if(n.op=="cast") {
        if(constant(0)) return cache[id]=literal(number(0),n.type);
        if(graph.nodes[n.args[0]].type==n.type) return cache[id]=n.args[0];
    }
    if(n.op=="Not" && constant(0)) return cache[id]=literal(number(0)==0 ? 1 : 0);
    if(n.op=="And" && ((constant(0) && number(0)==0) || (constant(1) && number(1)==0)))
        return cache[id]=literal(0);
    if((n.op=="Compare" || n.op=="SignedCompare") && constant(0) && constant(1)) {
        Integer a=number(0),b=number(1);
        if(n.op=="Compare" && (a<0 || b<0)) {a&=0xffffffff; b&=0xffffffff;}
        bool v=n.data=="==" ? a==b : n.data=="!=" ? a!=b : n.data=="<" ? a<b :
               n.data=="<=" ? a<=b : n.data==">" ? a>b : a>=b;
        return cache[id]=literal(v ? 1 : 0);
    }
    if(n.op=="Arithmetic" || n.op=="SignedArithmetic") {
        if(constant(1) && number(1)==0 && (n.data=="+" || n.data=="-" || n.data=="|" || n.data=="^"))
            return cache[id]=graph.convert(n.args[0],n.type);
        if(constant(0) && constant(1)) {
            Integer a=number(0),b=number(1),v;bool folded=true;
            if(n.data=="+") v=a+b;else if(n.data=="-") v=a-b;else if(n.data=="*") v=a*b;
            else if(n.data=="&") v=a&b;else if(n.data=="|") v=a|b;else if(n.data=="^") v=a^b;
            else folded=false;
            if(folded) return cache[id]=literal(v,n.type);
        }
    }
    return cache[id]=graph.add(std::move(n));
}
std::size_t lower(const Ref& trace,std::size_t next) {
    const auto& a=trace->arguments;const auto& op=trace->name;
    if(op=="Done" || op=="Evaluate") return next; // pure total values have no effect
    if(op=="Sequence") {for(auto it=a.rbegin();it!=a.rend();++it) next=lower(*it,next);return next;}
    if(op=="If") {
        auto yes=lower(a[1],next),no=lower(a[2],next);
        return yes==no ? yes : branch(graph.value(a[0]),yes,no);
    }
    if(op=="Loop") {
        const auto& updates=transfers.at(trace.get());
        auto test=step(Step{});auto head=lower(a[2],test);
        auto index=graph.runtime.at("LoopIndex:"+show(a[0]));
        auto advance=head;
        if(live.contains(index)) {
            Node n{"Arithmetic","+",{32,false},0,0,{index,literal(1)}};Graph::bounds(n);
            advance=move({{index,graph.add(n)}},head);
        }
        auto tail=move(updates[2],advance);
        if(!zero(a[7])) tail=branch(graph.value(a[7]),move(updates[2],next),tail);
        auto bodyEntry=lower(a[5],tail);
        auto leave=move(updates[1],next);
        if(!zero(a[9])) leave=branch(graph.value(a[9]),move(updates[1],head),leave);
        flow[test].kind=Step::Branch;flow[test].value=graph.value(a[3]);flow[test].next={bodyEntry,leave};
        auto init=updates[0];if(live.contains(index)) init.push_back({index,literal(0)});
        return move(std::move(init),head);
    }
    if(op=="ExternalCall") {
        auto symbolName=a[1]->name;const auto& b=boundaries.at(symbolName);
        const auto& outputs=boundaryResults.at(symbolName);Transfer snapshots;
        for(std::size_t i=0;i<outputs.size();++i) {
            auto dest=graph.runtime.at("CallValue:"+show(a[0])+":"+std::to_string(i));
            if(dest!=outputs[i]) snapshots.push_back({dest,outputs[i]});
        }
        next=move(std::move(snapshots),next);
        for(std::size_t i=0;i<outputs.size();++i) if(b.signature.outputs[i]->name=="Bool") {
            auto predicate=graph.add({"Compare","<=",{},0,1,{outputs[i],literal(1)}});
            Step check;check.kind=Step::Check;check.value=predicate;check.fault=16;check.next={next};next=step(std::move(check));
        }
        Step call;call.kind=Step::Call;call.boundary=symbolName;call.next={next};
        call.uses.insert(boundaryArgs.at(symbolName).begin(),boundaryArgs.at(symbolName).end());
        call.defs.insert(outputs.begin(),outputs.end());next=step(std::move(call));
        auto actual=flatten(a[2]);const auto& args=boundaryArgs.at(symbolName);
        if(actual.size()!=args.size()) throw Unavailable{"region boundary argument layout mismatch"};
        Transfer publish;for(std::size_t i=0;i<args.size();++i) publish.push_back({args[i],graph.value(actual[i])});
        return move(std::move(publish),next);
    }
    Step check;check.kind=Step::Check;check.value=graph.value(a[0]);check.next={next};check.fault=19;
    if(op=="CheckIndex") {
        check.value=graph.add({"Compare","<",{},0,1,{graph.convert(check.value,{32,false}),literal(a[1]->number,{32,false})}});
        check.fault=16;
    } else if(op!="CheckSchema") throw Unavailable{"unsupported region execution: "+op};
    return step(std::move(check));
}
std::vector<std::size_t> reachable(std::size_t entry) {
    std::vector<std::size_t> order;Variables visited;
    std::function<void(std::size_t)> visit=[&](std::size_t at) {
        if(!visited.insert(at).second) return;
        order.push_back(at);for(auto to:flow[at].next) visit(to);
    };visit(entry);return order;
}
void accesses() {
    for(auto& s:flow) {
        if(s.kind==Step::Call || s.kind==Step::Stop) continue;
        s.uses.clear();s.defs.clear();
        if(s.kind==Step::Move) for(const auto& a:s.moves) {
            s.defs.insert(a.destination);const auto& v=variables(a.value);s.uses.insert(v.begin(),v.end());
        } else s.uses=variables(s.value);
    }
}
void splitTransfers(std::size_t at,unsigned& budget) {
    if(!budget || flow[at].kind!=Step::Move || flow[at].moves.size()<2) return;
    std::map<std::size_t,unsigned> guards;
    for(const auto& a:flow[at].moves) {
        auto value=a.value;
        while(graph.nodes[value].op=="cast") value=graph.nodes[value].args[0];
        if(graph.nodes[value].op=="Select") ++guards[graph.nodes[value].args[0]];
    }
    auto best=guards.end();
    for(auto it=guards.begin();it!=guards.end();++it)
        if(it->second>1 && (best==guards.end() || it->second>best->second)) best=it;
    if(best==guards.end()) return;
    --budget;auto guard=best->first;auto original=flow[at].moves;auto next=flow[at].next[0];
    std::vector<std::size_t> successors;
    for(bool choice:{true,false}) {
        auto transfersForArm=original;std::map<std::size_t,std::size_t> cache;
        for(auto& a:transfersForArm) a.value=simplify(a.value,{{guard,choice}},cache);
        successors.push_back(move(std::move(transfersForArm),next));
    }
    flow[at].kind=Step::Branch;flow[at].value=guard;flow[at].moves.clear();flow[at].next=successors;
    for(auto to:successors) splitTransfers(to,budget);
}
bool specialize(std::size_t entry,bool split) {
    // Establish canonical predicates first. Readiness folding may have left
    // identity casts around a predicate used both by a branch and a Select.
    // Facts about one spelling must also specialize the other spelling.
    std::map<std::size_t,std::size_t> normalized;
    for(auto& s:flow) {
        for(auto& a:s.moves) a.value=simplify(a.value,{},normalized);
        if(s.kind==Step::Branch || s.kind==Step::Check) s.value=simplify(s.value,{},normalized);
    }
    // Decide a shared Select before writing any of its destinations. Besides
    // avoiding repeated tests, this exposes separate lifetimes for the arms;
    // an inactive loop result no longer creates a speculative read at entry.
    if(split) {
        auto originalSize=flow.size();
        for(std::size_t at=0;at<originalSize;++at) {unsigned budget=8;splitTransfers(at,budget);}
    }
    accesses();auto order=reachable(entry);
    std::vector<std::optional<Facts>> incoming(flow.size());incoming[entry]=Facts{};
    bool changed=true;
    while(changed) {
        changed=false;
        for(auto at:order) if(incoming[at]) {
            auto facts=*incoming[at];const auto& s=flow[at];
            std::erase_if(facts,[&](const auto& fact) {
                const auto& uses=variables(fact.first);
                for(auto id:s.defs) if(uses.contains(id)) return true;
                return false;
            });
            for(std::size_t edge=0;edge<s.next.size();++edge) {
                auto out=facts;if(s.kind==Step::Branch) out[s.value]=edge==0;
                auto& to=incoming[s.next[edge]];
                if(!to) {to=std::move(out);changed=true;}
                else {
                    auto before=to->size();
                    std::erase_if(*to,[&](const auto& f){auto it=out.find(f.first);return it==out.end() || it->second!=f.second;});
                    if(before!=to->size()) changed=true;
                }
            }
        }
    }
    bool simplified=false;
    // Thread an edge through a repeated decision when that edge already knows
    // its outcome. Merging the edge's facts at the decision first would lose
    // this correlation and keep inactive loop snapshots live at region entry.
    for(auto at:order) if(incoming[at]) {
        auto facts=*incoming[at];auto& s=flow[at];
        std::erase_if(facts,[&](const auto& fact) {
            const auto& uses=variables(fact.first);
            for(auto id:s.defs) if(uses.contains(id)) return true;
            return false;
        });
        for(std::size_t edge=0;edge<s.next.size();++edge) {
            auto known=facts;if(s.kind==Step::Branch) known[s.value]=edge==0;
            auto to=s.next[edge];Variables visited;std::map<std::size_t,std::size_t> cache;
            while(visited.insert(to).second) {
                const auto& target=flow[to];
                if(target.kind==Step::Move && target.moves.empty()) {to=target.next[0];continue;}
                if(target.kind!=Step::Branch) break;
                auto predicate=simplify(target.value,known,cache);
                if(graph.nodes[predicate].op!="constant") break;
                to=target.next[graph.nodes[predicate].lo!=0 ? 0 : 1];
            }
            if(to!=s.next[edge]) {s.next[edge]=to;simplified=true;}
        }
    }
    for(auto at:order) {
        auto& s=flow[at];std::map<std::size_t,std::size_t> cache;
        const Facts facts=incoming[at].value_or(Facts{});
        for(auto& a:s.moves) {auto before=a.value;a.value=simplify(a.value,facts,cache);simplified|=before!=a.value;}
        if(s.kind==Step::Branch || s.kind==Step::Check) {
            auto before=s.value;s.value=simplify(s.value,facts,cache);simplified|=before!=s.value;
            if(s.kind==Step::Branch && graph.nodes[s.value].op=="constant") {
                s.next={s.next[graph.nodes[s.value].lo!=0 ? 0 : 1]};s.kind=Step::Move;simplified=true;
            }
        }
    }
    return simplified;
}
using LiveSets=std::vector<Variables>;
void lowerDecisions(std::size_t entry) {
    // A branch consumes truth, not a stored Boolean. Follow pure selection
    // edges directly, preserving short circuiting and narrowing at each arm.
    // Calls/checks remain in the surrounding flow; no source is read earlier.
    for(auto at:reachable(entry)) {
        if(flow[at].kind!=Step::Branch) continue;
        const auto original=flow[at];unsigned budget=64;
        std::map<std::tuple<std::size_t,std::size_t,std::size_t>,std::size_t> memo;
        std::function<std::size_t(std::size_t,std::size_t,std::size_t)> decision=
            [&](std::size_t id,std::size_t yes,std::size_t no)->std::size_t {
                if(yes==no) return yes;
                auto key=std::make_tuple(id,yes,no);
                if(auto found=memo.find(key);found!=memo.end()) return found->second;
                const auto n=graph.nodes[id];
                if(n.op=="constant") return n.lo!=0 ? yes : no;
                if(!budget || flow.size()>=4096) return branch(id,yes,no);
                --budget;std::size_t next;
                if(n.op=="cast" && n.type.bits>=graph.nodes[n.args[0]].type.bits)
                    next=decision(n.args[0],yes,no);
                else if(n.op=="Not") next=decision(n.args[0],no,yes);
                else if(n.op=="And") {
                    auto right=decision(n.args[1],yes,no);
                    next=decision(n.args[0],right,no);
                } else if(n.op=="Select") {
                    auto left=graph.convert(n.args[1],n.type),right=graph.convert(n.args[2],n.type);
                    auto a=decision(left,yes,no),b=decision(right,yes,no);
                    next=decision(n.args[0],a,b);
                } else if((n.op=="Compare" || n.op=="SignedCompare") && (n.data=="==" || n.data=="!=")) {
                    auto zero=[&](std::size_t child){return graph.nodes[child].op=="constant" && graph.nodes[child].lo==0;};
                    if(zero(n.args[0]) || zero(n.args[1]))
                        next=decision(n.args[zero(n.args[1]) ? 0 : 1],n.data=="!=" ? yes : no,n.data=="!=" ? no : yes);
                    else next=branch(id,yes,no);
                } else next=branch(id,yes,no);
                memo.emplace(key,next);return next;
            };
        auto next=decision(original.value,original.next[0],original.next[1]);
        if(flow[next].kind==Step::Branch && flow[next].value==original.value && flow[next].next==original.next) continue;
        flow[at].kind=Step::Move;flow[at].moves.clear();flow[at].next={next};
        ++result.regionDirectDecisions;
    }
}
std::pair<LiveSets,LiveSets> liveness(std::size_t entry) {
    accesses();auto order=reachable(entry);LiveSets in(flow.size()),out(flow.size());bool changed=true;
    while(changed) {
        changed=false;
        for(auto it=order.rbegin();it!=order.rend();++it) {
            auto at=*it;const auto& s=flow[at];Variables after;
            for(auto to:s.next) after.insert(in[to].begin(),in[to].end());
            auto before=s.uses;for(auto id:after) if(!s.defs.contains(id)) before.insert(id);
            if(before!=in[at] || after!=out[at]) {in[at]=std::move(before);out[at]=std::move(after);changed=true;}
        }
    }
    return {in,out};
}
bool prune(std::size_t entry) {
    bool removed=false;
    for(;;) {
        auto [in,out]=liveness(entry);bool changed=false;
        for(auto at:reachable(entry)) {
            auto& s=flow[at];auto before=s.moves.size();
            std::erase_if(s.moves,[&](const Assignment& a){
                return !out[at].contains(a.destination) || a.destination==a.value || graph.nodes[a.destination].op!="input";
            });
            changed|=before!=s.moves.size();
        }
        if(!changed) break;
        removed=true;
    }
    return removed;
}
void coalesce(std::size_t entry) {
    auto [in,out]=liveness(entry);auto order=reachable(entry);
    std::map<std::size_t,Variables> conflict;Variables candidates;
    for(const auto& [key,id]:graph.runtime)
        if((key.starts_with("Loop") || key.starts_with("CallValue:")) &&
           graph.nodes[id].op=="input" && graph.nodes[id].data.find('.')==std::string::npos)
            candidates.insert(id);
    auto interfere=[&](std::size_t a,std::size_t b){if(a!=b) {conflict[a].insert(b);conflict[b].insert(a);}};
    // Entry values are independently published. Elsewhere, definitions create
    // interference: a copy establishes equality, so its source and destination
    // may remain live together until one is redefined. A clique at every read
    // would lose that equality and forbid sharing a loop counter with its
    // published boundary argument. Parallel destinations still conflict.
    for(auto a:in[entry]) for(auto b:in[entry]) interfere(a,b);
    for(auto at:order) {
        for(auto a:flow[at].defs) {
            std::optional<std::size_t> copied;
            if(flow[at].kind==Step::Move) for(const auto& assignment:flow[at].moves)
                if(assignment.destination==a && graph.nodes[assignment.value].op=="input" &&
                   graph.nodes[a].type==graph.nodes[assignment.value].type) copied=assignment.value;
            for(auto b:out[at]) if(!copied || b!=*copied) interfere(a,b);
            for(auto b:flow[at].defs) interfere(a,b);
        }
    }
    std::map<std::size_t,std::size_t> aliases;
    auto representative=[&](std::size_t id) {while(aliases.contains(id)) id=aliases.at(id);return id;};
    for(auto at:order) for(const auto& a:flow[at].moves) {
        if(graph.nodes[a.value].op!="input") continue;
        auto x=representative(a.destination),y=representative(a.value);
        if(x==y || graph.nodes[x].type!=graph.nodes[y].type) continue;
        if(!candidates.contains(x)) std::swap(x,y);
        if(!candidates.contains(x) || conflict[x].contains(y)) continue;
        // Only internal loop fields and call snapshots disappear. Each call
        // defines its boundary result fields, so interference preserves an old
        // snapshot whenever it must survive another call to that boundary.
        // Disjoint call sites can consume the published result in place.
        // Published inputs, results and boundary fields retain their ABI names.
        aliases[x]=y;
        result.regionCoalescedBytes+=graph.nodes[x].type.bits/8;
        // A published input may have a narrower initial domain. Once reused
        // for loop state its physical cell must admit the full storage type.
        Graph::bounds(graph.nodes[y]);
        for(auto other:conflict[x]) {conflict[other].erase(x);conflict[other].insert(y);conflict[y].insert(other);}
        conflict.erase(x);
    }
    for(auto& [id,to]:aliases) to=representative(to);
    std::map<std::size_t,std::size_t> cache;
    for(auto at:order) {
        auto& s=flow[at];
        for(auto& a:s.moves) {a.destination=representative(a.destination);a.value=simplify(a.value,{},cache,aliases);}
        if(s.kind==Step::Branch || s.kind==Step::Check) s.value=simplify(s.value,{},cache,aliases);
    }
    prune(entry);
}
void allocateBoundaries(std::size_t entry) {
    // A boundary borrows its complete typed publication object during the
    // synchronous call. Replies can remain live afterwards; coalesced loop
    // values can also extend an argument's lifetime. Use the final scalar
    // identities, not source scopes or the order of external declarations.
    auto [in,out]=liveness(entry);
    std::map<std::size_t,std::string> owners;
    std::map<std::string,std::size_t> bytes;
    std::map<std::string,std::set<std::string>> conflicts;
    std::set<std::string> dedicated;
    for(const auto& [symbolName,b]:boundaries) {
        std::size_t size=0;
        for(const auto& type:b.signature.inputs) size+=format(type).bits/8;
        for(const auto& type:b.signature.outputs) size+=format(type).bits/8;
        bytes[symbolName]=std::max(std::size_t(1),size);
        result.regionBoundaryUnsharedBytes+=bytes[symbolName];
        // Aliases exposed inside the enclosing C function can increase SDCC's
        // register pressure. Prefer a dedicated object for direct expansions;
        // aggregate expansions already retain a target call boundary. Mode 2
        // offers all legal sharing for measurement. This is a cost preference,
        // deliberately separate from the lifetime conflict proof below.
        if(boundaryOverlay==1 && !b.signature.expansion.empty() && b.signature.outputs.size()<=1) {
            dedicated.insert(symbolName);result.regionBoundaryDedicated.push_back(symbolName);
        }
        for(auto id:boundaryArgs.at(symbolName)) owners.emplace(id,symbolName);
        for(auto id:boundaryResults.at(symbolName)) owners.emplace(id,symbolName);
    }
    for(auto at:reachable(entry)) {
        const auto& s=flow[at];std::set<std::string> active;
        const Variables* valuesAtStep[]={&in[at],&out[at],&s.uses,&s.defs};
        for(const auto* values:valuesAtStep)
            for(auto id:*values) if(auto found=owners.find(id);found!=owners.end()) active.insert(found->second);
        if(s.kind==Step::Call) active.insert(s.boundary);
        // Include both reads and writes of the same transfer. The scalar copy
        // scheduler knows logical identities, not union offsets: sharing two
        // objects in a multi-field handoff could otherwise corrupt a later RHS.
        for(const auto& a:active) for(const auto& b:active) if(a<b) {
            conflicts[a].insert(b);conflicts[b].insert(a);
        }
    }
    for(const auto& [a,others]:conflicts) for(const auto& b:others) if(a<b)
        result.regionBoundaryConflicts.emplace_back(a,b);
    std::vector<std::string> order;
    for(const auto& [symbolName,b]:boundaries) {(void)b;order.push_back(symbolName);}
    std::sort(order.begin(),order.end(),[&](const auto& a,const auto& b) {
        return bytes[a]!=bytes[b] ? bytes[a]>bytes[b] : a<b;
    });
    for(const auto& symbolName:order) {
        auto chosen=result.regionBoundarySlots.size();
        if(boundaryOverlay && !dedicated.contains(symbolName)) for(std::size_t i=0;i<result.regionBoundarySlots.size();++i) {
            const auto& slot=result.regionBoundarySlots[i];
            if(std::none_of(slot.members.begin(),slot.members.end(),[&](const auto& other){return dedicated.contains(other) || conflicts[symbolName].contains(other);})) {
                chosen=i;break;
            }
        }
        if(chosen==result.regionBoundarySlots.size()) result.regionBoundarySlots.push_back({bytes[symbolName],{}});
        result.regionBoundarySlots[chosen].members.push_back(symbolName);
    }
    // Preserve the old declaration order when no sharing is possible/allowed.
    for(auto& slot:result.regionBoundarySlots) std::sort(slot.members.begin(),slot.members.end());
    std::sort(result.regionBoundarySlots.begin(),result.regionBoundarySlots.end(),[](const auto& a,const auto& b){return a.members<b.members;});
    for(std::size_t i=0;i<result.regionBoundarySlots.size();++i) {
        const auto& slot=result.regionBoundarySlots[i];result.regionBoundaryBytes+=slot.bytes;
        for(const auto& symbolName:slot.members)
            boundaryStorage[symbolName]=(slot.members.size()>1 ? "boundary_slot_"+std::to_string(i)+"." : "")+"boundary_"+symbolName;
    }
    for(const auto& [id,symbolName]:owners) {
        auto& field=graph.nodes[id].data;
        field=boundaryStorage.at(symbolName)+field.substr(field.find('.'));
    }
    // Physical field paths changed, so the old interning keys are no longer
    // valid. All graph edges and liveness identities remain unchanged.
    graph.reindex();
}
bool predicateKernel(std::size_t root) {
    // The native call has already published its values. A pure function may
    // borrow those stable fields without scalar argument publication. Start
    // with a bounded family: one dynamic byte operation on leaves, surrounded
    // by casts/constant byte operations and comparisons. There are no stores,
    // callbacks, selections, shared computed intermediates or wide operands.
    // Other predicates keep their existing materialization/operand safeguards.
    Variables visited,inputs;unsigned isolated=0;
    auto leaf=[&](std::size_t id) {
        while(graph.nodes[id].op=="cast" && graph.nodes[id].type.bits==8) id=graph.nodes[id].args[0];
        return graph.nodes[id].op=="input" && graph.nodes[id].type.bits==8;
    };
    std::function<bool(std::size_t)> visit=[&](std::size_t id) {
        const auto& n=graph.nodes[id];
        if(!visited.insert(id).second) return n.op=="input" || n.op=="constant" || n.op=="cast";
        if(visited.size()>24 || n.type.bits!=8) return false;
        if(n.op=="input") {inputs.insert(id);return inputs.size()<=2;}
        if(n.op=="constant") return true;
        if(n.op=="cast" || n.op=="Not") return visit(n.args[0]);
        if(n.op!="Arithmetic" && n.op!="SignedArithmetic" && n.op!="Compare" && n.op!="SignedCompare") return false;
        bool compare=n.op=="Compare" || n.op=="SignedCompare";
        if(!compare && n.data!="+" && n.data!="-" && n.data!="&" && n.data!="|" && n.data!="^") return false;
        bool dynamic=graph.nodes[n.args[0]].op!="constant" && graph.nodes[n.args[1]].op!="constant";
        if(dynamic) {
            if(!leaf(n.args[0]) || !leaf(n.args[1])) return false;
            bool guarded=compare ? n.op=="Compare" && n.data!="==" && n.data!="!=" &&
                !graph.nodes[n.args[0]].type.sign && !graph.nodes[n.args[1]].type.sign : n.data=="+" || n.data=="-";
            if(!guarded || ++isolated>1) return false;
        }
        return visit(n.args[0]) && visit(n.args[1]);
    };
    return visit(root) && isolated==1;
}
std::string flowCondition(std::size_t id) {
    if(predicateFunctions && (predicates.contains(id) || (predicates.size()<16 && predicateKernel(id)))) {
        auto found=predicates.find(id);
        if(found==predicates.end()) {
            auto function="vx_bind_"+name+"_predicate_"+std::to_string(predicates.size());
            Emitter kernel{graph,helpers};kernel.inputPrefix=frame+".";
            // Unlike the enclosing effect region, this function only reads
            // its inputs. The real call boundary isolates previous stores.
            const auto& root=graph.nodes[id];
            const bool boolean=root.op=="Compare" || root.op=="SignedCompare" || root.op=="Not";
            const auto value=kernel.expression(id,{}, {},true);
            emitter.runtimeHelpers.insert(kernel.runtimeHelpers.begin(),kernel.runtimeHelpers.end());
            // Comparisons already return 0/1. SDCC retains both inversions
            // in !!comparison, so do not add redundant truth conversion.
            helpers<<"static uint8_t "<<function<<"(VX_BIND_"<<name<<"_PREDICATE_PARAMETERS) {\n"
                <<"    return "<<(boolean ? value : "("+value+") != 0")<<";\n}\n";
            found=predicates.emplace(id,function).first;++result.regionPredicateFunctions;
        }
        ++result.regionPredicateCalls;
        return found->second+"(VX_BIND_"+name+"_TRANSFER_ARGUMENTS)";
    }
    // Volatile state prevents SDCC from sharing repeated reads/calculations in
    // a large predicate. Materialize its shared pure DAG in owned scratch just
    // as a transfer does, instead of growing the hardware stack with spills.
    std::map<std::size_t,unsigned> uses;Variables visited;
    std::function<void(std::size_t)> count=[&](std::size_t at) {
        ++uses[at];if(!visited.insert(at).second) return;
        for(auto child:graph.nodes[at].args) count(child);
    };count(id);
    bool shared=false;
    for(const auto& [at,n]:uses) {
        const auto& op=graph.nodes[at].op;
        shared|=n>1 && op!="input" && op!="constant" && op!="cast";
    }
    if(!shared) return emitter.expression(id,{}, {},true);
    auto f=graph.nodes[id].type;scratchCounts["test_"+f.c()]=0;
    auto temp=scratch(f,"test");block({id},{f},{temp},"    ");return temp;
}
void generateFlow() {
    for(const auto& [symbolName,b]:boundaries) {
        // A source may have no arguments; a sink may have no results. Their
        // empty publication sets still participate in ordered effect flow.
        boundaryArgs.try_emplace(symbolName);
        boundaryResults.try_emplace(symbolName);
        for(std::size_t i=0;i<b.signature.inputs.size();++i) {
            Node n{"input","boundary_"+symbolName+".a"+std::to_string(i),format(b.signature.inputs[i]),0,0,{}};Graph::bounds(n);
            boundaryArgs[symbolName].push_back(graph.add(n));
        }
        for(std::size_t i=0;i<b.signature.outputs.size();++i) {
            Node n{"input","boundary_"+symbolName+".r"+std::to_string(i),format(b.signature.outputs[i]),0,0,{}};Graph::bounds(n);
            boundaryResults[symbolName].push_back(graph.add(n));
        }
    }
    Step stop;stop.kind=Step::Stop;Transfer outputs;
    for(std::size_t i=0;i<roots.size();++i) {
        Node n{"input","result.v"+std::to_string(i),formats[i],0,0,{}};Graph::bounds(n);auto id=graph.add(n);
        outputs.push_back({id,roots[i]});stop.uses.insert(id);
    }
    auto entry=lower(execution,move(std::move(outputs),step(std::move(stop))));
    // Removing an impossible edge or identity store can expose a loop
    // invariant previously obscured by that edge/store. Recompute facts until
    // neither the expressions, edges nor transfers change. Splitting is bounded
    // and happens only once; subsequent iterations only simplify/remove work.
    bool first=true;
    for(;;) {
        bool changed=specialize(entry,first);first=false;
        changed=prune(entry) || changed;if(!changed) break;
    }
    lowerDecisions(entry);
    for(;;) {
        bool changed=specialize(entry,false);
        changed=prune(entry) || changed;if(!changed) break;
    }
    // At entry, liveness contains exactly the values that can be read before
    // a definition on some path. Initialize these only; publication already
    // defines input fields. This also covers speculative total Select operands.
    auto [in,out]=liveness(entry);Variables published;
    for(const auto& [key,id]:graph.inputs) published.insert(id);
    Transfer init;
    for(auto id:in[entry]) if(!published.contains(id)) {
        init.push_back({id,literal(0,graph.nodes[id].type)});
        result.regionZeroBytes+=graph.nodes[id].type.bits/8;
    }
    if(!init.empty()) entry=move(std::move(init),entry);
    coalesce(entry);allocateBoundaries(entry);accesses();live.clear();
    auto order=reachable(entry);
    for(auto at:order) {
        live.insert(flow[at].uses.begin(),flow[at].uses.end());live.insert(flow[at].defs.begin(),flow[at].defs.end());
    }
    for(auto id:live) {
        const auto& n=graph.nodes[id];if(n.data.find('.')==std::string::npos) fields[n.data]=n.type;
    }
    // Skip empty transfers, then place the true successor next when possible.
    // Labels preserve loop/backedge semantics without introducing call depth.
    auto skip=[&](std::size_t at) {
        Variables visited;
        while(flow[at].kind==Step::Move && flow[at].moves.empty() && visited.insert(at).second) at=flow[at].next[0];
        return at;
    };
    entry=skip(entry);for(auto& s:flow) for(auto& to:s.next) to=skip(to);
    order=reachable(entry);
    Variables labels;
    for(std::size_t i=0;i<order.size();++i) {
        const auto& s=flow[order[i]];
        if(s.kind==Step::Branch) labels.insert(s.next[1]);
        if(!s.next.empty() && (i+1==order.size() || order[i+1]!=s.next[0])) labels.insert(s.next[0]);
    }
    for(std::size_t i=0;i<order.size();++i) {
        auto at=order[i];const auto& s=flow[at];if(labels.contains(at)) body<<"vx_region_"<<at<<":;\n";
        auto jump=[&](std::size_t to) {if(i+1==order.size() || order[i+1]!=to) body<<"    goto vx_region_"<<to<<";\n";};
        if(s.kind==Step::Move) assign(s.moves,"    ");
        else if(s.kind==Step::Branch) {
            auto predicate=flowCondition(s.value);
            body<<"    if (!("<<predicate<<")) goto vx_region_"<<s.next[1]<<";\n";
        } else if(s.kind==Step::Call) body<<"    VX_BIND_"<<name<<"_CALL_"<<s.boundary<<"();\n";
        else if(s.kind==Step::Check) {
            auto predicate=flowCondition(s.value);body<<"    if (!("<<predicate<<")) vx_trap("<<s.fault<<");\n";
        }
        else body<<"    return;\n";
        if(!s.next.empty()) jump(s.next[0]);
    }
}
