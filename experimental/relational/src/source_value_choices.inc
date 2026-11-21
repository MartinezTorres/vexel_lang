// Specialize a pure graph under one established predicate. The graph retains
// every integer conversion; a selected value is not an untyped textual alias.
struct ValueChoice {
    Graph& graph;
    std::optional<std::size_t> selected;
    std::size_t guard;
    bool yes;
    const std::map<std::size_t,std::string>& locals;
    FiniteTables& finite;
    std::map<std::size_t,std::size_t> rewritten;

    static unsigned relation(const std::string& op) {
        if(op=="<") return 1;
        if(op=="==") return 2;
        if(op==">") return 4;
        if(op=="<=") return 3;
        if(op==">=") return 6;
        if(op=="!=") return 5;
        return 0;
    }
    std::size_t constant(const Integer& value,Format type) {
        const auto v=FiniteTables::wrap(value,type);
        return graph.add({"constant",v.str(),type,v,v,{}});
    }
    std::pair<Integer,Integer> bounds(std::size_t id) const {
        const auto& n=graph.nodes[id];
        auto lo=n.lo,hi=n.hi;
        const auto& predicate=graph.nodes[guard];
        if(predicate.op!="Compare" && predicate.op!="SignedCompare") return {lo,hi};
        for(unsigned side:{0u,1u}) if(predicate.args[side]==id) {
            const auto& other=graph.nodes[predicate.args[1-side]];
            if(other.op!="constant" || (predicate.op=="Compare" && (lo<0 || other.lo<0))) continue;
            auto known=relation(predicate.data);if(!yes) known=7u^known;
            if(side) known=((known&1u)<<2)|(known&2u)|((known&4u)>>2);
            const auto& k=other.lo;
            if(known==1) hi=std::min(hi,Integer(k-1));
            if(known==3) hi=std::min(hi,k);
            if(known==4) lo=std::max(lo,Integer(k+1));
            if(known==6) lo=std::max(lo,k);
            if(known==2) {lo=std::max(lo,k);hi=std::min(hi,k);}
            if(known==5) {if(lo==k) ++lo;if(hi==k) --hi;}
        }
        // An impossible path is not an additional numeric assumption. Keep
        // the original bounds; branch reachability remains a separate proof.
        return lo<=hi ? std::pair{lo,hi} : std::pair{n.lo,n.hi};
    }
    std::size_t value(std::size_t id) {
        if(auto it=rewritten.find(id);it!=rewritten.end()) return it->second;
        auto n=graph.nodes[id];std::size_t result=id;
        const auto predicate=graph.nodes[guard];
        // Equality to a representable constant fixes that operand throughout
        // this path, not merely the Boolean test. Preserve cast-node identity:
        // knowing a narrowed value does not fix its original wider producer.
        if((predicate.op=="Compare" || predicate.op=="SignedCompare") &&
           ((yes && predicate.data=="==") || (!yes && predicate.data=="!="))) {
            for(unsigned side:{0u,1u}) if(predicate.args[side]==id) {
                const auto other=graph.nodes[predicate.args[1-side]];
                if(other.op=="constant" && other.lo>=n.lo && other.lo<=n.hi &&
                   FiniteTables::wrap(other.lo,n.type)==other.lo &&
                   (predicate.op=="SignedCompare" || (n.lo>=0 && other.lo>=0)))
                    return rewritten[id]=constant(other.lo,n.type);
            }
        }
        if(id==guard && (!yes || (n.lo>=0 && n.hi<=1))) result=constant(yes ? 1 : 0,n.type);
        else if(selected && id==*selected) result=graph.convert(value(n.args[yes ? 1 : 2]),n.type);
        else if(!locals.contains(id) && finite.lookups.contains(id)) {
            auto lookup=finite.lookups.at(id);const auto& table=finite.tables[lookup.table];
            if(table.entries.size()==1 && !table.guarded)
                return rewritten[id]=constant(table.entries[0],n.type);
            // The table proves a total function of its cut. Established facts
            // may simplify that cut without reopening the table's interior or
            // inventing facts about another input. In particular, selecting a
            // shared elapsed-time value must also specialize its lookup index.
            const auto input=value(lookup.input);
            if(graph.nodes[input].op=="constant") {
                auto index=FiniteTables::wrap(graph.nodes[input].lo,{8,false}).convert_to<unsigned>();
                index=(index>>lookup.shift)&lookup.mask;
                const auto evaluated=table.guarded && (index<table.lower || index>=table.limit) ? table.fallback :
                    table.entries.size()==1 ? table.entries.front() : table.entries.at(index);
                return rewritten[id]=constant(evaluated,n.type);
            }
            if(input!=lookup.input) {
                Node specialized{"finite",std::to_string(lookup.table)+":"+std::to_string(lookup.shift)+":"+
                    std::to_string(lookup.mask),n.type,0,0,{input}};
                Graph::bounds(specialized);result=graph.add(std::move(specialized));
                lookup.input=input;
                if(lookup.index) lookup.index=finite.index(lookup);
                finite.lookups[result]=lookup;
            }
        }
        else if(!locals.contains(id) && !n.args.empty()) {
            bool changed=false;
            for(auto& child:n.args) {auto next=value(child);changed|=child!=next;child=next;}
            // Ordering predicates with the same operands share one trichotomy.
            // For example a<b proves a<=b; on its false arm a<=b becomes a==b.
            if((n.op=="Compare" || n.op=="SignedCompare") && n.op==predicate.op && n.args==predicate.args) {
                auto known=relation(predicate.data);if(!yes) known=7u^known;
                const auto intersection=known & relation(n.data);
                if(!intersection) return rewritten[id]=constant(0,n.type);
                if(intersection==known) return rewritten[id]=constant(1,n.type);
                if(intersection==2 && n.data!="==") {n.data="==";changed=true;}
            }
            // Substitution can expose impossible comparisons, e.g. u8 < 0.
            // These are representation bounds, not an assumed gameplay range.
            // Unsigned Compare promotes negative operands to uint32_t; leave
            // that mixed-sign case to the existing lowering.
            if(n.op=="Compare" || n.op=="SignedCompare") {
                const auto [leftLo,leftHi]=bounds(n.args[0]);
                const auto [rightLo,rightHi]=bounds(n.args[1]);
                if(n.op=="SignedCompare" || (leftLo>=0 && rightLo>=0)) {
                    const unsigned possible=n.args[0]==n.args[1] ? 2u :
                        (leftLo<rightHi ? 1u : 0u) |
                        (leftLo<=rightHi && rightLo<=leftHi ? 2u : 0u) |
                        (leftHi>rightLo ? 4u : 0u);
                    const auto intersection=possible & relation(n.data);
                    if(!intersection) return rewritten[id]=constant(0,n.type);
                    if(intersection==possible) return rewritten[id]=constant(1,n.type);
                }
            }
            if(n.op=="Select") {
                const auto condition=graph.nodes[n.args[0]];
                if(condition.op=="constant") return rewritten[id]=graph.convert(n.args[condition.lo!=0 ? 1 : 2],n.type);
                if(n.args[1]==n.args[2]) return rewritten[id]=graph.convert(n.args[1],n.type);
            }
            if(changed) {
                // Modular cancellation does not need a no-overflow assumption.
                if((n.op=="Arithmetic" || n.op=="SignedArithmetic") && n.data=="+") {
                    for(unsigned side:{0u,1u}) {
                        const auto other=graph.nodes[n.args[1-side]];
                        if(other.op==n.op && other.type==n.type && other.data=="-" &&
                           other.args[1]==n.args[side])
                            return rewritten[id]=graph.convert(other.args[0],n.type);
                    }
                }
                if(n.op=="cast") return rewritten[id]=graph.convert(n.args[0],n.type);
                result=graph.add(n);
                if(std::all_of(n.args.begin(),n.args.end(),[&](auto child){return graph.nodes[child].op=="constant";})) {
                    std::map<std::size_t,Integer> cache;
                    const auto evaluated=FiniteTables{graph,{},{},0}.evaluate(result,graph.nodes.size(),0,cache);
                    result=constant(evaluated,n.type);
                }
            }
        }
        rewritten[id]=result;return result;
    }
};
