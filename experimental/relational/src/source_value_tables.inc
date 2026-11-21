// Finite realization of a certified total value graph. Enumerating one byte
// is an exact evaluation, not a profile, a guessed range, or source execution.
// Tables remain private to the consuming function's ROM bank.
struct FiniteTables {
    struct Lookup {
        std::size_t input=0,table=0;
        unsigned shift=0,mask=255;
        std::optional<std::size_t> index={};
    };
    struct Table {
        Format storage;
        std::vector<Integer> entries;
        Integer fallback;
        bool guarded=false;
        unsigned limit=0;
        unsigned lower=0;
    };
    Graph& graph;
    std::map<std::size_t,Lookup> lookups;
    std::vector<Table> tables;
    std::size_t bytes=0;
    unsigned byteLimit=512,searchLimit=16;
    std::size_t nativeConstants=0,nativeGuards=0,nativeArithmetic=0;

    static int linearStep(const std::vector<Integer>& entries,Format type) {
        if(entries.size()<2) return 0;
        for(int step:{1,-1}) {
            bool exact=true;
            for(std::size_t i=0;i<entries.size();++i)
                if(wrap(entries.front()+Integer(i)*step,type)!=wrap(entries[i],type)) {exact=false;break;}
            if(exact) return step;
        }
        return 0;
    }

    std::size_t index(const Lookup& lookup) {
        auto result=graph.convert(lookup.input,{8,false});
        auto operation=[&](const std::string& op,unsigned value,unsigned maximum) {
            const auto literal=graph.add({"constant",std::to_string(value),{8,false},value,value,{}});
            result=graph.add({"Arithmetic",op,{8,false},0,maximum,{result,literal}});
        };
        if(lookup.shift) operation(">>",lookup.shift,255u>>lookup.shift);
        if(lookup.mask!=(255u>>lookup.shift)) operation("&",lookup.mask,lookup.mask);
        return result;
    }
    void prepareIndices() {
        // Index arithmetic belongs to the implementation graph too. Equal
        // shifts/masks across different tables can use one stable producer.
        // A full graph simply retains the original inline index realization.
        Graph before=graph;
        try {
            for(auto& [id,lookup]:lookups) {
                (void)id;const auto& table=tables[lookup.table];
                if(table.entries.size()!=1 || table.guarded) lookup.index=index(lookup);
            }
        } catch(const Unavailable&) {
            graph=std::move(before);
            for(auto& [id,lookup]:lookups) {(void)id;lookup.index.reset();}
        }
    }

