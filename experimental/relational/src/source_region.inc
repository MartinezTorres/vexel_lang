// A typed realization of an entire execution trace. Effects remain ordered;
// expression sharing is confined to a single transfer between effects.
namespace {
struct RegionEmitter {
    Graph graph;
    std::string name,source,frame;
    const Ref& execution;
    bool scheduled,transferFunctions,predicateFunctions;
    unsigned boundaryOverlay;
    struct Assignment {std::size_t destination,value;};
    using Transfer=std::vector<Assignment>;
    struct Boundary {
        RegionBoundary signature;
        std::vector<std::string> sites;
    };
    std::map<std::string,Boundary> boundaries;
    std::map<std::string,std::string> boundaryStorage;
    std::map<const Term*,std::vector<Transfer>> transfers;
    std::set<std::size_t> live;
    std::map<std::string,Format> fields;
    std::map<std::string,unsigned> scratchCounts,scratchMax;
    std::map<std::string,Format> scratchTypes;
    std::vector<std::size_t> roots;
    std::vector<Format> formats;
    std::ostringstream body;
    std::ostringstream helpers;
    unsigned helperCount=0;
    std::map<std::size_t,std::string> predicates;
    Emitter emitter;
    ValueBindingProjection result;

    RegionEmitter(std::string n,std::string s,const Ref& trace,bool schedule,bool functions,unsigned overlay,bool predicatesEnabled,bool reuseScratch,unsigned linearTerms):
        name(std::move(n)),source(std::move(s)),frame("vx_bind_"+name+"_frame"),
        execution(trace),scheduled(schedule),transferFunctions(functions),predicateFunctions(predicatesEnabled),boundaryOverlay(overlay),emitter{graph,body} {
        graph.region=true;graph.linearTermLimit=linearTerms;emitter.inputPrefix=frame+".";emitter.mutableInputs=true;
        emitter.reuseScratch=reuseScratch;
        emitter.temporary=[&](Format f){return scratch(f,"expr");};
    }
    std::string scratch(Format f,const std::string& purpose) {
        auto key=purpose+"_"+f.c();auto index=scratchCounts[key]++;
        scratchMax[key]=std::max(scratchMax[key],index+1);scratchTypes[key]=f;
        return frame+".tmp_"+key+"_"+std::to_string(index);
    }
    std::size_t symbol(const std::string& key,const std::string& field,Format f) {
        Node n{"input",field,f,0,0,{}};Graph::bounds(n);
        auto id=graph.add(n);graph.runtime.emplace(key,id);return id;
    }
    static std::vector<Ref> flatten(const Ref& value) {
        std::vector<Ref> result;
        std::function<void(const Ref&)> visit=[&](const Ref& v) {
            if(v->name=="Values") for(const auto& child:v->arguments) visit(child);
            else if(v->name=="RecordValue") visit(v->arguments[1]);
            else result.push_back(v);
        };visit(value);return result;
    }
    void declare(const Ref& trace) {
        const auto& a=trace->arguments;const auto& op=trace->name;
        if(op=="Sequence") {for(const auto& child:a) declare(child);return;}
        if(op=="If") {declare(a[1]);declare(a[2]);return;}
        if(op=="Loop") {
            if(!a.at(8)->arguments.empty()) throw Unavailable{"region loop returns require the ordinary projection"};
            ++result.regionLoops;auto id=show(a[0]);std::size_t at=0;
            for(const auto& slot:a[11]->arguments) {
                auto root=show(slot->arguments[0]);auto count=slot->arguments[1]->number.convert_to<unsigned>();
                for(unsigned i=0;i<count;++i)
                    symbol("LoopSlotValue:"+id+":"+root+":"+std::to_string(i),
                        "l"+id+"_s"+root+"_v"+std::to_string(i),format(a[10]->arguments.at(at++)));
                symbol("LoopReady:"+id+":"+root,"q"+id+"_s"+root,{});
            }
            symbol("LoopIndex:"+id,"index"+id,{32,false});
            declare(a[2]);declare(a[5]);return;
        }
        if(op=="ExternalCall") {
            auto found=boundaries.find(a[1]->name);
            if(found==boundaries.end()) throw Unavailable{"missing region boundary signature"};
            found->second.sites.push_back(show(a[0]));++result.regionBoundaries;return;
        }
        if(op!="Done" && op!="Evaluate" && op!="CheckSchema" && op!="CheckIndex")
            throw Unavailable{"region execution requires "+op};
    }
    void need(std::size_t id) {
        if(!live.insert(id).second) return;
        for(auto child:graph.nodes[id].args) need(child);
    }
    Transfer transfer(const Ref& trace,unsigned valueAt,unsigned readyAt) {
        const auto& a=trace->arguments;auto values=flatten(a[valueAt]);std::size_t at=0;
        auto id=show(a[0]);Transfer out;
        for(std::size_t s=0;s<a[11]->arguments.size();++s) {
            const auto& slot=a[11]->arguments[s];auto root=show(slot->arguments[0]);
            auto ready=graph.value(a[readyAt]->arguments.at(s));
            out.push_back({graph.runtime.at("LoopReady:"+id+":"+root),ready});
            auto count=slot->arguments[1]->number.convert_to<unsigned>();
            for(unsigned i=0;i<count;++i) {
                auto dest=graph.runtime.at("LoopSlotValue:"+id+":"+root+":"+std::to_string(i));
                auto rhs=graph.value(values.at(at++));
                const auto predicate=graph.nodes[ready];
                if(predicate.lo==0 && predicate.hi==0) rhs=graph.add({"constant","0",{},0,0,{}});
                else if(predicate.lo<=0 && predicate.hi>=0) {
                    auto f=graph.nodes[dest].type;Node n{"Select","",f,0,0,{ready,rhs,graph.add({"constant","0",{},0,0,{}})}};
                    Graph::bounds(n);rhs=graph.add(n);
                }
                out.push_back({dest,rhs});
            }
        }
        return out;
    }
    void analyze(const Ref& trace) {
        const auto& a=trace->arguments;const auto& op=trace->name;
        if(op=="Sequence") {for(const auto& child:a) analyze(child);return;}
        if(op=="If") {need(graph.value(a[0]));analyze(a[1]);analyze(a[2]);return;}
        if(op=="Loop") {
            need(graph.value(a[3]));need(graph.value(a[7]));need(graph.value(a[9]));
            transfers[trace.get()]={transfer(trace,1,12),transfer(trace,4,13),transfer(trace,6,14)};
            analyze(a[2]);analyze(a[5]);return;
        }
        if(op=="ExternalCall") {for(const auto& value:flatten(a[2])) need(graph.value(value));return;}
        if(op=="Evaluate" || op=="CheckSchema" || op=="CheckIndex") need(graph.value(a[0]));
    }
    void readyInvariants() {
        // Greatest fixed point of definitely-ready loop slots. Start with the
        // proposed invariant "ready", then discard any slot whose entry or
        // backedge cannot preserve it. Entry edges are essential: a cycle of
        // identities alone cannot establish initialization.
        std::set<std::size_t> candidates;
        for(const auto& [key,id]:graph.runtime) if(key.starts_with("LoopReady:")) candidates.insert(id);
        for(;;) {
            std::map<std::size_t,std::optional<Integer>> cache;
            std::function<std::optional<Integer>(std::size_t)> constant=[&](std::size_t id)->std::optional<Integer> {
                if(auto it=cache.find(id);it!=cache.end()) return it->second;
                const auto& n=graph.nodes[id];std::optional<Integer> value;
                if(candidates.contains(id)) value=1;
                else if(n.op=="constant") value=n.lo;
                else if(n.op=="cast") {
                    value=constant(n.args[0]);
                    if(value) {
                        *value&=(Integer(1)<<n.type.bits)-1;
                        if(n.type.sign && *value>=(Integer(1)<<(n.type.bits-1))) *value-=Integer(1)<<n.type.bits;
                    }
                }
                else if(n.op=="Select") {
                    auto c=constant(n.args[0]);
                    if(c) value=constant(n.args[*c!=0 ? 1 : 2]);
                    else {auto yes=constant(n.args[1]),no=constant(n.args[2]);if(yes && no && *yes==*no) value=yes;}
                } else if(n.op=="Not") {auto c=constant(n.args[0]);if(c) value=*c==0 ? 1 : 0;}
                else if(n.op=="And") {auto a=constant(n.args[0]),b=constant(n.args[1]);if(a && b) value=*a!=0 && *b!=0 ? 1 : 0;}
                cache[id]=value;return value;
            };
            std::set<std::size_t> rejected;
            for(const auto& [trace,updates]:transfers) for(const auto& update:updates)
                for(const auto& a:update) if(candidates.contains(a.destination)) {
                    auto value=constant(a.value);if(!value || *value!=1) rejected.insert(a.destination);
                }
            if(rejected.empty()) break;
            for(auto id:rejected) candidates.erase(id);
        }
        for(auto id:candidates) graph.nodes[id]={"constant","1",{},1,1,{}};
        // Nodes are in dependency order. This removes readiness selects and
        // exposes identity transfers before liveness and copy scheduling.
        for(auto& node:graph.nodes) {
            if(node.op=="Select") {
                const auto& condition=graph.nodes[node.args[0]];
                if(condition.op=="constant") {
                    auto selected=node.args[condition.lo!=0 ? 1 : 2];
                    // Keep an edge to the canonical input node. Copying an
                    // input Node here would invent another identity for the
                    // same mutable field and break liveness/cycle detection.
                    node.op="cast";node.data.clear();node.args={selected};
                }
            }
            if(node.op=="cast" && graph.nodes[node.args[0]].op=="constant") {
                Integer value=graph.nodes[node.args[0]].lo & ((Integer(1)<<node.type.bits)-1);
                if(node.type.sign && value>=(Integer(1)<<(node.type.bits-1))) value-=Integer(1)<<node.type.bits;
                node.op="constant";node.data=value.str();node.lo=node.hi=value;node.args.clear();
            }
        }
        // Folding changed node keys. Rebuild interning before constructing
        // path-specialized expressions; old keys must not name different nodes.
        graph.reindex();
        live.clear();for(auto id:roots) need(id);
        analyze(execution);
    }
    void block(const std::vector<std::size_t>& values,const std::vector<Format>& types,
               const std::vector<std::string>& destinations,const std::string& pad,
               const Emitter::Locals& replacements={}) {
        // Scratch lives only until this block finishes. Branch recursion within
        // block() retains separate slots, so its inherited temporaries survive.
        for(auto& [key,count]:scratchCounts) if(key.starts_with("expr_")) count=0;
        emitter.destinations=destinations;
        if(transferFunctions && values.size()>=3) {
            // Give SDCC bounded allocation regions. A large loop with many
            // volatile frame fields otherwise makes its allocation search
            // disproportionately expensive. This is a physical call boundary;
            // the same region frame owns all live state across the call.
            std::ostringstream code;
            struct Redirect {
                std::ostream& stream;std::streambuf* previous;
                Redirect(std::ostream& s,std::streambuf* next):stream(s),previous(s.rdbuf(next)) {}
                ~Redirect() {stream.rdbuf(previous);}
            };
            {Redirect redirect(body,code.rdbuf());emitter.block(values,types,replacements,"    ");}
            auto function="vx_bind_"+name+"_transfer_"+std::to_string(helperCount++);
            helpers<<"static void "<<function<<"(VX_BIND_"<<name<<"_TRANSFER_PARAMETERS) {\n"<<code.str()<<"}\n";
            body<<pad<<function<<"(VX_BIND_"<<name<<"_TRANSFER_ARGUMENTS);\n";
        } else emitter.block(values,types,replacements,pad);
    }
    bool depends(std::size_t root,std::size_t variable,const Emitter::Locals& replacements) {
        std::set<std::size_t> visited;
        std::function<bool(std::size_t)> visit=[&](std::size_t id) {
            if(replacements.contains(id) || !visited.insert(id).second) return false;
            if(id==variable) return true;
            for(auto child:graph.nodes[id].args) if(visit(child)) return true;
            return false;
        };return visit(root);
    }
    void assign(Transfer pending,const std::string& pad) {
        std::erase_if(pending,[&](const Assignment& a){
            auto sourceValue=a.value;
            while(graph.nodes[sourceValue].op=="cast" && graph.nodes[sourceValue].type==graph.nodes[a.destination].type)
                sourceValue=graph.nodes[sourceValue].args[0];
            return graph.nodes[a.destination].op!="input" || !live.contains(a.destination) || a.destination==sourceValue;
        });
        Emitter::Locals replacements;
        for(auto& [key,count]:scratchCounts) if(!key.starts_with("expr_")) count=0;
        if(!scheduled) {
            std::vector<std::size_t> values;std::vector<Format> types;std::vector<std::string> names;
            for(const auto& a:pending) {
                auto f=graph.nodes[a.destination].type;values.push_back(a.value);types.push_back(f);names.push_back(scratch(f,"copy"));
            }
            block(values,types,names,pad);
            for(std::size_t i=0;i<pending.size();++i)
                body<<pad<<frame<<'.'<<graph.nodes[pending[i].destination].data<<" = "<<names[i]<<";\n";
            return;
        }
        while(!pending.empty()) {
            std::vector<std::size_t> safe;
            for(std::size_t i=0;i<pending.size();++i) {
                bool used=false;
                for(std::size_t j=0;j<pending.size();++j) if(i!=j && depends(pending[j].value,pending[i].destination,replacements)) {used=true;break;}
                if(!used) safe.push_back(i);
            }
            if(safe.empty()) {
                // Break a parallel-copy cycle by saving the OLD destination.
                // Do not save its RHS: another unscheduled RHS may still need it.
                auto id=pending.front().destination;auto f=graph.nodes[id].type;
                auto temp=scratch(f,"cycle");emitter.store(id,f,temp,{},pad);replacements[id]=temp;
                continue;
            }
            std::vector<std::size_t> values;std::vector<Format> types;std::vector<std::string> names;
            for(auto i:safe) {
                const auto& a=pending[i];values.push_back(a.value);types.push_back(graph.nodes[a.destination].type);
                names.push_back(frame+"."+graph.nodes[a.destination].data);
            }
            block(values,types,names,pad,replacements);
            for(auto it=safe.rbegin();it!=safe.rend();++it) pending.erase(pending.begin()+static_cast<std::ptrdiff_t>(*it));
        }
    }
    bool zero(const Ref& value) {const auto& n=graph.nodes[graph.value(value)];return n.lo==0 && n.hi==0;}
#include "source_region_flow.inc"
    ValueBindingProjection project(const std::vector<Ref>& inputs,const std::vector<Ref>& types,
        const std::vector<Ref>& values,const std::vector<Ref>& paths,const std::vector<RegionBoundary>& signatures) {
        for(const auto& b:signatures) boundaries.emplace(b.name,Boundary{b,{}});
        declare(execution);
        std::erase_if(boundaries,[](const auto& item){return item.second.sites.empty();});
        for(const auto& [symbolName,b]:boundaries) for(const auto& site:b.sites)
            for(std::size_t i=0;i<b.signature.outputs.size();++i)
                symbol("CallValue:"+site+":"+std::to_string(i),b.sites.size()==1 ? "boundary_"+symbolName+".r"+std::to_string(i) : "call"+site+"_v"+std::to_string(i),format(b.signature.outputs[i]));
        for(std::size_t i=0;i<inputs.size();++i) {
            const auto& a=inputs[i]->arguments;auto f=format(a[2]);
            graph.inputs[a[0]->name]=graph.add({"input",a[0]->name,f,a[3]->number,a[4]->number,{}});
            fields[a[0]->name]=f;result.inputs.push_back(i);
            Node n;n.type=f;Graph::bounds(n);
            if(a[3]->number>n.lo) body<<"    if ("<<frame<<'.'<<a[0]->name<<" < "<<a[3]->number<<"L) vx_trap(16);\n";
            if(a[4]->number<n.hi) body<<"    if ("<<frame<<'.'<<a[0]->name<<" > "<<a[4]->number<<(f.sign ? "L" : "UL")<<") vx_trap(16);\n";
        }
        for(std::size_t i=0;i<values.size();++i) {roots.push_back(graph.value(values[i]));formats.push_back(format(types[i]));need(roots.back());}
        analyze(execution);
        readyInvariants();
        // Fixed point over loop-carried values. Dead slots do not make their
        // own identity transfers live, unlike a scan of all transition RHSs.
        for(;;) {
            auto before=live.size();
            for(const auto& [trace,updates]:transfers) for(const auto& update:updates)
                for(const auto& a:update) if(live.contains(a.destination)) need(a.value);
            if(before==live.size()) break;
        }
        generateFlow();
        result.bytes=result.regionBoundaryBytes;
        std::ostringstream declarations,common,fixedBoundary,autoBoundary;
        for(const auto& [symbolName,b]:boundaries) {
            auto type="vx_bind_"+name+"_boundary_"+symbolName+"_type";
            declarations<<"typedef struct {\n";
            for(std::size_t i=0;i<b.signature.inputs.size();++i) {
                auto f=format(b.signature.inputs[i]);declarations<<"    VX_STABLE "<<f.c()<<" a"<<i<<";\n";
            }
            for(std::size_t i=0;i<b.signature.outputs.size();++i) {
                auto f=format(b.signature.outputs[i]);declarations<<"    VX_STABLE "<<f.c()<<" r"<<i<<";\n";
            }
            if(b.signature.inputs.empty() && b.signature.outputs.empty()) declarations<<"    uint8_t unused;\n";
            declarations<<"} "<<type<<";\n";
            auto function="vx_bind_"+name+"_boundary_"+symbolName,macro="VX_BIND_"+name;
            const auto& location=boundaryStorage.at(symbolName);
            // Both alternatives publish through exactly the same typed fields.
            // The expanded statement executes at the original call position;
            // no read moves across an effect and no lifetime promise changes.
            const auto& expansion=b.signature.expansion;
            // SDCC can forward an expanded aggregate result through a clobbered
            // register even though its publication fields are volatile. Keep a
            // real call for that target; host expansion and scalar queries are
            // unaffected. This is a correctness boundary, not a cost heuristic.
            const bool isolate=!expansion.empty() && b.signature.outputs.size()>1;
            if(!expansion.empty()) {
                result.regionExpansions.emplace(symbolName,expansion);
                if(isolate) result.regionExpansionBarriers.push_back(symbolName);
                auto check="#ifndef "+expansion+"\n#error \"native boundary expansion "+expansion+" must be defined before this binding\"\n#endif\n";
                fixedBoundary<<check;autoBoundary<<check;
            }
            fixedBoundary<<"#define "<<macro<<"_EXPANDED_"<<symbolName<<' '<<(!expansion.empty() ? 1 : 0)<<"\n";
            autoBoundary<<"#define "<<macro<<"_EXPANDED_"<<symbolName<<' '<<(!expansion.empty() ? 1 : 0)<<"\n";
            fixedBoundary<<"#define "<<macro<<"_BOUNDARY_"<<symbolName<<" static void "<<function<<"(void)\n"
                <<"#define "<<macro<<"_DATA_"<<symbolName<<" ("<<frame<<'.'<<location<<")\n"
                <<"#define "<<macro<<"_CALL_"<<symbolName<<"() ";
            if(expansion.empty()) fixedBoundary<<function<<"()\n"<<macro<<"_BOUNDARY_"<<symbolName<<";\n";
            else fixedBoundary<<expansion<<"(("<<frame<<'.'<<location<<"))\n";
            autoBoundary<<"#define "<<macro<<"_BOUNDARY_"<<symbolName<<" static void "<<function<<"("<<type<<" *vxb)\n"
                <<"#define "<<macro<<"_DATA_"<<symbolName<<" (*vxb)\n"
                <<"#define "<<macro<<"_CALL_"<<symbolName<<"() ";
            if(expansion.empty()) autoBoundary<<function<<"(&vxf->"<<location<<")\n"<<macro<<"_BOUNDARY_"<<symbolName<<";\n";
            else autoBoundary<<expansion<<"((vxf->"<<location<<"))\n";
            if(isolate) {
                for(auto* stream:{&fixedBoundary,&autoBoundary})
                    *stream<<"#ifdef __SDCC_z80\n#undef "<<macro<<"_CALL_"<<symbolName<<"\n"
                        <<macro<<"_BOUNDARY_"<<symbolName<<" { "<<expansion<<"("<<macro<<"_DATA_"<<symbolName<<"); }\n"
                        <<"#define "<<macro<<"_CALL_"<<symbolName<<"() "<<function
                        <<(stream==&fixedBoundary ? "()" : "(&vxf->"+location+")")<<"\n#endif\n";
            }
        }
        declarations<<"#ifndef VX_VALUE_"<<source<<"_RESULT\n#define VX_VALUE_"<<source<<"_RESULT\ntypedef struct {\n";
        for(std::size_t i=0;i<formats.size();++i) {
            declarations<<"    "<<formats[i].c()<<" v"<<i<<"; /* "<<paths[i]->name<<" */\n";result.bytes+=formats[i].bits/8;
        }
        declarations<<"} vx_"<<source<<"_result;\n#endif\ntypedef struct {\n";
        for(const auto& [field,f]:fields) {declarations<<"    VX_STABLE "<<f.c()<<' '<<field<<";\n";result.bytes+=f.bits/8;}
        result.regionStateBytes=result.bytes;
        for(const auto& [key,count]:scratchMax) for(unsigned i=0;i<count;++i) {
            auto f=scratchTypes.at(key);declarations<<"    VX_STABLE "<<f.c()<<" tmp_"<<key<<'_'<<i<<";\n";
            result.bytes+=f.bits/8;result.regionScratchBytes+=f.bits/8;
        }
        for(std::size_t i=0;i<result.regionBoundarySlots.size();++i) {
            const auto& slot=result.regionBoundarySlots[i];bool shared=slot.members.size()>1;
            if(shared) declarations<<"    union {\n";
            for(const auto& symbolName:slot.members)
                declarations<<(shared ? "        " : "    ")<<"vx_bind_"<<name<<"_boundary_"<<symbolName<<"_type boundary_"<<symbolName<<";\n";
            if(shared) declarations<<"    } boundary_slot_"<<i<<";\n";
        }
        declarations<<"    vx_"<<source<<"_result result;\n} "<<frame<<"_type;\n"
            <<"#ifdef __SDCC_z80\ntypedef char vx_bind_"<<name<<"_size[sizeof("<<frame<<"_type)=="<<result.bytes<<" ? 1 : -1];\n#endif\n";
        common<<"/* Execution-region binding. The lifetime covers input publication, all\n"
            <<" * synchronous boundary calls, and result consumption. A boundary may\n"
            <<" * invoke other bindings with disjoint storage, never this exclusive\n"
            <<" * occurrence or a binding whose workspace overlaps it. */\n"
            <<"#ifndef VX_BIND_"<<name<<"_H\n#define VX_BIND_"<<name<<"_H\n"
            <<"#include \"vx_binding_types.h\"\n"<<runtimeIncludes(emitter.runtimeHelpers);
        auto automaticBody=helpers.str()+"static void vx_bind_"+name+"_compute("+frame+"_type *vxf) {\n"+body.str()+"}\n";
        const auto prefix=frame+".";
        for(std::size_t at=0;(at=automaticBody.find(prefix,at))!=std::string::npos;at+=5) automaticBody.replace(at,prefix.size(),"vxf->");
        result.types=declarations.str();result.headerPrefix=common.str();
        result.fixedFunction=fixedBoundary.str()+"#define VX_BIND_"+name+"_TRANSFER_PARAMETERS void\n"
            +"#define VX_BIND_"+name+"_PREDICATE_PARAMETERS void\n"
            +"#define VX_BIND_"+name+"_TRANSFER_ARGUMENTS /* fixed region */\n"
            +helpers.str()+"static void vx_bind_"+name+"_run(void) {\n"+body.str()+"}\n";
        result.automaticHeader=common.str()+autoBoundary.str()+"#define VX_BIND_"+name+"_LOCAL "+frame+"_type "+frame+"\n"
            +"#define VX_BIND_"+name+"_TRANSFER_PARAMETERS "+frame+"_type *vxf\n"
            +"#define VX_BIND_"+name+"_PREDICATE_PARAMETERS "+frame+"_type * VX_STABLE vxf\n"
            +"#define VX_BIND_"+name+"_TRANSFER_ARGUMENTS vxf\n"+automaticBody
            +"#define vx_bind_"+name+"_run() vx_bind_"+name+"_compute(&"+frame+")\n#endif\n";
        result.wideSelectBranches=emitter.wideSelectBranches;result.wideByteCopies=emitter.wideByteCopies;
        result.reuseScratch=emitter.reuseScratch;result.scratchReuses=emitter.scratchReuses;
        result.narrowedValues=emitter.narrowedValues;
        result.linearCombinations=graph.linearCombinations;result.linearTerms=graph.linearTermLimit;
        return result;
    }
};
}

ValueBindingProjection projectRegionBinding(const std::string& name,const std::string& source,
    const std::vector<Ref>& inputs,const std::vector<Ref>& types,const std::vector<Ref>& values,
    const std::vector<Ref>& paths,const Ref& execution,const std::vector<RegionBoundary>& boundaries,bool scheduleCopies,bool transferFunctions,unsigned boundaryOverlay,bool predicateFunctions,bool reuseScratch,unsigned linearTerms) {
    try {return RegionEmitter(name,source,execution,scheduleCopies,transferFunctions,boundaryOverlay,predicateFunctions,reuseScratch,linearTerms).project(inputs,types,values,paths,boundaries);}
    catch(const Unavailable& failure) {throw std::invalid_argument("C region binding "+name+": "+failure.reason);}
}
