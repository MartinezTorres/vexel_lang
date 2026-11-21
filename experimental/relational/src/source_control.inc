Ref Session::booleanAnd(Ref left,Ref right) {
    auto result=fresh("control conjunction");
    later({left,right},[this,result](const auto& args){
        const auto& a=args[0];const auto& b=args[1];
        if(a->kind==Term::Number) {equation(result,a->number==0 ? a : b);return;}
        if(b->kind==Term::Number) {equation(result,b->number==0 ? b : a);return;}
        if(identical(a,b)) {equation(result,a);return;}
        equation(result,terms().make("Runtime",{terms().make("And",{a,b}),number(0),number(1)}));
    });return result;
}
Ref Session::booleanNot(Ref value) {
    auto result=fresh("control negation");
    later({value},[this,result](const auto& args){
        if(args[0]->kind==Term::Number) equation(result,number(args[0]->number==0 ? 1 : 0));
        else if(args[0]->name=="Runtime" && args[0]->arguments[0]->name=="Not") equation(result,args[0]->arguments[0]->arguments[0]);
        else equation(result,terms().make("Runtime",{terms().make("Not",{args[0]}),number(0),number(1)}));
    });return result;
}
Ref Session::conditionedValue(Ref type,Ref value,const std::vector<Ref>& premises,bool expandNative) {
    auto result=fresh("value under control premises");
    // Refinement must not impose groundness on a finite value. Publish each
    // aggregate leaf independently; integer identities remain relationships.
    later({type},[this,value,premises,expandNative,result](const auto& types){
        const auto& t=types[0];
        if(aggregate(t)) {
            std::vector<Ref> refined;
            auto original=structure(t,[&](const Ref& leaf){
                auto part=fresh("unrefined control leaf");
                refined.push_back(conditionedValue(leaf,part,premises,expandNative));
                return part;
            });
            equation(value,original);std::size_t at=0;
            equation(result,structure(t,[&](const Ref&){return refined[at++];}));return;
        }
        if(!scalarFormat(t)) {equation(result,value);return;}
        auto kind=fresh("control value kind");
        request.goals.push_back(call("numeric_value_kind",{value,kind}));
        later({kind},[this,value,premises,expandNative,result](const auto& kinds){
            if(kinds[0]->name=="Integer") {equation(result,value);return;}
            later({value,terms().make("Premises",premises)},[this,result,expandNative](const auto& facts){
                ConditionalProofs::Meaning meaning;
                if(expandNative) meaning=[this](const Ref& input){return controlMeaning(input);};
                equation(result,conditionalProofs.refine(terms(),facts[0],facts[1],meaning));
            });
        });
    });return result;
}
InitializationFacts Session::initialization(Id slot,const FlowState& at) {
    auto found=at.initialized.find(slot);
    return found==at.initialized.end() ? InitializationFacts(number(at.values.contains(slot) ? 1 : 0)) : found->second;
}
Ref Session::initialized(Id slot,const FlowState& at) {return initialization(slot,at).proof;}
Ref Session::runtimeInitialized(Id slot,const FlowState& at) {return initialization(slot,at).runtime;}
InitializationFacts Session::mergeInitialization(Ref condition,const InitializationFacts& yes,const InitializationFacts& no) {
    auto evidence=fresh("merged initialization proof"),physical=fresh("merged runtime initialization");
    selectValue(terms().make("Bool"),condition,yes.proof,no.proof,evidence);
    selectValue(terms().make("Bool"),condition,yes.runtime,no.runtime,physical);
    return {evidence,physical};
}
Id Session::reachingValue(Id slot,Id at) const {
    if(auto found=states[at].values.find(slot);found!=states[at].values.end()) return found->second;
    if(auto loop=loopInitializations.find(states[at].initializationLoop);loop!=loopInitializations.end())
        if(auto found=loop->second.slots.find(slot);found!=loop->second.slots.end()) return found->second.proxy;
    return none;
}
void Session::initializationObligation(Id slot,Ref ready,const std::vector<Ref>& conditions,Id loop) {
    auto premises=conditions;premises.insert(premises.begin(),ready);
    if(loop!=none) {loopInitializations.at(loop).reads.emplace_back(slot,terms().make("InitializationRead",premises));return;}
    later(premises,[this,slot](const auto& args){
        if(args[0]->kind==Term::Number && args[0]->number!=0) return;
        ControlProof proof;std::size_t premise=1;
        for(std::size_t i=1;i<args.size();++i) premise=proof.select(premise,proof.read(args[i]),0);
        if(proof.select(premise,proof.read(args[0]),1)==1) return;
        pending("read has no definite initialization proof",cells[slot].site);
    });
}
void Session::requireInitialized(Id slot,Id at) {
    captureUse(slot,at);
    initializationObligation(slot,initialized(slot,states[at]),states[at].conditions,states[at].initializationLoop);
}
void Session::guardedFlow(Id id,Id before,const Flow& out) {
    normalFlow(before,out,[this,id](Id at,const Flow& result){flowBody(id,at,result);});
}
void Session::continueFlow(Id before,Ref prefix,const Flow& out,const FlowAction& action) {
    Flow continuation{known(fresh("continuation type"),fresh("continuation value")),fresh("continuation state"),fresh("continuation code")};
    normalFlow(before,continuation,action);
    equation(cells[out.value].knownType,demand(continuation.value,Facet::Type));equation(cells[out.value].knownValue,demand(continuation.value,Facet::Value));
    equation(out.state,continuation.state);equation(out.code,sequence(prefix,continuation.code));
}
void Session::normalFlow(Id before,const Flow& out,const FlowAction& action) {
    later({states[before].live},[this,before,out,action](const auto& live){
        if(live[0]->kind==Term::Number) {
            if(live[0]->number!=0) {action(before,out);return;}
            equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));
            equation(out.state,number(before));equation(out.code,terms().make("Done"));return;
        }
        auto entry=states[before];entry.live=number(1);entry.conditions.push_back(live[0]);
        Flow body{known(fresh("guarded type"),fresh("guarded value"),cells[out.value].site),fresh("guarded state"),fresh("guarded code")};
        action(state(std::move(entry)),body);
        equation(cells[out.value].knownType,demand(body.value,Facet::Type));equation(cells[out.value].knownValue,demand(body.value,Facet::Value));
        later({body.code},[this,body,out,condition=live[0]](const auto& code){
            equation(out.code,code[0]->name=="Done" ? code[0] : terms().make("If",{condition,body.code,terms().make("Done")}));
        });
        later({body.state},[this,before,body,out,condition=live[0]](const auto& ends){
            const auto& end=states[ends[0]->number.template convert_to<Id>()];auto next=states[before];
            for(const auto& [slot,value]:end.values) {
                Id old=reachingValue(slot,before);
                if(old!=none && old!=value) {
                    auto type=demand(value,Facet::Type),merged=fresh("guarded store");equation(type,demand(old,Facet::Type));
                    selectValue(type,condition,demand(value,Facet::Value),demand(old,Facet::Value),merged);next.values[slot]=known(type,merged);
                } else next.values[slot]=value;
                next.initialized[slot]=mergeInitialization(condition,initialization(slot,end),initialization(slot,states[before]));
            }
            for(std::size_t i=next.exits.size();i<end.exits.size();++i) {auto exit=end.exits[i];exit.condition=booleanAnd(condition,exit.condition);next.exits.push_back(exit);}
            next.live=booleanAnd(condition,end.live);equation(out.state,number(state(std::move(next))));
        });
    });
}
void Session::flowExit(Id id,Id before,const Flow& out) {
    auto finish=[this,id,before,out](Id value,Id afterOperand,Ref code){
        continueFlow(afterOperand,code,out,[this,id,value,before](Id end,const Flow& result){
        auto next=states[end];const auto& kind=graph.nodes[cells[id].site].text;
        Id target=kind=="return" ? next.returnTarget : next.loopTargets.empty() ? none : next.loopTargets.back();
        if(target==none) {pending("control transfer has no enclosing target",cells[id].site);return;}
        next.exits.push_back({kind,number(1),value,target});next.live=number(0);
        equation(result.state,number(state(std::move(next))));equation(result.code,terms().make("Done"));
        equation(cells[result.value].knownType,terms().make("NoValue"));equation(cells[result.value].knownValue,terms().make("NoValue"));
        (void)before;
        });
    };
    if(cells[id].items.empty()) {finish(known(terms().make("NoValue"),terms().make("NoValue")),before,terms().make("Done"));return;}
    flowItems(cells[id].items,before,[finish](const auto& values,Id end,Ref code){finish(values[0],end,code);});
}
void Session::finishFunction(const Flow& body,Id before,Id scope,Id annotationSite,Id owner,const Flow& out) {
    equation(out.code,body.code);
    later({body.state},[this,body,before,scope,annotationSite,owner,out](const auto& ends){
        Id end=ends[0]->number.template convert_to<Id>();auto guards=std::vector<Ref>{states[end].live};
        for(const auto& exit:states[end].exits) guards.push_back(exit.condition);
        later(guards,[this,body,before,scope,annotationSite,owner,out,end](const auto& active){
            std::vector<std::pair<Ref,Id>> results;std::vector<FlowExit> escaped;
            auto present=[](const Ref& guard){return guard->kind!=Term::Number || guard->number!=0;};
            if(present(active[0])) results.emplace_back(active[0],body.value);
            for(std::size_t i=0;i<states[end].exits.size();++i) if(present(active[i+1])) {
                const auto& exit=states[end].exits[i];
                if(exit.target!=owner) {escaped.push_back(exit);continue;}
                if(exit.kind!="return") {pending("loop exit escapes its loop");return;}
                results.emplace_back(active[i+1],exit.value);
            }
            auto finish=[this,body,before,scope,annotationSite,out,end,results,escaped]() mutable {
            if(results.empty()) {
                equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));
            }
            for(auto& [condition,value]:results) {
                (void)condition;
                if(annotationSite!=none) {Id converted=bound(cells[body.value].site,scope,annotationSite,value);cells[converted].flowPure=true;value=converted;}
            }
            Ref type=results.empty() ? terms().make("NoValue") : demand(results[0].second,Facet::Type),value=results.empty() ? terms().make("NoValue") : demand(results[0].second,Facet::Value);
            for(std::size_t i=1;i<results.size();++i) {
                equation(type,demand(results[i].second,Facet::Type));auto merged=fresh("function result");
                selectValue(type,results[i].first,demand(results[i].second,Facet::Value),value,merged);value=merged;
            }
            equation(cells[out.value].knownType,type);equation(cells[out.value].knownValue,value);
            auto next=states[end];next.live=states[before].live;
            if(!escaped.empty()) {
                next.live=number(0);
                for(const auto& result:results) next.live=booleanNot(booleanAnd(booleanNot(next.live),booleanNot(result.first)));
            }
            next.exits=states[before].exits;next.exits.insert(next.exits.end(),escaped.begin(),escaped.end());next.conditions=states[before].conditions;
            next.returnTarget=states[before].returnTarget;next.loopTargets=states[before].loopTargets;
            equation(out.state,number(state(std::move(next))));
            };
            finish();
        });
    });
}
void Session::flowIteration(Id id,Id before,const Flow& out) {
    // Runtime loop interfaces are built separately; unrolled iteration retains
    // a distinct source instance (including its local locations) per element.
    const auto& c=cells[id];const auto& n=graph.nodes[c.site];
    if(n.syntax==Syntax::Repeat) {auto entry=states[before];entry.loopTargets.push_back(id);repeatStep(id,state(std::move(entry)),states[before].exits.size(),out,{});return;}
    if(n.syntax==Syntax::Loop) {runtimeLoop(id,before,out);return;}
    if(!c.builtin && !functions(c.scope,"."+n.text).empty()) {flowCustomIteration(id,before,out);return;}
    // The iterable runs before this loop acquires its control target.
    auto iterable=flow(c.items[0],before);
    later({iterable.state},[this,id,iterable,out](const auto& args){
        continueFlow(args[0]->number.template convert_to<Id>(),iterable.code,out,[this,id,iterable](Id at,const Flow& continuation){
            later({demand(iterable.value,Facet::Type),demand(iterable.value,Facet::Value)},[this,id,at,continuation](const auto& values){
                if(values[0]->name!="Array" || values[1]->name!="Values") {
                    conflict();
                    return;
                }
                auto elements=values[1]->arguments;
                bool sorted=graph.nodes[cells[id].site].text=="@@";
                auto format=scalarFormat(values[0]->arguments[0]);
                if(sorted && format && format->kind==ScalarFormat::Float)
                    for(const auto& value:elements) if(value->kind==Term::Number && splitFloat(value->number,format->bits).nan) {conflict();return;}
                // Builtin scalar constants need no source calls. Everything
                // else uses the same overload and purity obligations as <.
                bool scalarConstants=std::all_of(elements.begin(),elements.end(),[](const Ref& value){return value->kind==Term::Number;});
                if(sorted && scalarConstants && functions(cells[id].scope,".<").empty()) {
                    if(format && format->kind==ScalarFormat::Float)
                        std::stable_sort(elements.begin(),elements.end(),[&](const Ref& a,const Ref& b){return floatCompare(a->number,b->number,format->bits).value()<0;});
                    else std::stable_sort(elements.begin(),elements.end(),[](const Ref& a,const Ref& b){return a->number<b->number;});
                    sorted=false;
                }
                std::vector<Id> items;for(const auto& value:elements) items.push_back(known(values[0]->arguments[0],value));
                auto iterate=[this,id,at,continuation](const std::vector<Id>& ordered){
                    auto entry=states[at];entry.loopTargets.push_back(id);
                    equation(cells[continuation.value].knownType,terms().make("NoValue"));equation(cells[continuation.value].knownValue,terms().make("NoValue"));
                    iterateItems(id,ordered,0,state(std::move(entry)),states[at].exits.size(),number(0),continuation);
                };
                if(sorted) sortItems(id,std::move(items),1,1,at,iterate);else iterate(items);
            });
        });
    });
}

