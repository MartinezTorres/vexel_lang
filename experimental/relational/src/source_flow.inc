// Execution is another projection of source relationships. State identities
// name immutable graphs of reaching values, never mutable host-language values.
bool Session::needsFlow(Id id) {
    std::set<Id> seen;
    std::function<bool(Id)> visit=[&](Id at) {
        if(at==none || !seen.insert(at).second || cells[at].flowPure) return false;
        expand(at);const auto& c=cells[at];
        if(c.site!=none && (graph.nodes[c.site].syntax==Syntax::Probe || graph.nodes[c.site].syntax==Syntax::MethodProbe)) return false;
        auto op=operatorName(at);if(!op.empty() && !functions(c.scope,"."+op).empty()) return true;
        if(!broadcastName(at).empty()) return true;
        // A result-free block still has normal completion. Its execution
        // projection produces NoValue even when there is no effect to visit.
        if(c.site!=none && graph.nodes[c.site].syntax==Syntax::Block && c.target==none) return true;
        if(c.site!=none && graph.nodes[c.site].syntax==Syntax::Binary && (graph.nodes[c.site].text=="&&" || graph.nodes[c.site].text=="||")) return true;
        if(c.kind==Cell::Projection || c.kind==Cell::Receiver) return true;
        if(c.kind==Cell::Math && (c.inputName=="isqrt" || c.inputName=="fmod")) return true;
        if(global(at) && (graph.nodes[c.site].external || c.target==none)) return true;
        if(c.site!=none && graph.nodes[c.site].syntax==Syntax::Binary && (graph.nodes[c.site].text=="/" || graph.nodes[c.site].text=="%" || graph.nodes[c.site].text=="<<" || graph.nodes[c.site].text==">>")) return true;
        if(c.kind==Cell::Thunk || c.kind==Cell::Store || c.kind==Cell::Invocation || (c.site!=none && ((graph.nodes[c.site].syntax==Syntax::Assign || graph.nodes[c.site].syntax==Syntax::Destructure) || graph.nodes[c.site].syntax==Syntax::Index || graph.nodes[c.site].syntax==Syntax::Cast || graph.nodes[c.site].syntax==Syntax::Return || graph.nodes[c.site].syntax==Syntax::Require || graph.nodes[c.site].syntax==Syntax::Iterate || graph.nodes[c.site].syntax==Syntax::Repeat || graph.nodes[c.site].syntax==Syntax::Loop))) return true;
        if(visit(c.target)) return true;
        for(Id child:c.prefix) if(visit(child)) return true;
        for(Id child:c.items) if(visit(child)) return true;
        if(c.site!=none && graph.nodes[c.site].syntax==Syntax::Call) for(auto def:functions(c.scope,graph.nodes[c.site].text))
            for(Id param:graph.nodes[def.site].children) if(graph.nodes[param].expression) return true;
        if(c.kind==Cell::Dispatch) {
            for(auto def:functions(c.scope,graph.nodes[c.site].text)) {
                const auto& fn=graph.nodes[def.site];
                if(fn.noinline || fn.external) return true;
                std::function<bool(Id)> writes=[&](Id site) {
                    const auto& n=graph.nodes[site];
                    if(n.syntax==Syntax::Assign || n.syntax==Syntax::Destructure) return true;
                    for(Id child:n.children) if(writes(child)) return true;
                    return n.body!=none && writes(n.body);
                };
                if(writes(fn.body)) return true;
            }
        }
        return false;
    };
    return visit(id);
}
Ref Session::observedType(Id id,bool implementing,bool* execution) {
    if(execution) *execution=false;
    expand(id);if(cells[id].kind==Cell::Projection) return execution ? Ref{} : demand(id,Facet::Type);
    if(!implementing && cells[id].flowWrapper && graph.nodes[cells[id].definition.site].annotation==none) return demand(id,Facet::Type);
    auto ordinary=[&]{return execution ? Ref{} : demand(cells[id].flowWrapper ? cells[id].target : id,Facet::Type);};
    if(!needsFlow(id)) return ordinary();
    // A structural record projection can close a property without accessing
    // the runtime values needed to construct an instance of the whole schema.
    if(cells[peel(id)].kind==Cell::Record && !changesProjectionState(id)) return ordinary();
    // Overload selection is itself a type relation. Pure argument completion
    // (for example a checked cast) does not require executing a call just to
    // observe its interface. State-changing calls retain execution below.
    if(cells[id].kind==Cell::Dispatch && !changesProjectionState(id)) return ordinary();
    std::set<Id> seen;
    std::function<bool(Id)> dependsOnExecution=[&](Id at) {
        if(at==none || !seen.insert(at).second) return false;
        expand(at);const auto& c=cells[at];
        if(c.kind==Cell::Known || c.kind==Cell::Input || c.kind==Cell::Receiver) return false;
        if(c.kind==Cell::Bound) {
            if(c.annotation==none) return dependsOnExecution(c.target);
            std::function<bool(Id)> annotationNeedsState=[&](Id site) {
                const auto& spec=graph.nodes[site];
                if(spec.body!=none && dependsOnExecution(expression(spec.body,c.scope))) return true;
                for(Id child:spec.children) {
                    if(spec.text=="(tuple)" ? annotationNeedsState(child) : dependsOnExecution(expression(child,c.scope))) return true;
                }
                return false;
            };
            return annotationNeedsState(c.annotation);
        }
        if(c.kind==Cell::Projection) {
            auto selected=c.names;Id owner=peel(c.target);
            while(cells[owner].kind==Cell::Projection) {
                auto joined=cells[owner].names;joined.insert(joined.end(),selected.begin(),selected.end());selected=std::move(joined);
                owner=peel(cells[owner].target);
            }
            const auto& source=cells[owner];
            // Literal aggregates expose independent fields. An unrelated
            // missing call in a sibling is not evidence that this field's
            // interface needs execution of the whole containing value.
            if(source.kind==Cell::Record || (source.kind==Cell::SyntaxCell && graph.nodes[source.site].syntax==Syntax::Tuple)) {
                for(std::size_t i=0;i<source.items.size();++i) {
                    auto name=source.kind==Cell::Record ? source.names[i] : "__"+std::to_string(i);
                    if(selected[0]==name) return dependsOnExecution(projection(source.items[i],{selected.begin()+1,selected.end()}));
                }
                return false;
            }
            return changesProjectionState(c.target) || dependsOnExecution(c.target);
        }
        if(c.kind==Cell::Store) return dependsOnExecution(c.location);
        if(c.flowWrapper && graph.nodes[c.definition.site].annotation==none) {
            if(!implementing && isolatedFunctionInterface(at)) return false;
            // Completion of an argument may require execution even when the
            // callee's result type depends only on that argument's interface.
            // Demand execution for the callee's own transfers, then inspect
            // its result properties without evaluating all arguments.
            std::set<Id> calls;
            if(!effectSignature(graph.nodes[c.definition.site].body,c.scope,calls).second) return true;
            return dependsOnExecution(c.target);
        }
        if(c.kind==Cell::Alias) return dependsOnExecution(c.target);
        if(c.kind==Cell::Invocation) return graph.nodes[c.definition.site].annotation==none && (implementing || !isolatedFunctionInterface(at));
        if(c.kind==Cell::Dispatch) return changesProjectionState(at);
        if(c.kind==Cell::Record) return true;
        const auto& n=graph.nodes[c.site];
        if(n.syntax==Syntax::Select && c.items.size()==3) {
            Id condition=peel(c.items[0]);const auto& test=cells[condition];
            if(test.site!=none && (graph.nodes[test.site].syntax==Syntax::Probe || graph.nodes[test.site].syntax==Syntax::MethodProbe))
                return dependsOnExecution(c.items[1]) || dependsOnExecution(c.items[2]);
        }
        if(n.syntax==Syntax::Select || (n.syntax==Syntax::Assign || n.syntax==Syntax::Destructure) || n.syntax==Syntax::Iterate || n.syntax==Syntax::Repeat || n.syntax==Syntax::Loop || (n.syntax==Syntax::Binary && (n.text=="&&" || n.text=="||"))) return true;
        if(n.syntax==Syntax::Cast || (n.syntax==Syntax::Unary && n.text=="|")) return false;
        for(Id child:c.items) if(dependsOnExecution(child)) return true;
        return false;
    };
    if(!dependsOnExecution(id)) return ordinary();
    if(execution) {*execution=true;return {};}
    return demand(flow(id,queryState(cells[id].scope)).value,Facet::Type);
}
Id Session::known(Ref type,Ref value,Id site) {
    Cell c;c.kind=Cell::Known;c.expanded=true;c.flowPure=true;c.site=site;
    c.knownType=std::move(type);c.knownValue=std::move(value);return addCell(std::move(c));
}
Id Session::state(FlowState value) {states.push_back(std::move(value));return states.size()-1;}
Id Session::location(Id id) const {return cells[id].kind==Cell::Store ? cells[id].location : id;}
Ref Session::locationType(Id slot,Id at) {
    if(auto value=states[at].values.find(slot);value!=states[at].values.end()) return demand(value->second,Facet::Type);
    if(auto type=states[at].locationTypes.find(slot);type!=states[at].locationTypes.end()) {captureUse(slot,at);return type->second;}
    return demand(slot,Facet::Type);
}
std::vector<Id> Session::captures(Id scope,Id declaration) const {
    std::set<Id> slots;std::set<std::string> names;
    for(Id env=scope;env!=none;env=environments[env].parent)
        for(const auto& [name,value]:environments[env].values) if(names.insert(name).second) {
            Id slot=captureLocation(value);
            if(cells[slot].kind==Cell::Bound && graph.nodes[cells[slot].site].syntax!=Syntax::Global) slots.insert(slot);
        }
    if(declaration==none) return {slots.begin(),slots.end()};
    std::set<Id> used;std::set<std::pair<Id,Id>> scanned;
    std::function<void(Id,Id,std::set<std::string>)> visit;
    std::function<void(Definition)> function=[&](Definition def){
        if(!scanned.emplace(def.site,def.scope).second) return;
        std::set<std::string> boundNames;
        const auto& fn=graph.nodes[def.site];
        for(Id parameter:fn.children) boundNames.insert(graph.nodes[parameter].text);
        visit(fn.body,def.scope,boundNames);
        if(fn.annotation!=none) visit(fn.annotation,def.scope,boundNames);
    };
    visit=[&](Id site,Id lexical,std::set<std::string> boundNames){
        if(site==none) return;
        const auto& n=graph.nodes[site];
        if(n.syntax==Syntax::Name && !boundNames.contains(n.text)) {
            Id value=lookup(lexical,n.text);if(value!=none && slots.contains(captureLocation(value))) used.insert(captureLocation(value));
        }
        if(n.syntax==Syntax::Block) {
            for(Id child:n.children) {
                const auto& statement=graph.nodes[child];
                if(statement.syntax==Syntax::Declare) boundNames.insert(statement.text);
                else if(statement.syntax==Syntax::Assign && graph.nodes[statement.children[0]].syntax==Syntax::Name) {
                    const auto& name=graph.nodes[statement.children[0]].text;
                    if(lookup(lexical,name)==none) boundNames.insert(name);
                }
                else if(statement.syntax==Syntax::Destructure) for(std::size_t i=0;i+1<statement.children.size();++i) {
                    const auto& name=graph.nodes[statement.children[i]].text;
                    if(lookup(lexical,name)==none) boundNames.insert(name);
                }
            }
        }
        for(auto def:callCandidates(site,lexical)) function(def);
        if(n.syntax==Syntax::Construct) {
            auto def=record(lexical,n.text);
            if(def.site!=none) {
                auto fields=boundNames;for(Id f:graph.nodes[def.site].children) fields.insert(graph.nodes[f].text);
                for(Id f:graph.nodes[def.site].children) if(graph.nodes[f].annotation!=none) visit(graph.nodes[f].annotation,def.scope,fields);
            }
        }
        if(n.syntax==Syntax::Function) for(Id param:n.children) boundNames.insert(graph.nodes[param].text);
        for(Id child:n.children) visit(child,lexical,boundNames);
        if(n.body!=none) visit(n.body,lexical,boundNames);
        if(n.annotation!=none) visit(n.annotation,lexical,boundNames);
    };
    function({declaration,scope});return {used.begin(),used.end()};
}
Ref Session::sequence(Ref a,Ref b) {
    auto result=fresh("execution sequence");
    later({a,b},[this,result](const auto& args){
        equation(result,args[0]->name=="Done" ? args[1] : args[1]->name=="Done" ? args[0] : terms().make("Sequence",args));
    });
    return result;
}
Ref Session::after(const Ref& type,const Ref& value,const Ref& code) {
    if(type->name=="NoValue") return value;
    if(code->name=="Done") return value;
    if(aggregate(type)) {
        auto flat=leaves(type,value);std::size_t at=0;
        return structure(type,[&](const Ref& leafType){return after(leafType,flat[at++].value,code);});
    }
    if(scalarFormat(type).has_value()) {
        Ref lo,hi;
        if(value->kind==Term::Number) lo=hi=value;
        else if(value->name=="Runtime") {lo=value->arguments[1];hi=value->arguments[2];}
        else {pending("execution value has no scalar range");return fresh("execution value");}
        return terms().make("Runtime",{terms().make("After",{code,value}),lo,hi});
    }
    pending("execution result requires a supported scalar/array interface");return fresh("execution result");
}
Flow Session::flow(Id id,Id before) {
    auto key=std::make_pair(id,before);
    if(auto found=flows.find(key);found!=flows.end()) {
        if(effectInputs) effectInputs->push_back(found->second.effect);
        if(runtimeInputs) runtimeInputs->push_back(found->second.runtime);
        request.goals.push_back(require("flow_"+std::to_string(found->second.value)));return found->second;
    }
    Flow out{known(fresh("execution type"),fresh("execution value"),cells[id].site),fresh("outgoing state"),fresh("completion"),fresh("boundary effect"),fresh("runtime requirement")};
    if(effectInputs) effectInputs->push_back(out.effect);
    if(runtimeInputs) runtimeInputs->push_back(out.runtime);
    flows.emplace(key,out);auto name="flow_"+std::to_string(out.value);
    auto body=constructed([&]{
        std::vector<Ref> effects;SupportScope collecting(effectInputs,&effects);
        std::vector<Ref> runtimes;SupportScope collectingRuntime(runtimeInputs,&runtimes);
        guardedFlow(id,before,out);closeEffects(effects,out.effect);closeEffects(runtimes,out.runtime);
    });program.bundles.emplace(name,std::move(body));
    request.goals.push_back(require(name));return out;
}
void Session::flowItems(const std::vector<Id>& items,Id before,const std::function<void(std::vector<Id>,Id,Ref)>& finish) {
    if(items.empty()) {finish({},before,terms().make("Done"));return;}
    auto first=flow(items[0],before);std::vector<Id> rest(items.begin()+1,items.end());
    later({first.state},[this,first,rest,finish](const auto& args){
        flowItems(rest,args[0]->number.template convert_to<Id>(),[this,first,finish](auto values,Id end,Ref code){
            values.insert(values.begin(),first.value);finish(values,end,sequence(first.code,code));
        });
    });
}
void Session::flowSequence(const std::vector<Id>& list,Id target,Id before,const Flow& out) {
    flowItems(list,before,[this,list,target,out](const auto& values,Id end,Ref code){
        auto finish=[this,out,code](Id value,Ref post,Ref tail) {
            equation(cells[out.value].knownType,demand(value,Facet::Type));
            equation(cells[out.value].knownValue,demand(value,Facet::Value));
            equation(out.state,post);equation(out.code,sequence(code,tail));
        };
        if(target==none) {finish(known(terms().make("NoValue"),terms().make("NoValue")),number(end),terms().make("Done"));return;}
        auto found=std::find(list.begin(),list.end(),target);
        if(found!=list.end()) finish(values[static_cast<std::size_t>(found-list.begin())],number(end),terms().make("Done"));
        else {auto value=flow(target,end);finish(value.value,value.state,value.code);}
    });
}
void Session::flowBody(Id id,Id before,const Flow& out) {
    expand(id);const auto& c=cells[id];
    if(c.kind==Cell::Missing && c.absence==Diagnostic::ImportResolution) {
        throw Error(graph.nodes[c.absenceSite].span,c.problem,c.absence);
        return;
    }
    auto done=[&](Id value,Id end,Ref code) {
        equation(cells[out.value].knownType,demand(value,Facet::Type));
        equation(cells[out.value].knownValue,conditionedValue(demand(value,Facet::Type),demand(value,Facet::Value),states[end].conditions,!states[end].controlShadow));
        equation(out.state,number(end));equation(out.code,code);
    };
    if(c.flowPure || c.kind==Cell::Input || c.kind==Cell::LoopSlot) {done(id,before,terms().make("Done"));return;}
    if(c.kind==Cell::Receiver) {flowReceiver(id,before,out);return;}
    if(c.kind==Cell::Projection) {flowProjection(id,before,out);return;}
    if(global(id)) {flowGlobal(id,before,out);return;}
    if(c.kind==Cell::Bound || c.kind==Cell::Store) {
        Id slot=location(id);
        if(c.kind==Cell::Store && cells[slot].kind==Cell::Receiver) {flowReceiverStore(id,before,out);return;}
        if(c.kind==Cell::Store && (cells[slot].readonly || states[before].immutableRoots.contains(slot))) {conflict();return;}
        if(c.kind==Cell::Bound) if(Id value=reachingValue(slot,before);value!=none) {requireInitialized(slot,before);done(value,before,terms().make("Done"));return;}
        if(c.kind==Cell::Bound && states[before].locationTypes.contains(slot)) {pending("uninitialized execution location",c.site);return;}
        if(c.kind==Cell::Store && global(slot)) {
            auto value=flow(c.target,before);
            later({value.state},[this,slot,value,out](const auto& args){
                continueFlow(args[0]->number.template convert_to<Id>(),value.code,out,[this,slot,value](Id at,const Flow& result){flowGlobalStore(slot,value.value,at,result,true);});
            });return;
        }
        if(c.kind==Cell::Store && states[before].frame!=none) for(const auto& [key,frame]:frames) {
            (void)key;
            if(frame.id==states[before].frame && slot<frame.firstAutomatic && std::find(frame.captures.begin(),frame.captures.end(),slot)==frame.captures.end()) {
                pending("runtime captured writes require an initialized scalar interface",c.site);return;
            }
        }
        if(c.target==none) {pending("uninitialized execution location",c.site);return;}
        auto value=flow(c.target,before);
        later({value.state},[this,id,slot,value,out](const auto& args){
            continueFlow(args[0]->number.template convert_to<Id>(),value.code,out,[this,id,slot,value](Id at,const Flow& result){
                auto store=[this,id,slot,value,at,result]{
                const auto& binding=cells[id];Ref type,converted=fresh("stored value");
                if(binding.kind==Cell::Bound && binding.annotation==none) type=demand(value.value,Facet::Type);
                else if(cells[slot].annotation==none && states[at].values.contains(slot)) type=demand(states[at].values.at(slot),Facet::Type);
                else if(binding.kind==Cell::Bound && binding.annotation!=none) {
                    Id specialized=bound(binding.site,frozenScope(binding.scope,at),binding.annotation,value.value);
                    type=demand(specialized,Facet::Type);
                } else type=locationType(slot,at);
                conversion(value.value,type,demand(value.value,Facet::Value),converted);
                Id stored=known(type,converted,binding.site);auto next=states[at];next.values[slot]=stored;next.initialized[slot]=number(1);
                captureUse(slot,at,true);
                equation(result.state,number(state(std::move(next))));equation(result.code,validateSchema(type,converted));
                bool statement=binding.kind==Cell::Store;
                equation(cells[result.value].knownType,statement ? terms().make("NoValue") : type);
                equation(cells[result.value].knownValue,statement ? terms().make("NoValue") : converted);
                };
                store();
            });
        });return;
    }
    const auto& n=graph.nodes[c.site];
    if(n.syntax==Syntax::Probe || n.syntax==Syntax::MethodProbe) {done(id,before,terms().make("Done"));return;}
    if(n.syntax==Syntax::Solve) {
        auto body=flow(c.target,before);equation(body.effect,number(0));equation(body.runtime,number(0));
        equation(cells[out.value].knownType,demand(body.value,Facet::Type));equation(cells[out.value].knownValue,demand(body.value,Facet::Value));
        equation(out.state,body.state);equation(out.code,body.code);return;
    }
    if(c.flowWrapper) {
        Flow body{known(fresh("function body type"),fresh("function body value"),c.site),fresh("function body state"),fresh("function body completion")};
        auto entry=states[before];entry.exits.clear();entry.live=number(1);entry.returnTarget=id;entry.loopTargets.clear();Id start=state(std::move(entry));
        Id target=c.target;
        if(graph.nodes[c.definition.site].annotation!=none && cells[target].kind==Cell::Bound) target=cells[target].target;
        flowSequence(c.prefix,target,start,body);
        finishFunction(body,before,c.scope,graph.nodes[c.definition.site].annotation,id,out);return;
    }
    if(c.kind==Cell::Thunk) {flowThunk(id,before,out);return;}
    if(n.syntax==Syntax::Require) {
        if(!environments[c.scope].puzzle) {conflict();return;}
        auto predicate=flow(c.items[0],before);auto boolean=fresh("finite requirement");
        request.goals.push_back(call("implicit",{demand(predicate.value,Facet::Type),terms().make("Bool"),demand(predicate.value,Facet::Value),boolean}));
        equation(boolean,number(1));
        equation(out.state,predicate.state);equation(out.code,predicate.code);
        equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));return;
    }
    if(c.syntheticCall || (n.syntax==Syntax::Call && (c.kind==Cell::Alias || c.kind==Cell::Dispatch || c.kind==Cell::Missing)) || c.kind==Cell::Invocation) {flowCall(id,before,out);return;}
    if(Id call=operatorCall(id);call!=none) {flowOperator(id,call,before,out);return;}
    if(auto op=broadcastName(id);!op.empty()) {flowBroadcast(id,op,before,out);return;}
    if(c.kind==Cell::SyntaxCell && n.syntax==Syntax::Return) {flowExit(id,before,out);return;}
    if(c.kind==Cell::SyntaxCell && n.syntax==Syntax::Binary && (n.text=="&&" || n.text=="||")) {flowLogical(id,before,out);return;}
    if(c.kind==Cell::SyntaxCell && (n.syntax==Syntax::Iterate || n.syntax==Syntax::Repeat || n.syntax==Syntax::Loop)) {flowIteration(id,before,out);return;}
    if(n.syntax==Syntax::Name && c.kind==Cell::Alias) {
        Id slot=location(c.target);
        if(cells[slot].kind==Cell::Thunk) {
            Cell occurrence=cells[slot];occurrence.properties.clear();occurrence.useScope=c.scope;
            auto value=flow(addCell(std::move(occurrence)),before);
            equation(cells[out.value].knownType,demand(value.value,Facet::Type));equation(cells[out.value].knownValue,demand(value.value,Facet::Value));equation(out.state,value.state);equation(out.code,value.code);return;
        }
        if(Id reaching=reachingValue(slot,before);reaching!=none) {requireInitialized(slot,before);done(reaching,before,terms().make("Done"));}
        else {auto value=flow(slot,before);equation(cells[out.value].knownType,demand(value.value,Facet::Type));equation(cells[out.value].knownValue,demand(value.value,Facet::Value));equation(out.state,value.state);equation(out.code,value.code);}
        return;
    }
    if(n.syntax==Syntax::Member) {flowMember(id,before,out);return;}
    if(c.kind==Cell::SyntaxCell && n.syntax==Syntax::Assign) {flowAssign(id,before,out);return;}
    if(c.kind==Cell::SyntaxCell && n.syntax==Syntax::Destructure) {flowDestructure(id,before,out);return;}
    if(c.kind==Cell::Alias || (c.kind==Cell::Missing && n.syntax==Syntax::Block)) {
        if(c.locals.empty()) {flowSequence(c.prefix,c.target,before,out);return;}
        Flow block{known(fresh("block result type"),fresh("block result value")),fresh("block state"),fresh("block completion")};
        flowSequence(c.prefix,c.target,before,block);equation(cells[out.value].knownType,demand(block.value,Facet::Type));equation(cells[out.value].knownValue,demand(block.value,Facet::Value));equation(out.code,block.code);
        later({block.state},[this,id,out](const auto& ends){
            auto next=states[ends[0]->number.template convert_to<Id>()];
            for(Id slot:cells[id].locals) {next.values.erase(slot);next.initialized.erase(slot);}
            equation(out.state,number(state(std::move(next))));
        });return;
    }
    if(c.kind==Cell::Missing) {
        if(n.syntax==Syntax::Construct && c.absence==Diagnostic::UndefinedType) {
            flowItems(c.prefix,before,[this,id,out](const auto&,Id at,Ref prefix){
                continueFlow(at,prefix,out,[this,id](Id end,const Flow& result){
                    (void)end;(void)result;pending(cells[id].problem,cells[id].site);
                });
            });
        } else pending(c.problem,c.site);
        return;
    }
    if(n.syntax==Syntax::Select) {
        auto condition=flow(c.items[0],before);
        later({condition.state},[this,id,condition,out](const auto& ends){
            continueFlow(ends[0]->number.template convert_to<Id>(),condition.code,out,[this,id,condition](Id start,const Flow& branch){
                auto boolean=fresh("execution condition");
                request.goals.push_back(call("implicit",{demand(condition.value,Facet::Type),terms().make("Bool"),demand(condition.value,Facet::Value),boolean}));
                later({conditionedValue(terms().make("Bool"),boolean,states[start].conditions,!states[start].controlShadow)},[this,id,start,branch](const auto& tests){
                    const auto& items=cells[id].items;const auto& test=tests[0];
                    auto empty=[&]{return Flow{known(terms().make("NoValue"),terms().make("NoValue")),number(start),terms().make("Done")};};
                    if(test->kind==Term::Number) {
                        Flow chosen=test->number!=0 ? flow(items[1],start) : items.size()==3 ? flow(items[2],start) : empty();
                        equation(cells[branch.value].knownType,demand(chosen.value,Facet::Type));equation(cells[branch.value].knownValue,demand(chosen.value,Facet::Value));
                        equation(branch.state,chosen.state);equation(branch.code,chosen.code);
                    } else if(test->name=="Runtime") {
                        auto yes=states[start],no=yes;yes.conditions.push_back(test);no.conditions.push_back(booleanNot(test));
                        auto first=flow(items[1],state(std::move(yes)));
                        auto second=items.size()==3 ? flow(items[2],state(std::move(no))) : empty();
                        flowMerge(test,first,second,start,branch);
                    }
                    else conflict();
                });
            });
        });return;
    }
    if(c.kind==Cell::Record) {flowRecord(id,before,out);return;}
    if(n.syntax==Syntax::Unary && n.text=="|") {
        later({demand(c.items[0],Facet::Category)},[this,id,before,out](const auto& categories){
            if(categories[0]->name=="Array") {
                Cell pure=cells[id];pure.flowPure=true;pure.properties.clear();Id value=addCell(std::move(pure));
                equation(cells[out.value].knownType,demand(value,Facet::Type));equation(cells[out.value].knownValue,demand(value,Facet::Value));
                equation(out.state,number(before));equation(out.code,terms().make("Done"));return;
            }
            auto operand=flow(cells[id].items[0],before);
            later({operand.state},[this,id,operand,out](const auto& ends){
                continueFlow(ends[0]->number.template convert_to<Id>(),operand.code,out,[this,id,operand](Id at,const Flow& result){
                    Cell pure=cells[id];pure.flowPure=true;pure.properties.clear();pure.items={operand.value};Id value=addCell(std::move(pure));
                    equation(cells[result.value].knownType,demand(value,Facet::Type));equation(cells[result.value].knownValue,demand(value,Facet::Value));
                    equation(result.state,number(at));equation(result.code,terms().make("Done"));
                });
            });
        });return;
    }
    auto items=c.items;
    if(c.kind==Cell::Arithmetic && !items.empty()) {
        Id slot=location(items[0]),value=reachingValue(slot,before);
        if(value!=none) requireInitialized(slot,before);
        items[0]=value==none ? slot : value;
    }
    if(n.syntax==Syntax::Cast) items={c.items[1]};
    flowItems(items,before,[this,id,out](auto values,Id end,Ref code){
        continueFlow(end,code,out,[this,id,values](Id at,const Flow& result) mutable {
        auto evaluate=[this,id,values,at,result]() mutable {
        Cell pure=cells[id];pure.flowPure=true;pure.properties.clear();
        if(graph.nodes[pure.site].syntax==Syntax::Cast) values.insert(values.begin(),pure.items[0]);
        pure.items=std::move(values);Id value=addCell(std::move(pure));
        equation(cells[result.value].knownType,demand(value,Facet::Type));equation(cells[result.value].knownValue,demand(value,Facet::Value));
        equation(result.state,number(at));
        if(graph.nodes[cells[id].site].syntax==Syntax::Index) {
            auto checks=fresh("index completion"),element=fresh("indexed type");
            Id owner=cells[value].items[0],index=cells[value].items[1];
            later({demand(owner,Facet::Type)},[this,owner,index,element,checks](const auto& types){
                Ref indexed=types[0];
                if(indexed->name=="String") indexed=terms().make("Array",{terms().make("UInt",{number(8)}),demand(owner,Facet::Length)});
                placeType(indexed,{terms().make("Subscript",{demand(index,Facet::Value)})},element,checks);
            });
            equation(result.code,checks);
        } else equation(result.code,arithmeticCompletion(value));
        };
        evaluate();
        });
    });
}
void Session::flowReturn(const Flow& body,Id before,const std::vector<Id>& captured,const Flow& out) {
    equation(cells[out.value].knownType,demand(body.value,Facet::Type));
    equation(cells[out.value].knownValue,demand(body.value,Facet::Value));equation(out.code,body.code);
    later({body.state},[this,before,captured,out](const auto& args){
        auto returned=states[before];const auto& end=states[args[0]->number.template convert_to<Id>()];
        for(Id slot:captured) if(auto found=end.values.find(slot);found!=end.values.end()) {returned.values[slot]=found->second;returned.initialized[slot]=initialization(slot,end);}
        returned.live=end.live;returned.exits=end.exits;
        equation(out.state,number(state(std::move(returned))));
    });
}
void Session::flowInvoke(Definition definition,std::vector<Id> arguments,Id site,Id callerScope,Id before,const Flow& out) {
    if(states[before].schema) definition.scope=reachingScope(definition.scope,callerScope);
    const auto& fn=graph.nodes[definition.site];
    if(fn.children.size()!=arguments.size()) {conflict();return;}
    Id parameterScope=environment(definition.scope);
    for(std::size_t i=0;i<arguments.size();++i) {
        const auto& parameter=graph.nodes[fn.children[i]];
        if(parameter.expression) continue;
        if(i<fn.receiverCount) {
            if(cells[arguments[i]].kind!=Cell::Receiver) {conflict();return;}
            auto receiver=cells[arguments[i]];
            if(parameter.annotation!=none) {
                auto supplied=demand(receiver.target,Facet::Type),required=appliedAnnotation(parameter.annotation,parameterScope,supplied);
                if(receiver.temporaryReceiver) {
                    auto value=fresh("converted temporary receiver");conversion(receiver.target,required,demand(receiver.target,Facet::Value),value);
                    receiver.target=known(required,value,site);receiver.properties.clear();
                    auto entry=states[before];entry.values[receiver.location]=receiver.target;before=state(std::move(entry));
                    arguments[i]=addCell(std::move(receiver));
                } else equation(supplied,required);
            }
            environments[parameterScope].values[parameter.text]=arguments[i];continue;
        }
        arguments[i]=bound(fn.children[i],parameterScope,parameter.annotation,arguments[i]);cells[arguments[i]].flowPure=true;
        environments[parameterScope].values[parameter.text]=arguments[i];
        (void)demand(arguments[i],Facet::Value);
    }
    if(fn.key=="bundled-math") {
        auto value=flow(instantiate(definition,arguments,site,callerScope),before);
        equation(cells[out.value].knownType,demand(value.value,Facet::Type));equation(cells[out.value].knownValue,demand(value.value,Facet::Value));
        equation(out.state,value.state);equation(out.code,value.code);return;
    }
    if(fn.external) {flowExternal(definition,arguments,before,out);return;}
    if(!fn.noinline) {
        auto callSite="inline:"+std::to_string(site);
        for(const auto& active:states[before].calls) if(active.first==definition.site && active.second==callSite) {pending("recursive execution requires &~",site);return;}
        auto next=states[before];next.calls.emplace_back(definition.site,callSite);
        Id body=instantiate(definition,arguments,site,callerScope,true,{},true);
        auto captured=callCaptures(definition,arguments,true);
        if(std::any_of(fn.children.begin(),fn.children.end(),[this](Id param){return graph.nodes[param].expression;})) {
            auto add=[&](Id scope){if(scope==none) return;auto caller=captures(scope);captured.insert(captured.end(),caller.begin(),caller.end());};
            add(callerScope);std::set<Id> visited;
            std::function<void(Id)> trace=[&](Id argument){
                if(argument==none || !visited.insert(argument).second) return;
                expand(argument);const auto& cell=cells[argument];add(cell.scope);
                if(cell.kind==Cell::Thunk || cell.kind==Cell::Alias) trace(cell.target);
            };
            for(std::size_t i=0;i<arguments.size();++i) if(graph.nodes[fn.children[i]].expression) trace(arguments[i]);
            std::sort(captured.begin(),captured.end());captured.erase(std::unique(captured.begin(),captured.end()),captured.end());
        }
        flowReturn(flow(body,state(std::move(next))),before,captured,out);return;
    }
    selectCaptures(definition,arguments,site,callerScope,before,callCaptures(definition,arguments),
        [this,definition,arguments,site,callerScope,before,out](const CapturePlan& plan){
    const auto& captured=plan.slots;
    std::vector<Ref> inputs;
    for(Id argument:arguments) {inputs.push_back(demand(argument,Facet::Type));inputs.push_back(cells[argument].kind==Cell::Receiver ? terms().make("Path",cells[argument].receiverPath) : demand(argument,Facet::Value));}
    for(std::size_t i=0;i<captured.size();++i) if(plan.inputs[i] && captureAvailable(captured[i],before)) {
        auto value=states[before].values.at(captured[i]);
        requireInitialized(captured[i],before);inputs.push_back(demand(value,Facet::Type));inputs.push_back(demand(value,Facet::Value));
    }
    later(inputs,[this,definition,arguments,site,callerScope,before,plan,out](const auto& args){
        bool runtime=false;std::string key=std::to_string(definition.site)+":"+std::to_string(definition.scope);
        for(const auto& item:args) {key+="|"+show(item);if(runtimeValue(item)) runtime=true;}
        if(runtime) {
            std::vector<Ref> types;for(std::size_t i=0;i<arguments.size();++i) types.push_back(args[i*2]);
            materialize(definition,arguments,types,site,callerScope,before,plan,out);return;
        }
        for(const auto& active:states[before].calls) if(active.second==key) {pending("recursive execution revisits an unfinished state",site);return;}
        auto next=states[before];next.calls.emplace_back(definition.site,key);
        Id body=instantiate(definition,arguments,site,callerScope,true,{},true);
        flowReturn(flow(body,state(std::move(next))),before,callCaptures(definition,arguments,true),out);
    });
    });
}
void Session::runtime(Ref type,Ref expression,Ref result) {
    later({type},[this,expression,result](const auto& args){
        std::size_t offset=0;
        auto value=structure(args[0],[&](const Ref& scalar){
            if(!nativeScalar(scalar)) {pending("runtime interface needs concrete scalar leaves");return fresh("runtime leaf");}
            auto indices=expression->arguments;auto index=fresh("runtime leaf index");
            request.goals.push_back(add(indices.back(),number(offset++),index));indices.back()=index;
            unsigned bits=scalarBits(scalar);
            if(expression->name=="LoopValue" || expression->name=="LoopSlotValue") indices.push_back(scalar);
            return terms().make("Runtime",{terms().make(expression->name,indices),
                number(scalarSigned(scalar) ? -(Integer(1)<<(bits-1)) : Integer(0)),
                number((Integer(1)<<(scalarSigned(scalar) ? bits-1 : bits))-1)});
        });
        equation(result,value);
    });
}
bool Session::captureAvailable(Id slot,Id at) {
    if(!states[at].values.contains(slot)) return false;
    auto ready=initialized(slot,states[at]);
    // A typed location need not have an initialized value. Conditional
    // readiness still needs a caller proof.
    return ready->kind!=Term::Number || ready->number!=0;
}
void Session::materialize(Definition definition,const std::vector<Id>& arguments,const std::vector<Ref>& types,
                         Id site,Id scope,Id before,const CapturePlan& plan,const Flow& out) {
    const auto& captured=plan.slots;
    runtimeCode();
    for(std::size_t i=0;i<captured.size();++i) {
        captureUse(captured[i],before,plan.outputs[i]);
    }
    std::vector<Ref> captureTypes;for(Id slot:captured) {
        auto type=locationType(slot,before);captureTypes.push_back(type);
        if(plan.conservative) request.goals.push_back(awaitInput(type,graph.location(graph.nodes[site].span)+
            ": capture interface needs a grounded type; annotate/initialize the captured state or pass it as an explicit parameter"));
    }
    auto prerequisites=captureTypes;
    for(Id argument:arguments) if(cells[argument].kind==Cell::Receiver) prerequisites.push_back(terms().make("Path",cells[argument].receiverPath));
    const auto valuesBegin=prerequisites.size();
    for(Id argument:arguments) if(cells[argument].kind!=Cell::Receiver) prerequisites.push_back(demand(argument,Facet::Value));
    const auto capturesBegin=prerequisites.size();
    for(std::size_t i=0;i<captured.size();++i) if(plan.inputs[i] && captureAvailable(captured[i],before)) {
        requireInitialized(captured[i],before);
        prerequisites.push_back(demand(states[before].values.at(captured[i]),Facet::Value));
    }
    later(prerequisites,[this,definition,arguments,types,site,scope,before,captured,plan,out,valuesBegin,capturesBegin](const auto& facts){
        std::vector<Ref> capTypes(facts.begin(),facts.begin()+static_cast<std::ptrdiff_t>(captured.size()));
        // Receiver roots cross the ABI once as captures. Dynamic designations
        // cross as scalar indices; no source alias is turned into a C pointer.
        std::vector<Ref> abiTypes,callValues;std::map<std::size_t,std::vector<Ref>> paths;
        std::vector<std::vector<Leaf>> argumentLeaves(arguments.size());
        std::vector<std::vector<std::size_t>> argumentIndices(arguments.size());
        std::map<std::pair<std::size_t,std::size_t>,std::size_t> indexParameters;
        const auto active=states[before].runtimeInterfaces.find({definition.site,definition.scope});
        const bool recursive=active!=states[before].runtimeInterfaces.end();
        RuntimeInterface interface;interface.types=types;interface.arguments.resize(arguments.size());
        if(recursive) {
            if(types.size()!=active->second.types.size() || !std::equal(types.begin(),types.end(),active->second.types.begin(),identical)) {
                pending("recursive runtime parameters need a fixed interface; supply explicit parameter types and fixed shapes",site);return;
            }
        }
        std::string key=std::to_string(definition.site)+":"+std::to_string(definition.scope);
        // A reusable body is validated under its enclosing recursive
        // interfaces. Reusing a helper constructed from another root must not
        // hide a later incompatible back edge. Our own old interface has
        // already been checked above and is replaced on entry to this frame.
        for(const auto& [owner,required]:states[before].runtimeInterfaces) {
            if(owner==std::make_pair(definition.site,definition.scope)) continue;
            key+="|ancestor:"+std::to_string(owner.first)+":"+std::to_string(owner.second);
            for(const auto& type:required.types) key+="|type:"+show(type);
            for(std::size_t i=0;i<required.arguments.size();++i) {
                key+="|argument:"+std::to_string(i);
                for(const auto& value:required.arguments[i]) key+="|leaf:"+show(value);
            }
            for(const auto& [slot,value]:required.captures) key+="|capture:"+std::to_string(slot)+":"+show(value);
        }
        if(states[before].schema) key+=":schema";
        for(const auto& [owner,lexical]:states[before].captureProofs) key+=":capture-proof:"+std::to_string(owner)+":"+std::to_string(lexical);
        if(states[before].captureProofIdentity!=none) key+=":proof-instance:"+std::to_string(states[before].captureProofIdentity);
        for(Id slot:states[before].immutableRoots) key+=":immutable:"+std::to_string(slot);
        for(const auto& type:types) key+="|"+show(type);
        // Static facts stay in the specialization. Only runtime leaves cross
        // its ABI. In particular a known string has no target pointer, and a
        // constant dimension must not become unknown merely because another
        // argument is supplied by a source activation.
        std::size_t valueFact=valuesBegin;
        for(std::size_t i=0;i<arguments.size();++i) if(cells[arguments[i]].kind!=Cell::Receiver) {
            argumentLeaves[i]=leaves(types[i],facts[valueFact++]);
            key+="|argument:"+std::to_string(i);
            for(std::size_t j=0;j<argumentLeaves[i].size();++j) {
                const auto& leaf=argumentLeaves[i][j];
                auto previous=recursive ? active->second.arguments.at(i).at(j) : leaf.value;
                bool changed=recursive && !identical(previous,leaf.value);
                if(changed && !nativeScalar(leaf.type)) {
                    pending("recursive compile-time argument changes; use an explicit parameter with a fixed runtime representation",site);return;
                }
                if(runtimeValue(leaf.value) || (nativeScalar(leaf.type) && changed)) {
                    interface.arguments[i].push_back(terms().make("Dynamic"));
                    key+="|dynamic";argumentIndices[i].push_back(abiTypes.size());
                    abiTypes.push_back(leaf.type);callValues.push_back(leaf.value);
                } else {interface.arguments[i].push_back(leaf.value);key+="|constant:"+show(leaf.value);argumentIndices[i].push_back(none);}
            }
        }
        std::size_t pathFact=captured.size();
        for(std::size_t i=0;i<arguments.size();++i) if(cells[arguments[i]].kind==Cell::Receiver) {
            key+="|receiver:"+std::to_string(cells[arguments[i]].location);
            paths[i]=facts[pathFact++]->arguments;
            for(std::size_t j=0;j<paths[i].size();++j) {
                const auto& step=paths[i][j];
                if(step->name=="Subscript" && step->arguments[0]->name=="Runtime") {
                    auto value=step->arguments[0];auto indexType=terms().make(value->arguments[1]->number<0 ? "SInt" : "UInt",{number(32)});
                    key+="|index:"+show(indexType);indexParameters[{i,j}]=abiTypes.size();abiTypes.push_back(indexType);callValues.push_back(value);
                } else key+="|"+show(step);
            }
        }
        std::vector<Ref> captureInputs(captured.size());std::size_t captureFact=capturesBegin;
        for(std::size_t i=0;i<captured.size();++i) {
            bool input=plan.inputs[i] && captureAvailable(captured[i],before);
            key+="|capture:"+std::to_string(captured[i])+":"+show(capTypes[i])+(input ? ":input" : ":no-input")+(plan.outputs[i] ? ":output" : ":no-output");
            std::vector<Ref> fixed{capTypes[i]};
            if(input) {
                captureInputs[i]=facts[captureFact++];
                for(const auto& leaf:leaves(capTypes[i],captureInputs[i])) if(!nativeScalar(leaf.type)) {
                    if(runtimeValue(leaf.value)) {pending("runtime capture needs a target representation",site);return;}
                    key+="|constant:"+show(leaf.value);fixed.push_back(leaf.value);
                }
            }
            interface.captures[captured[i]]=terms().make("StaticCapture",fixed);
            if(recursive) if(auto prior=active->second.captures.find(captured[i]);prior!=active->second.captures.end() &&
                !identical(prior->second,interface.captures.at(captured[i]))) {
                pending("recursive capture changes its static interface; pass state with a fixed runtime representation explicitly",site);return;
            }
        }
        for(const auto& type:abiTypes) for(const auto& leaf:leaves(type)) if(!nativeScalar(leaf.type)) {pending("runtime parameters need concrete scalar leaves",site);return;}
        auto found=frames.find(key);
        if(found==frames.end()) {
            Frame frame;frame.id=nextFrame++;frame.resultType=fresh("recursive result type");frame.resultPattern=fresh("recursive result interface");
            frame.capturePatterns=fresh("recursive capture interfaces");frame.effect=fresh("recursive boundary effect");frame.captures=captured;
            frame.bundle="runtime_frame_"+std::to_string(frame.id);frame.firstAutomatic=cells.size();
            for(std::size_t i=0;i<captured.size();++i) frame.captureInputs.push_back(plan.inputs[i] && captureAvailable(captured[i],before));
            frame.captureOutputs=plan.outputs;
            frames.emplace(key,frame);
            frameCodes.emplace(frame.id,fresh("runtime frame execution"));
            auto goals=constructed([&]{
                SupportScope detachedEffects(effectInputs,nullptr),detachedRuntime(runtimeInputs,nullptr);
                FlowState entry;entry.frame=frame.id;entry.live=number(1);entry.schema=states[before].schema;
                entry.runtimeInterfaces=states[before].runtimeInterfaces;entry.runtimeInterfaces[{definition.site,definition.scope}]=interface;
                entry.captureProofs=states[before].captureProofs;
                entry.captureProofIdentity=states[before].captureProofIdentity;
                entry.controlShadow=states[before].controlShadow;
                entry.immutableRoots=states[before].immutableRoots;
                std::vector<Id> abiParams;std::size_t parameterOffset=0,captureOffset=0;
                for(const auto& type:abiTypes) {
                    auto value=fresh("runtime parameter");runtime(type,terms().make("FrameArg",{number(parameterOffset)}),value);parameterOffset+=leaves(type).size();
                    abiParams.push_back(known(type,value,site));
                }
                std::vector<Id> params(arguments.size(),none);
                for(std::size_t i=0;i<arguments.size();++i) if(cells[arguments[i]].kind!=Cell::Receiver) {
                    std::size_t leaf=0;
                    auto value=structure(types[i],[&](const Ref&){
                        auto index=argumentIndices[i][leaf];auto constant=argumentLeaves[i][leaf++].value;
                        return index==none ? constant : demand(abiParams[index],Facet::Value);
                    });
                    params[i]=known(types[i],value,site);
                }
                for(const auto& [i,path]:paths) {
                    Cell receiver=cells[arguments[i]];receiver.properties.clear();receiver.temporaryReceiver=false;
                    receiver.target=known(types[i],{},site);receiver.receiverPath=path;
                    for(std::size_t j=0;j<path.size();++j) if(auto parameter=indexParameters.find({i,j});parameter!=indexParameters.end())
                        receiver.receiverPath[j]=terms().make("Subscript",{demand(abiParams[parameter->second],Facet::Value)});
                    params[i]=addCell(std::move(receiver));
                }
                for(std::size_t i=0;i<captured.size();++i) {
                    entry.locationTypes[captured[i]]=capTypes[i];
                    if(frame.captureInputs[i]) {
                        auto value=fresh("runtime capture");frameValue(capTypes[i],captureInputs[i],terms().make("CaptureArg",{number(captureOffset)}),value);captureOffset+=nativeLeafCount(capTypes[i]);
                        entry.values[captured[i]]=known(capTypes[i],value,site);
                    }
                    entry.initialized[captured[i]]=number(frame.captureInputs[i] ? 1 : 0);
                }
                Id body=instantiate(definition,params,site,scope,true,{},true);
                Id entryState=state(std::move(entry));auto execution=flow(body,entryState);
                equation(frameCodes.at(frame.id),execution.code);
                // Effects belong to the closed call graph, including recursive
                // edges. Every activation uses this same promise, regardless
                // of which caller first constructed the frame's obligations.
                auto accesses=boundaryAccesses(execution.code,{frame.id},{});
                later({accesses},[this,frame](const auto& closed){
                    std::function<bool(const Ref&)> hasAccess=[&](const Ref& term){
                        if(term->name=="Access") return true;
                        return std::any_of(term->arguments.begin(),term->arguments.end(),hasAccess);
                    };
                    equation(frame.effect,number(hasAccess(closed[0]) ? 1 : 0));
                });
                equation(frame.resultType,demand(execution.value,Facet::Type));
                auto returnValue=demand(execution.value,Facet::Value);
                equation(frame.resultPattern,framePattern(frame.resultType,returnValue));
                later({execution.state},[this,frame,abiTypes,capTypes,execution,returnValue,entryState,site](const auto& end){
                    const auto& finalState=states[end[0]->number.template convert_to<Id>()];std::vector<Ref> returned,patterns;
                    for(std::size_t i=0;i<frame.captures.size();++i) {
                        Id slot=frame.captures[i];
                        auto value=finalState.values.find(slot);
                        if(!frame.captureOutputs[i]) {
                            auto initial=states[entryState].values.find(slot);
                            if((value==finalState.values.end())!=(initial==states[entryState].values.end()) ||
                               (value!=finalState.values.end() && value->second!=initial->second)) {pending("capture closure omitted a changed output");return;}
                            returned.push_back(terms().make("Ignored"));patterns.push_back(terms().make("Ignored"));continue;
                        }
                        if(value==finalState.values.end()) {pending("captured output is not definitely initialized; initialize it or return explicit state",site);return;}
                        requireInitialized(slot,end[0]->number.template convert_to<Id>());
                        returned.push_back(demand(value->second,Facet::Value));
                        patterns.push_back(framePattern(capTypes[i],returned.back()));
                    }
                    equation(frame.capturePatterns,terms().make("Values",patterns));
                    std::vector<Ref> inputs;for(bool input:frame.captureInputs) inputs.push_back(number(input ? 1 : 0));
                    std::vector<Ref> outputFlags;for(bool output:frame.captureOutputs) outputFlags.push_back(number(output ? 1 : 0));
                    request.goals.push_back(witness(frame.bundle,terms().make("Function",{number(frame.id),terms().make("Types",abiTypes),
                        terms().make("Types",capTypes),frame.resultType,execution.code,returnValue,terms().make("Values",returned),terms().make("Inputs",inputs),terms().make("Outputs",outputFlags)})));
                });
            });
            program.bundles.emplace(frame.bundle,std::move(goals));found=frames.find(key);
        }
        const auto frame=found->second;request.goals.push_back(require(frame.bundle));
        if(recursive) {
            // Recursive calls tie to the existing interface. This wait is a
            // diagnostic dependency, not speculative execution or a guessed
            // type. Base/result equations or an annotation must close it.
            request.goals.push_back(awaitInput(frame.resultType,graph.location(graph.nodes[definition.site].span)+
                ": recursive result interface needs a return annotation (-> #Type) or a grounded result relationship"));
        }
        if(effectInputs) effectInputs->push_back(frame.effect);
        Id temporary=nextTemporary++;std::vector<Ref> capturesIn;
        if(!states[before].controlShadow &&
           std::none_of(graph.nodes[definition.site].children.begin(),graph.nodes[definition.site].children.end(),
               [this](Id parameter){return graph.nodes[parameter].expression;})) {
            std::vector<Ref> supplied(arguments.size());std::map<std::size_t,NativeReceiver> receivers;
            std::size_t inputFact=valuesBegin;
            for(std::size_t i=0;i<arguments.size();++i) {
                if(auto path=paths.find(i);path!=paths.end()) receivers.emplace(i,NativeReceiver{cells[arguments[i]].location,path->second});
                else supplied[i]=facts[inputFact++];
            }
            nativeControlCalls.emplace(temporary,NativeControlCall{definition,types,supplied,
                captured,capTypes,captureInputs,frame.captureInputs,frame.captureOutputs,receivers,site,scope,before});
        }
        for(std::size_t i=0;i<captured.size();++i) if(frame.captureInputs[i])
            for(const auto& leaf:leaves(capTypes[i],captureInputs[i])) if(nativeScalar(leaf.type)) capturesIn.push_back(leaf.value);
        equation(cells[out.value].knownType,frame.resultType);
        frameValue(frame.resultType,frame.resultPattern,terms().make("CallValue",{number(temporary),number(0)}),cells[out.value].knownValue);
        auto returned=states[before];auto resultSize=fresh("call result slots");
        later({frame.resultType},[this,resultSize](const auto& args){equation(resultSize,number(std::max(std::size_t(1),nativeLeafCount(args[0]))));});
        std::size_t captureOffset=0;
        std::vector<Ref> capturePatterns;for(std::size_t i=0;i<captured.size();++i) capturePatterns.push_back(fresh("returned capture interface"));
        equation(frame.capturePatterns,terms().make("Values",capturePatterns));
        for(std::size_t i=0;i<captured.size();++i) if(frame.captureOutputs[i]) {
            auto index=fresh("capture output index");request.goals.push_back(add(resultSize,number(captureOffset),index));captureOffset+=nativeLeafCount(capTypes[i]);
            auto value=fresh("returned capture");frameValue(capTypes[i],capturePatterns[i],terms().make("CallValue",{number(temporary),index}),value);
            returned.values[captured[i]]=known(capTypes[i],value,site);returned.initialized[captured[i]]=number(1);
        }
        equation(out.state,number(state(std::move(returned))));
        auto total=fresh("call temporary slots");request.goals.push_back(add(resultSize,number(captureOffset),total));
        equation(out.code,terms().make("Invoke",{number(temporary),number(frame.id),terms().make("Values",callValues),terms().make("Values",capturesIn),total}));
    });
}
void Session::flowMerge(Ref condition,const Flow& left,const Flow& right,Id before,const Flow& out) {
    auto merge=[this,condition](Id a,Id b) {
        auto type=demand(a,Facet::Type);equation(type,demand(b,Facet::Type));auto result=fresh("merged value");
        selectValue(type,condition,demand(a,Facet::Value),demand(b,Facet::Value),result);return known(type,result);
    };
    later({left.code,right.code},[this,condition,out](const auto& args){
        equation(out.code,args[0]->name=="Done" && args[1]->name=="Done" ? terms().make("Done") : terms().make("If",{condition,args[0],args[1]}));
    });
    later({left.state,right.state},[this,before,condition,left,right,merge,out](const auto& args){
        Id aId=args[0]->number.template convert_to<Id>(),bId=args[1]->number.template convert_to<Id>();
        later({states[aId].live,states[bId].live},[this,aId,bId,before,condition,left,right,merge,out](const auto& live){
            auto dead=[](const Ref& v){return v->kind==Term::Number && v->number==0;};
            Id value=dead(live[0]) ? right.value : dead(live[1]) ? left.value : merge(left.value,right.value);
            equation(cells[out.value].knownType,demand(value,Facet::Type));equation(cells[out.value].knownValue,demand(value,Facet::Value));
            const auto& a=states[aId];const auto& b=states[bId];auto next=states[before];std::set<Id> slots;
            for(const auto& [slot,item]:a.values) {(void)item;slots.insert(slot);}
            for(const auto& [slot,item]:b.values) {(void)item;slots.insert(slot);}
            for(Id slot:slots) {
                Id first=reachingValue(slot,aId),second=reachingValue(slot,bId);
                if(first!=none && second!=none) next.values[slot]=first==second ? first : merge(first,second);
                else next.values[slot]=first!=none ? first : second;
                next.initialized[slot]=mergeInitialization(condition,initialization(slot,a),initialization(slot,b));
            }
            next.live=fresh("normal completion guard");selectValue(terms().make("Bool"),condition,a.live,b.live,next.live);
            auto baseline=states[before].exits.size();
            for(std::size_t i=baseline;i<a.exits.size();++i) {auto exit=a.exits[i];exit.condition=booleanAnd(condition,exit.condition);next.exits.push_back(exit);}
            for(std::size_t i=baseline;i<b.exits.size();++i) {auto exit=b.exits[i];exit.condition=booleanAnd(booleanNot(condition),exit.condition);next.exits.push_back(exit);}
            equation(out.state,number(state(std::move(next))));
        });
    });
}