    std::vector<std::size_t> lowerNative(const std::vector<std::size_t>& roots) {
        // A finite certificate can expose an ordinary value graph, not just
        // opaque ROM. Rejoin branch specialization and sharing before emission.
        // Keep general indexed data as a cut. Constants, immediate choices and
        // one-operation linear runs are cheap enough to expose as native code.
        if(lookups.empty()) return roots;
        Graph before=graph;const auto oldLookups=lookups;
        const auto oldConstants=nativeConstants,oldGuards=nativeGuards,oldArithmetic=nativeArithmetic;
        std::map<std::size_t,std::size_t> rewritten;
        std::set<std::size_t> liveLookups;
        auto constant=[&](const Integer& value,Format type) {
            auto v=wrap(value,type);
            return graph.add({"constant",v.str(),type,v,v,{}});
        };
        auto visit=[&](auto&& self,std::size_t id)->std::size_t {
            if(auto it=rewritten.find(id);it!=rewritten.end()) return it->second;
            auto node=graph.nodes[id];auto result=id;
            if(auto it=lookups.find(id);it!=lookups.end()) {
                auto lookup=it->second;const auto table=tables[lookup.table];
                const auto linear=linearStep(table.entries,node.type);
                if(table.entries.size()==1 || linear) {
                    result=constant(table.entries.front(),node.type);
                    std::optional<std::size_t> nativeIndex;
                    if(table.guarded || linear) lookup.input=self(self,lookup.input);
                    if(linear) nativeIndex=index(lookup);
                    if(linear) {
                        auto input=graph.convert(*nativeIndex,node.type);
                        if(linear==1 && graph.nodes[result].lo==0) result=input;
                        else {
                            Node operation{"Arithmetic",linear==1 ? "+" : "-",node.type,0,0,{result,input}};
                            Graph::bounds(operation);result=graph.add(std::move(operation));
                        }
                        ++nativeArithmetic;
                    }
                    if(table.guarded) {
                        const auto length=table.limit-table.lower;
                        std::size_t condition;
                        // An aligned power-of-two interval is a masked bit
                        // pattern. Test the original byte directly; shifting
                        // a one-bit index back down costs several Z80 rotates.
                        if(length && (length&(length-1))==0 && (table.lower&(length-1))==0) {
                            const auto mask=(lookup.mask&~(length-1))<<lookup.shift;
                            auto input=graph.convert(lookup.input,{8,false});
                            if(mask!=255) input=graph.add({"Arithmetic","&",{8,false},0,mask,
                                {input,constant(mask,{8,false})}});
                            condition=mask ? graph.add({"Compare","==",{8,false},0,1,
                                {input,constant(table.lower<<lookup.shift,{8,false})}}) : constant(1,{8,false});
                        } else {
                            auto input=nativeIndex ? *nativeIndex : index(lookup);
                            if(table.lower) input=graph.add({"Arithmetic","-",{8,false},0,255,
                                {input,constant(table.lower,{8,false})}});
                            condition=graph.add({"Compare","<",{8,false},0,1,
                                {input,constant(length,{8,false})}});
                        }
                        auto fallback=constant(table.fallback,node.type);
                        auto lo=std::min(graph.nodes[result].lo,graph.nodes[fallback].lo);
                        auto hi=std::max(graph.nodes[result].hi,graph.nodes[fallback].hi);
                        result=graph.add({"Select","",node.type,lo,hi,{condition,result,fallback}});
                        ++nativeGuards;
                    } else if(!linear) ++nativeConstants;
                    lookups.erase(id);
                } else {
                    auto input=self(self,lookup.input);
                    if(input!=lookup.input) {
                        node={"finite",std::to_string(lookup.table)+":"+std::to_string(lookup.shift)+":"+
                            std::to_string(lookup.mask),node.type,0,0,{input}};
                        Graph::bounds(node);result=graph.add(std::move(node));
                        lookup.input=input;lookup.index=index(lookup);
                        lookups.erase(id);lookups[result]=lookup;
                    }
                    liveLookups.insert(result);
                }
            } else {
                bool changed=false;
                for(auto& child:node.args) {auto next=self(self,child);changed|=child!=next;child=next;}
                if(changed) result=graph.add(std::move(node));
            }
            rewritten[id]=result;return result;
        };
        try {
            std::vector<std::size_t> result;
            for(auto root:roots) result.push_back(visit(visit,root));
            // Native rewrites and enclosing cuts can make earlier tables dead.
            // Keep stable table identities, but charge and emit only live data.
            std::erase_if(lookups,[&](const auto& item){return !liveLookups.contains(item.first);});
            bytes=0;
            for(auto table:storedTables()) bytes+=tables[table].entries.size()*(tables[table].storage.bits/8);
            return result;
        } catch(const Unavailable&) {
            graph=std::move(before);lookups=oldLookups;
            nativeConstants=oldConstants;nativeGuards=oldGuards;nativeArithmetic=oldArithmetic;
            return roots;
        }
    }

    std::set<std::size_t> storedTables() const {
        std::set<std::size_t> result;
        for(const auto& [id,lookup]:lookups) {
            (void)id;if(tables[lookup.table].entries.size()>1) result.insert(lookup.table);
        }
        return result;
    }

    static std::string literal(const Integer& value,Format type) {
        if(value==-2147483648LL) return "(-2147483647L - 1L)";
        return value.str()+(type.bits==32 ? (type.sign ? "L" : "UL") : "");
    }