void Session::sortItems(Id id,std::vector<Id> items,std::size_t next,std::size_t position,Id before,const std::function<void(std::vector<Id>)>& finish) {
    // Stable insertion sorting is an ordinary chain of relational calls. Only
    // the comparison result must be known: other fields may be runtime values.
    if(next>=items.size()) {finish(std::move(items));return;}
    if(!position) {sortItems(id,std::move(items),next+1,next+1,before,finish);return;}
    auto entry=states[before];
    for(Id slot:captures(cells[id].scope)) entry.immutableRoots.insert(captureLocation(slot));
    for(const auto& [slot,value]:entry.values) {(void)value;entry.immutableRoots.insert(slot);}
    Cell comparison;comparison.kind=Cell::Arithmetic;comparison.expanded=true;
    comparison.site=cells[id].site;comparison.scope=cells[id].scope;comparison.inputName="<";
    comparison.items={items[position],items[position-1]};
    auto compared=flow(addCell(std::move(comparison)),state(std::move(entry)));
    equation(compared.effect,number(0));
    equation(demand(compared.value,Facet::Type),terms().make("Bool"));
    later({demand(compared.value,Facet::Value),compared.code,compared.state},[this,id,items=std::move(items),next,position,before,finish](const auto& facts) mutable {
        if(facts[0]->kind!=Term::Number || facts[1]->name!="Done") {pending("sorted comparison needs a compile-time Boolean without runtime execution",cells[id].site);return;}
        if(facts[0]->number!=0) {
            std::swap(items[position],items[position-1]);sortItems(id,std::move(items),next,position-1,before,finish);
        } else sortItems(id,std::move(items),next+1,next+1,before,finish);
    });
}