    static Integer wrap(Integer value,Format type) {
        const Integer modulus=Integer(1)<<type.bits;
        value%=modulus;if(value<0) value+=modulus;
        if(type.sign && value>=(modulus>>1)) value-=modulus;
        return value;
    }
    Integer evaluate(std::size_t id,std::size_t input,const Integer& sample,
                     std::map<std::size_t,Integer>& cache) const {
        if(id==input) return sample;
        if(auto it=cache.find(id);it!=cache.end()) return it->second;
        const auto& n=graph.nodes[id];Integer result;
        auto arg=[&](std::size_t i){return evaluate(n.args[i],input,sample,cache);};
        if(n.op=="constant") result=n.lo;
        else if(n.op=="cast") result=wrap(arg(0),n.type);
        else if(n.op=="Select") result=wrap(arg(arg(0)!=0 ? 1 : 2),n.type);
        else if(n.op=="Not") result=arg(0)==0;
        else if(n.op=="And") result=arg(0)!=0 && arg(1)!=0;
        else if(n.op=="Compare" || n.op=="SignedCompare") {
            auto a=arg(0),b=arg(1);
            if(n.op=="Compare") {
                const auto& left=graph.nodes[n.args[0]];const auto& right=graph.nodes[n.args[1]];
                const auto width=left.lo<0 || right.lo<0 ? 32u : std::max(left.type.bits,right.type.bits);
                a=wrap(a,{width,false});b=wrap(b,{width,false});
            }
            if(n.data=="==") result=a==b;
            else if(n.data=="!=") result=a!=b;
            else if(n.data=="<") result=a<b;
            else if(n.data=="<=") result=a<=b;
            else if(n.data==">") result=a>b;
            else if(n.data==">=") result=a>=b;
            else throw Unavailable{"unsupported finite comparison"};
        } else if(n.op=="Arithmetic" || n.op=="SignedArithmetic") {
            const auto a=wrap(arg(0),n.type),b=wrap(arg(1),n.type);
            if(n.data=="+") result=a+b;
            else if(n.data=="-") result=a-b;
            else if(n.data=="*") result=a*b;
            else if(n.data=="/" || n.data=="%") {
                if(b==0) throw Unavailable{"finite candidate has an undefined divisor"};
                if(n.data=="/") result=a/b;else result=a%b;
            }
            else if(n.data=="<<") result=wrap(a,{n.type.bits,false})<<b.convert_to<unsigned>();
            else if(n.data==">>") result=a>>b.convert_to<unsigned>();
            else if(n.data=="&") result=wrap(a,{n.type.bits,false}) & wrap(b,{n.type.bits,false});
            else if(n.data=="|") result=wrap(a,{n.type.bits,false}) | wrap(b,{n.type.bits,false});
            else if(n.data=="^") result=wrap(a,{n.type.bits,false}) ^ wrap(b,{n.type.bits,false});
            else throw Unavailable{"unsupported finite arithmetic"};
            result=wrap(result,n.type);
        } else throw Unavailable{"non-finite value leaf"};
        cache.emplace(id,result);return result;
    }
    Integer sample(std::size_t input,unsigned pattern) const {
        const auto& n=graph.nodes[input];auto value=wrap(pattern,n.type);
        // A source check or the computed producer establishes this envelope.
        // Outside it the table is unreachable. Give those entries a defined
        // representative, rather than executing an impossible zero divisor or
        // shift while considering an optional physical realization.
        return value<n.lo || value>n.hi ? n.lo : value;
    }
    bool realize(std::size_t id,std::size_t input,const std::vector<Integer>& values,std::size_t allowance,
                 std::optional<std::size_t> eliminated={}) {
        const auto& n=graph.nodes[id];
        unsigned shift=0,period=256;
        // Prove ignored low bits and periodicity over ALL 256
        // inputs before reducing the index. No range check is lost.
        while(shift<7) {
            const auto step=1u<<(shift+1);bool same=true;
            for(unsigned i=0;i<256;++i) if(values[i]!=values[i & ~(step-1)]) {same=false;break;}
            if(!same) break;
            ++shift;
        }
        while(period>1 && std::equal(values.begin(),values.begin()+period/2,values.begin()+period/2)) period/=2;
        auto size=std::max(1u,period>>shift);
        std::vector<Integer> entries;
        for(unsigned i=0;i<size;++i) entries.push_back(values[i<<shift]);
        Format storage=n.type;
        // A modular word displacement may fit in a signed byte.
        // Cast it back to the ORIGINAL result format at the load.
        for(unsigned bits:{8u,16u,32u}) {
            if(bits>n.type.bits) break;
            bool found=false;
            for(bool sign:{false,true}) {
                Format trial{bits,sign};
                if(std::all_of(entries.begin(),entries.end(),[&](const Integer& v){return wrap(wrap(v,trial),n.type)==v;})) {
                    storage=trial;found=true;break;
                }
            }
            if(found) break;
        }
        for(auto& v:entries) v=wrap(v,storage);
        const auto complete=entries;
        auto fallback=entries.back();auto count=entries.size();
        while(count>1 && entries[count-1]==fallback) --count;
        // One extra bounds branch is worthwhile only for a sizable
        // constant suffix. Otherwise keep a single indexed load.
        // A computed cut is evaluated once by an indexed load. Tail guards
        // would repeat that producer in the comparison and the table index.
        bool guarded=graph.nodes[input].op=="input" && (entries.size()-count)*(storage.bits/8)>=16;
        if(guarded) entries.resize(count);
        unsigned limit=guarded ? static_cast<unsigned>(count) : 0;
        unsigned lower=0;
        // One exceptional contiguous run, including a singleton, is a native
        // equality or interval test. This also handles a two-entry periodic
        // function after its ignored bits have been proved and removed.
        auto first=std::find_if(complete.begin(),complete.end(),[&](const Integer& v){return v!=complete.front();});
        if(first!=complete.end()) {
            auto last=std::find_if(first,complete.end(),[&](const Integer& v){return v!=*first;});
            if(last==complete.end()) {
                entries={complete.front()};fallback=*first;guarded=true;
                limit=static_cast<unsigned>(first-complete.begin());
            } else if(std::all_of(last,complete.end(),[&](const Integer& v){return v==complete.front();})) {
                entries={*first};fallback=complete.front();guarded=true;
                lower=static_cast<unsigned>(first-complete.begin());limit=static_cast<unsigned>(last-complete.begin());
            }
        }
        // A complete certificate with two adjacent constant runs is one
        // unsigned byte threshold. Test the original byte once, eliminating
        // index arithmetic and ROM even when the cut itself is computed.
        const auto boundary=std::find_if(values.begin(),values.end(),[&](const Integer& v){return v!=values.front();});
        if(boundary!=values.end() && std::all_of(boundary,values.end(),[&](const Integer& v){return v==*boundary;})) {
            limit=static_cast<unsigned>(boundary-values.begin());guarded=true;
            entries={wrap(values.front(),storage)};fallback=wrap(*boundary,storage);
            shift=0;size=256;lower=0;
        }
        if(eliminated) {
            const auto linear=linearStep(entries,n.type);
            const auto indexCost=(shift ? 1u : 0u)+(size-1!=(255u>>shift) ? 1u : 0u);
            std::size_t replacement;
            if(entries.size()==1 || linear) {
                replacement=linear ? indexCost+(linear==1 && wrap(entries.front(),n.type)==0 ? 0u : 1u) : 0;
                if(guarded) {
                    const auto length=limit-lower;
                    const bool aligned=length && (length&(length-1))==0 && (lower&(length-1))==0;
                    replacement+=4; // one comparison and one selection
                    if(aligned) replacement+=(((size-1)&~(length-1))<<shift)!=255 ? 1u : 0u;
                    else replacement+=(linear ? 0u : indexCost)+(lower ? 1u : 0u);
                }
            } else replacement=4+indexCost+(storage.bits/8-1)*2+(guarded ? 4u : 0u);
            // A proof may merely rediscover already cheap native code. Keep
            // that graph and its demand order unless the replacement removes
            // more estimated work than it introduces. This is a cost gate,
            // independent of the exhaustive equality certificate above.
            if(*eliminated<=replacement) return false;
        }
        // One entry is an immediate, optionally guarded by the suffix test.
        // There is no indexed data to store. Reusing a table likewise spends
        // no additional ROM and remains possible at the byte/table ceiling.
        const auto cost=entries.size()==1 ? 0 : entries.size()*(storage.bits/8);
        std::size_t table=0;
        for(;table<tables.size();++table) {
            const auto& t=tables[table];
            if(t.storage==storage && t.entries==entries && t.fallback==fallback && t.guarded==guarded && t.limit==limit && t.lower==lower) break;
        }
        if(table==tables.size()) {
            if(tables.size()>=8 || cost>256 || bytes+cost>byteLimit || cost>allowance) return false;
            tables.push_back({storage,std::move(entries),fallback,guarded,limit,lower});bytes+=cost;
        }
        lookups.emplace(id,Lookup{input,table,shift,size-1});return true;
    }
    void plan(const std::vector<std::size_t>& roots) {
        // Bound both solver work and ROM growth. Mutable region state and
        // expensive unbounded searches retain their ordinary lowering.
        if(graph.region || !byteLimit) return;
        std::vector<std::set<std::size_t>> dependencies(graph.nodes.size()),selectors(graph.nodes.size());
        for(std::size_t id=0;id<graph.nodes.size();++id) {
            const auto& n=graph.nodes[id];
            if(n.op=="input") dependencies[id].insert(id);
            if(n.op=="Select") selectors[id].insert(id);
            for(auto child:n.args) {
                dependencies[id].insert(dependencies[child].begin(),dependencies[child].end());
                selectors[id].insert(selectors[child].begin(),selectors[child].end());
            }
        }
        std::set<std::size_t> visited;
        // Rejected outer candidates may share most of their graph with the
        // next candidate. Evaluate each (node, byte pattern) at most once.
        // Eligible nodes have exactly one input, so its identity is already
        // part of the interned node, including signed input interpretation.
        std::vector<std::map<std::size_t,Integer>> samples(256);
        std::function<void(std::size_t)> choose=[&](std::size_t id) {
            if(!visited.insert(id).second) return;
            const auto& n=graph.nodes[id];
            // The total byte budget remains the ROM bound. Several small
            // command fields must not crowd out a later expensive field.
            if(dependencies[id].size()==1 && selectors[id].size()>=4) {
                auto input=*dependencies[id].begin();
                if(graph.nodes[input].type.bits==8) {
                    std::vector<Integer> values;
                    try {
                        for(unsigned pattern=0;pattern<256;++pattern)
                            values.push_back(evaluate(id,input,sample(input,pattern),samples[pattern]));
                        if(realize(id,input,values,selectors[id].size()*16)) return;
                    } catch(const Unavailable&) {} // Keep this candidate's native realization.
                }
            }
            for(auto child:n.args) choose(child);
        };
        for(auto root:roots) choose(root);
        if(!searchLimit) return;
        // A finite input need not be a source parameter. Any byte-valued node
        // through which all nonconstant paths pass is a valid cut: its producer
        // stays executable and the total function after it can be enumerated.
        // Intersections prove independence from every other source input.
        std::vector<std::set<std::size_t>> cuts(graph.nodes.size());
        for(std::size_t id=0;id<graph.nodes.size();++id) {
            const auto& n=graph.nodes[id];bool first=true;
            for(auto child:n.args) if(!dependencies[child].empty()) {
                if(first) {cuts[id]=cuts[child];first=false;}
                else {
                    std::set<std::size_t> common;
                    std::set_intersection(cuts[id].begin(),cuts[id].end(),cuts[child].begin(),cuts[child].end(),
                        std::inserter(common,common.end()));
                    cuts[id]=std::move(common);
                }
            }
            if(n.type.bits==8 && !dependencies[id].empty()) cuts[id].insert(id);
            // Keep both early (often compressible) and nearby cuts. Dropping
            // candidates affects policy only; it cannot create a false cut.
            while(cuts[id].size()>8) cuts[id].erase(std::next(cuts[id].begin(),4));
        }
        auto work=[&](std::size_t root,std::size_t input)->std::optional<std::size_t> {
            std::set<std::size_t> seen;std::vector<std::size_t> pending{root};
            std::map<std::size_t,std::size_t> costs;
            while(!pending.empty()) {
                auto id=pending.back();pending.pop_back();
                if(id==input || dependencies[id].empty() || !seen.insert(id).second) continue;
                // Preserve existing lookup realizations and bound enumeration
                // work. A missed optimization keeps the original graph.
                if(seen.size()>64 || lookups.contains(id)) return {};
                const auto& n=graph.nodes[id];
                if(n.op=="input") return {};
                // A selected value needs a conditional transfer and result
                // selection as well as its separately counted comparison.
                // This remains a bounded target heuristic, not a speed proof.
                auto& cost=costs[id];cost=n.op=="cast" ? 0 : n.op=="Select" ? 3 : 1;
                if(n.data=="*") {
                    bool shift=false;
                    for(auto child:n.args) if(graph.nodes[child].op=="constant") {
                        Integer factor=graph.nodes[child].lo&((Integer(1)<<n.type.bits)-1);
                        if(factor!=0 && (factor&(factor-1))!=0) factor=(Integer(1)<<n.type.bits)-factor;
                        shift=factor==0 || (factor&(factor-1))==0;
                    }
                    if(!shift) cost+=15;
                }
                if(n.data=="/" || n.data=="%") {
                    const auto& operand=graph.nodes[n.args[1]];
                    Integer divisor=operand.lo;
                    if(divisor<0) divisor=-divisor;
                    if(operand.lo!=operand.hi) cost+=n.type.bits*3u;
                    else if((divisor&(divisor-1))!=0) {
                        const Integer maximum=n.type.sign ? Integer(1)<<(n.type.bits-1) : (Integer(1)<<n.type.bits)-1;
                        for(Integer threshold=divisor;threshold<=maximum;threshold<<=1) cost+=3;
                    }
                }
                pending.insert(pending.end(),n.args.begin(),n.args.end());
            }
            // A candidate saves only operations that disappear after its
            // replacement. Other result paths and earlier table cuts may
            // still require a shared producer; charging its whole subtree
            // would incorrectly make a cheap predicate look expensive.
            std::vector<unsigned> uses(graph.nodes.size());
            std::set<std::size_t> live;
            auto count=[&](auto&& self,std::size_t id)->void {
                ++uses[id];if(!live.insert(id).second) return;
                if(auto found=lookups.find(id);found!=lookups.end()) {
                    const auto& table=tables[found->second.table];
                    if(table.entries.size()!=1 || table.guarded) self(self,found->second.input);
                } else for(auto child:graph.nodes[id].args) self(self,child);
            };
            for(auto id:roots) count(count,id);
            std::size_t saved=0;pending={root};
            while(!pending.empty()) {
                auto id=pending.back();pending.pop_back();
                if(id==input || !costs.contains(id)) continue;
                saved+=costs.at(id);
                for(auto child:graph.nodes[id].args) if(--uses[child]==0) pending.push_back(child);
            }
            return saved;
        };
        std::set<std::size_t> cutVisited;std::size_t attempts=0;
        std::function<void(std::size_t)> chooseCut=[&](std::size_t id) {
            if(lookups.contains(id) || !cutVisited.insert(id).second) return;
            if(attempts<searchLimit) for(auto input:cuts[id]) {
                if(input==id) continue;
                auto cost=work(id,input);if(!cost || *cost<4) continue;
                if(attempts++>=searchLimit) break;
                // Cache values only within this cut: another intermediate
                // denotes a different environment for the same graph node.
                std::vector<Integer> values;
                try {
                    for(unsigned pattern=0;pattern<256;++pattern) {
                        std::map<std::size_t,Integer> cache;
                        values.push_back(evaluate(id,input,sample(input,pattern),cache));
                    }
                    if(realize(id,input,values,*cost*16,*cost)) return;
                } catch(const Unavailable&) {} // A failed cut is not a source error.
            }
            for(auto child:graph.nodes[id].args) chooseCut(child);
        };
        for(auto root:roots) chooseCut(root);
    }
    void declare(std::ostream& out,const std::string& pad) const {
        for(auto i:storedTables()) {
            const auto& table=tables[i];
            if(table.entries.size()==1) continue;
            out<<pad<<"/* Exhaustive byte-domain realization; immutable, bank-local ROM. */\n"
               <<pad<<"static const "<<table.storage.c()<<" vx_lut_"<<i<<"[] = {";
            for(std::size_t j=0;j<table.entries.size();++j) {
                if(j) out<<',';
                if(j%16==0) out<<'\n'<<pad<<"    ";
                out<<literal(table.entries[j],table.storage);
            }
            out<<"\n"<<pad<<"};\n";
        }
    }
};