void Session::iterateItems(Id id,const std::vector<Id>& items,std::size_t at,Id before,std::size_t exitBase,Ref breaks,const Flow& out) {
    if(at==items.size()) {
        auto next=states[before];next.live=booleanNot(booleanAnd(booleanNot(next.live),booleanNot(breaks)));
        if(!next.loopTargets.empty() && next.loopTargets.back()==id) next.loopTargets.pop_back();
        equation(out.state,number(state(std::move(next))));equation(out.code,terms().make("Done"));return;
    }
    const auto& c=cells[id];Id scope=environment(c.scope);environments[scope].values["_"]=items[at];cells[items[at]].readonly=true;
    auto body=flow(expression(graph.nodes[c.site].children[1],scope),before);
    later({body.state},[this,id,items,at,exitBase,breaks,out,body](const auto& ends){
        auto next=states[ends[0]->number.template convert_to<Id>()];auto exits=next.exits;next.exits.resize(exitBase);auto stopped=breaks;
        for(std::size_t i=exitBase;i<exits.size();++i) {
            const auto& exit=exits[i];
            if(exit.target==id && exit.kind=="continue") next.live=booleanNot(booleanAnd(booleanNot(next.live),booleanNot(exit.condition)));
            else if(exit.target==id && exit.kind=="break") stopped=booleanNot(booleanAnd(booleanNot(stopped),booleanNot(exit.condition)));
            else next.exits.push_back(exit);
        }
        Flow rest{known(terms().make("NoValue"),terms().make("NoValue")),fresh("remaining iteration state"),fresh("remaining iteration code")};
        iterateItems(id,items,at+1,state(std::move(next)),exitBase,stopped,rest);
        equation(out.state,rest.state);equation(out.code,sequence(body.code,rest.code));
    });
}

void Session::repeatStep(Id id,Id before,std::size_t exitBase,const Flow& out,std::set<std::string> visited) {
    // Cycle detection includes checks skipped by a continue in the predicate.
    // Observe the state before evaluating the condition, on every back edge.
    std::vector<Ref> inputs;std::string prefix;
    for(const auto& [slot,value]:states[before].values) {prefix+=std::to_string(slot)+":";inputs.push_back(demand(value,Facet::Value));}
    later(inputs,[this,id,before,exitBase,out,visited=std::move(visited),prefix](const auto& values) mutable {
        std::string key=prefix;for(const auto& value:values) key+=show(value)+"|";
        if(!visited.insert(key).second) {pending("unrolled repeat revisits an unfinished state",cells[id].site);return;}
        auto check=flow(cells[id].items[0],before);
        later({check.state},[this,id,exitBase,out,visited,check](const auto& ended){
            Flow rest{known(terms().make("NoValue"),terms().make("NoValue")),fresh("repeat condition state"),fresh("repeat condition code")};
            repeatAdvance(id,ended[0]->number.template convert_to<Id>(),exitBase,rest,visited,check.value);
            equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));
            equation(out.state,rest.state);equation(out.code,sequence(check.code,rest.code));
        });
    });
}
void Session::repeatAdvance(Id id,Id before,std::size_t exitBase,const Flow& out,std::set<std::string> visited,Id predicate) {
    auto next=states[before];auto exits=next.exits;next.exits.resize(exitBase);Ref stopped=number(0),again=number(0);
    for(std::size_t i=exitBase;i<exits.size();++i) {
        const auto& exit=exits[i];
        if(exit.target==id && exit.kind=="continue") again=booleanNot(booleanAnd(booleanNot(again),booleanNot(exit.condition)));
        else if(exit.target==id && exit.kind=="break") stopped=booleanNot(booleanAnd(booleanNot(stopped),booleanNot(exit.condition)));
        else next.exits.push_back(exit);
    }
    Id at=state(std::move(next));
    later({states[at].live,stopped,again},[this,id,at,exitBase,out,visited=std::move(visited),predicate](const auto& guards){
        if(std::any_of(guards.begin(),guards.end(),[](const Ref& guard){return guard->kind!=Term::Number;})) {
            auto entry=states[at];entry.live=booleanNot(booleanAnd(booleanNot(guards[0]),booleanNot(guards[2])));
            Flow rest{known(terms().make("NoValue"),terms().make("NoValue")),fresh("guarded repeat state"),fresh("guarded repeat code")};
            normalFlow(state(std::move(entry)),rest,[this,id,exitBase,visited,predicate,again=guards[2]](Id live,const Flow& continued){
                if(predicate==none || (again->kind==Term::Number && again->number!=0)) {repeatStep(id,live,exitBase,continued,visited);return;}
                if(again->kind==Term::Number) {repeatAdvance(id,live,exitBase,continued,visited,predicate);return;}
                // A continue from the predicate does not supply a Boolean.
                // Its continuation restarts the predicate; other live paths
                // consume the value it actually produced.
                auto restart=states[live],testing=states[live];restart.live=again;testing.live=booleanNot(again);
                Flow repeated{known(terms().make("NoValue"),terms().make("NoValue")),fresh("repeat restart state"),fresh("repeat restart code")};
                Flow tested{known(terms().make("NoValue"),terms().make("NoValue")),fresh("repeat test state"),fresh("repeat test code")};
                normalFlow(state(std::move(restart)),repeated,[this,id,exitBase,visited](Id start,const Flow& branch){repeatStep(id,start,exitBase,branch,visited);});
                normalFlow(state(std::move(testing)),tested,[this,id,exitBase,visited,predicate](Id start,const Flow& branch){repeatAdvance(id,start,exitBase,branch,visited,predicate);});
                flowMerge(again,repeated,tested,live,continued);
            });
            later({rest.state},[this,id,at,rest,out,stopped=guards[1]](const auto& ended){
                auto completed=states[ended[0]->number.template convert_to<Id>()];
                completed.live=booleanNot(booleanAnd(booleanNot(completed.live),booleanNot(stopped)));
                completed.loopTargets=states[at].loopTargets;
                if(!completed.loopTargets.empty() && completed.loopTargets.back()==id) completed.loopTargets.pop_back();
                completed.conditions=states[at].conditions;
                equation(out.state,number(state(std::move(completed))));equation(out.code,rest.code);
                equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));
            });return;
        }
        equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));
        auto finish=[this,at,out](bool normal){
            auto finalState=states[at];finalState.loopTargets.pop_back();finalState.live=number(normal ? 1 : 0);
            equation(out.state,number(state(std::move(finalState))));equation(out.code,terms().make("Done"));
        };
        if(guards[1]->number!=0 || (guards[0]->number==0 && guards[2]->number==0)) {finish(guards[1]->number!=0);return;}
        if(guards[2]->number!=0 || predicate==none) {
            auto entry=states[at];entry.live=number(1);repeatStep(id,state(std::move(entry)),exitBase,out,visited);return;
        }
        auto condition=fresh("repeat condition");
        request.goals.push_back(call("implicit",{demand(predicate,Facet::Type),terms().make("Bool"),demand(predicate,Facet::Value),condition}));
        later({condition},[this,id,at,exitBase,out,visited,finish](const auto& values){
            if(values[0]->kind!=Term::Number) {pending("unrolled repeat needs a compile-time condition",cells[id].site);return;}
            if(values[0]->number==0) {finish(true);return;}
            Id scope=environment(cells[id].scope);auto body=flow(expression(graph.nodes[cells[id].site].children[1],scope),at);
            later({body.state},[this,id,exitBase,out,visited,body](const auto& ended){
                Flow rest{known(terms().make("NoValue"),terms().make("NoValue")),fresh("repeat body state"),fresh("repeat body code")};
                repeatAdvance(id,ended[0]->number.template convert_to<Id>(),exitBase,rest,visited);
                equation(out.state,rest.state);equation(out.code,sequence(body.code,rest.code));
            });
        });
    });
}

void Session::flowArguments(const std::vector<Id>& arguments,const std::vector<bool>& deferred,Id before,const std::function<void(std::vector<Id>,Id,Ref)>& finish) {
    if(arguments.empty()) {finish({},before,terms().make("Done"));return;}
    std::vector<Id> rest(arguments.begin()+1,arguments.end());std::vector<bool> modes(deferred.begin()+1,deferred.end());
    if(deferred[0]) {
        if(cells[arguments[0]].iterationTarget!=none) {
            flowArguments(rest,modes,before,[value=arguments[0],finish](auto values,Id end,Ref code){values.insert(values.begin(),value);finish(values,end,code);});return;
        }
        Cell thunk;thunk.kind=Cell::Thunk;thunk.expanded=true;thunk.site=cells[arguments[0]].site;thunk.scope=cells[arguments[0]].scope;
        thunk.thunkActual=true;thunk.target=arguments[0];thunk.returnTarget=states[before].returnTarget;thunk.loopTargets=states[before].loopTargets;
        Id value=addCell(std::move(thunk));
        flowArguments(rest,modes,before,[value,finish](auto values,Id end,Ref code){values.insert(values.begin(),value);finish(values,end,code);});return;
    }
    auto first=flow(arguments[0],before);
    later({first.state},[this,first,rest,modes,finish](const auto& ends){
        flowArguments(rest,modes,ends[0]->number.template convert_to<Id>(),[this,first,finish](auto values,Id end,Ref code){values.insert(values.begin(),first.value);finish(values,end,sequence(first.code,code));});
    });
}
void Session::flowThunk(Id id,Id before,const Flow& out) {
    if(cells[id].iterationTarget!=none) {flowIterationBody(id,before,out);return;}
    const auto& c=cells[id];auto entry=states[before];
    // Binding wrappers defer to the actual argument, which owns the caller's
    // return and loop targets. Forwarding keeps that ownership intact.
    bool actual=c.thunkActual;
    if(actual) {entry.returnTarget=c.returnTarget;entry.loopTargets=c.loopTargets;}
    Id expressionId=c.target;
    if(!actual && cells[expressionId].iterationTarget!=none) {
        Cell occurrence=cells[expressionId];occurrence.properties.clear();occurrence.useScope=c.useScope;
        expressionId=addCell(std::move(occurrence));
    }
    if(actual && cells[expressionId].kind==Cell::SyntaxCell)
        expressionId=expression(cells[expressionId].site,environment(cells[expressionId].scope));
    auto body=flow(expressionId,state(std::move(entry)));
    later({body.state},[this,id,before,out,body](const auto& ended){
        auto next=states[ended[0]->number.template convert_to<Id>()];next.returnTarget=states[before].returnTarget;next.loopTargets=states[before].loopTargets;
        continueFlow(state(std::move(next)),body.code,out,[this,id,body](Id at,const Flow& result){
            const auto& thunk=cells[id];Id value=body.value;
            if(thunk.annotation!=none) {value=bound(thunk.site,thunk.scope,thunk.annotation,value);cells[value].flowPure=true;}
            auto type=demand(value,Facet::Type),resultValue=demand(value,Facet::Value);
            equation(cells[result.value].knownType,type);equation(cells[result.value].knownValue,resultValue);
            equation(result.code,validateSchema(type,resultValue));equation(result.state,number(at));
        });
    });
}
