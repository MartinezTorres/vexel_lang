// Runtime loops close a finite interface once. The back edge carries logical
// values, never host addresses or recursive source expansion. The C projection
// uses parallel assignments so every update observes the preceding iteration.
void Session::runtimeLoop(Id id,Id before,const Flow& out) {
    const auto& n=graph.nodes[cells[id].site];
    if(n.text=="condition") {buildLoop(id,before,out,{}, {},terms().make("Done"));return;}
    auto iterable=flow(cells[id].items[0],before);
    later({iterable.state},[this,id,out,iterable](const auto& args){
        continueFlow(args[0]->number.template convert_to<Id>(),iterable.code,out,[this,id,iterable](Id at,const Flow& continuation){
            later({demand(iterable.value,Facet::Type),demand(iterable.value,Facet::Value)},[this,id,at,continuation](const auto& values){
                if(values[0]->name!="Array" || values[1]->name!="Values") {
                    conflict();
                    return;
                }
                buildLoop(id,at,continuation,values[0],values[1],terms().make("Done"));
            });
        });
    });
}
void Session::buildLoop(Id id,Id before,const Flow& out,Ref iterableType,Ref iterableValue,Ref prefix) {
    runtimeCode();
    std::vector<Id> oldSlots;std::vector<Ref> inputTypes;
    for(const auto& [slot,value]:states[before].values) {oldSlots.push_back(slot);inputTypes.push_back(demand(value,Facet::Type));}
    later(inputTypes,[this,id,before,out,iterableType,iterableValue,prefix,oldSlots](const auto& oldTypes){
        Id loop=nextLoop++,firstTemporary=nextTemporary;auto entry=states[before];
        entry.exits.clear();entry.live=number(1);entry.loopTargets.push_back(id);entry.initializationLoop=loop;
        auto& context=loopInitializations[loop];std::set<Id> excluded;
        for(std::size_t i=0;i<oldSlots.size();++i) {
            auto flat=leaves(oldTypes[i]);
            if(std::any_of(flat.begin(),flat.end(),[](const Leaf& leaf){return !nativeScalar(leaf.type);})) excluded.insert(oldSlots[i]);
        }
        auto visible=captures(cells[id].scope);visible.insert(visible.end(),oldSlots.begin(),oldSlots.end());
        for(Id slot:visible) if(!excluded.contains(slot) && !context.slots.contains(slot)) {
            bool existing=states[before].values.contains(slot);Id initial=reachingValue(slot,before);
            Cell proxy;proxy.kind=Cell::LoopSlot;proxy.expanded=true;proxy.flowPure=true;proxy.site=cells[slot].site;
            proxy.target=initial==none ? slot : initial;proxy.location=loop;proxy.previous=slot;
            if(initial==none) if(auto type=states[before].locationTypes.find(slot);type!=states[before].locationTypes.end()) proxy.target=known(type->second,{},cells[slot].site);
            Id value=addCell(std::move(proxy));context.slots.emplace(slot,LoopSlotInfo{value,initial,existing});
            // Absent roots remain absent until a selected relationship reads
            // or writes them. Merely being in lexical scope closes no type.
            if(existing) entry.values[slot]=value;
            entry.initialized[slot]=terms().make("Runtime",{terms().make("LoopReady",{number(loop),number(slot)}),number(0),number(1)});
        }
        Id start=state(std::move(entry)),scope=environment(cells[id].scope);
        Flow check{known(terms().make("Bool"),number(1)),number(start),terms().make("Done")};Ref condition=fresh("runtime loop condition");
        if(iterableType) {
            auto count=arrayCount(iterableType);
            auto cursor=terms().make("Runtime",{terms().make("LoopIndex",{number(loop)}),number(0),number(count)});
            equation(condition,count==0 ? number(0) : terms().make("Runtime",{terms().make("Compare",{terms().make("<"),cursor,number(count)}),number(0),number(1)}));
            auto selected=fresh("loop element");if(count!=0) indexValue(iterableType,iterableValue,cursor,selected);
            Id element=known(iterableType->arguments[0],selected);cells[element].readonly=true;environments[scope].values["_"]=element;
        } else {
            check=flow(expression(graph.nodes[cells[id].site].children[0],scope),start);
            later({check.state},[this,check,condition](const auto& statesAt){
                auto live=states[statesAt[0]->number.template convert_to<Id>()].live;
                later({live},[this,check,condition](const auto& guards){
                    if(guards[0]->kind==Term::Number && guards[0]->number==0) {equation(condition,number(0));return;}
                    auto boolean=fresh("loop predicate");request.goals.push_back(call("implicit",{demand(check.value,Facet::Type),terms().make("Bool"),demand(check.value,Facet::Value),boolean}));
                    equation(condition,booleanAnd(guards[0],boolean));
                });
            });
        }
        later({check.state},[this,id,before,out,prefix,loop,scope,check,condition,iterableType,firstTemporary](const auto& checked){
            Id checkedId=checked[0]->number.template convert_to<Id>();auto bodyEntry=states[checkedId];bodyEntry.conditions.push_back(condition);bodyEntry.live=condition;
            auto body=flow(expression(graph.nodes[cells[id].site].children[1],scope),state(std::move(bodyEntry)));
            later({body.state},[this,id,before,out,prefix,loop,check,condition,body,checkedId,iterableType,firstTemporary](const auto& ended){
                Id end=ended[0]->number.template convert_to<Id>();const auto& finalState=states[end];
                const auto& slots=loopInitializations.at(loop).slots;
                for(const auto& [slot,old]:states[before].values) if(!slots.contains(slot)) {
                    auto found=finalState.values.find(slot);
                    if(found!=finalState.values.end() && found->second!=old) {
                        later({demand(old,Facet::Value),demand(found->second,Facet::Value)},[this,id](const auto& values){
                            if(!identical(values[0],values[1])) pending("runtime back edge needs a concrete carried representation",cells[id].site);
                        });
                    }
                }
                std::vector<Id> carried;std::vector<Ref> prerequisites;
                for(const auto& [slot,info]:slots) if(info.existing || states[checkedId].values.contains(slot) || finalState.values.contains(slot) || cells[info.proxy].properties.contains(Facet::Value)) {
                    carried.push_back(slot);auto type=demand(info.proxy,Facet::Type);
                    prerequisites.push_back(type);
                    auto a=reachingValue(slot,checkedId),b=reachingValue(slot,end);
                    equation(type,demand(a,Facet::Type));equation(type,demand(b,Facet::Type));
                }
                std::vector<FlowExit> returns;std::vector<Ref> phases;Ref stopped=number(0),checkAgain=number(0),escaped=number(0);
                for(std::size_t index=0;index<finalState.exits.size();++index) {
                    const auto& exit=finalState.exits[index];
                    if(index<states[checkedId].exits.size() && exit.target==id && exit.kind=="continue")
                        checkAgain=booleanNot(booleanAnd(booleanNot(checkAgain),booleanNot(exit.condition)));
                    if(exit.kind=="break" || exit.target!=id) stopped=booleanNot(booleanAnd(booleanNot(stopped),booleanNot(exit.condition)));
                    if(exit.target!=id) {escaped=booleanNot(booleanAnd(booleanNot(escaped),booleanNot(exit.condition)));phases.push_back(number(index<states[checkedId].exits.size() ? 1 : 0));returns.push_back(exit);prerequisites.push_back(demand(exit.value,Facet::Type));prerequisites.push_back(demand(exit.value,Facet::Value));}
                }
                auto readiness=closeLoopInitialization(loop,before,checkedId,end,condition,stopped,checkAgain,escaped,carried,iterableType,firstTemporary);
                later(prerequisites,[this,before,out,prefix,loop,carried,check,condition,body,checkedId,end,returns,phases,stopped,checkAgain,readiness](const auto& facts){
                    auto next=states[before];std::vector<Ref> cases;std::size_t returnOffset=0;
                    std::vector<Ref> initial,afterCheck,afterBody,storageTypes,descriptors,initialReady,checkReady,bodyReady,outputReady;
                    for(std::size_t i=0;i<carried.size();++i) {
                        Id slot=carried[i];const auto& info=loopInitializations.at(loop).slots.at(slot);auto type=facts[i];auto flat=leaves(type);
                        if(std::any_of(flat.begin(),flat.end(),[](const Leaf& leaf){return !nativeScalar(leaf.type);})) {pending("runtime loop storage needs native scalar leaves",cells[slot].site);return;}
                        descriptors.push_back(terms().make("Slot",{number(slot),number(flat.size())}));
                        for(const auto& leaf:flat) storageTypes.push_back(leaf.type);
                        auto value=fresh("completed loop value");runtime(type,terms().make("LoopSlotValue",{number(loop),number(slot),number(0)}),value);
                        bool unchanged=info.initial!=none && reachingValue(slot,checkedId)==info.proxy && reachingValue(slot,end)==info.proxy;
                        next.values[slot]=unchanged ? info.initial : known(type,value);
                        auto ready=fresh("completed loop initialization");outputReady.push_back(ready);
                        auto physical=terms().make("Runtime",{terms().make("LoopReady",{number(loop),number(slot)}),number(0),number(1)});
                        next.initialized[slot]=InitializationFacts{ready,physical};
                        if(info.initial!=none) initial.push_back(demand(info.initial,Facet::Value));
                        else initial.push_back(structure(type,[this](const Ref&){return number(0);}));
                        afterCheck.push_back(demand(reachingValue(slot,checkedId),Facet::Value));afterBody.push_back(demand(reachingValue(slot,end),Facet::Value));
                        initialReady.push_back(runtimeInitialized(slot,states[before]));checkReady.push_back(runtimeInitialized(slot,states[checkedId]));bodyReady.push_back(runtimeInitialized(slot,states[end]));
                    }
                    equation(readiness,terms().make("Ready",outputReady));
                    for(std::size_t i=0;i<returns.size();++i) {
                        auto flag=terms().make("Runtime",{terms().make("LoopFlag",{number(loop),number(i)}),number(0),number(1)});
                        auto resultType=facts[carried.size()+2*i],result=facts[carried.size()+2*i+1];auto flat=leaves(resultType,result);std::size_t at=0;std::vector<Ref> copied;
                        auto value=structure(resultType,[&](const Ref&){
                            const auto& leaf=flat[at++];if(!runtimeValue(leaf.value)) return leaf.value;
                            if(leaf.value->name!="Runtime") throw std::logic_error("invalid runtime loop return leaf");
                            copied.push_back(leaf.value);
                            return terms().make("Runtime",{terms().make("LoopReturned",{number(loop),number(returnOffset++)}),leaf.value->arguments[1],leaf.value->arguments[2]});
                        });
                        next.exits.push_back({returns[i].kind,flag,known(resultType,value),returns[i].target});
                        cases.push_back(terms().make("ReturnCase",{returns[i].condition,terms().make("Values",copied),phases[i]}));
                    }
                    Ref returnedGuard=number(0);
                    for(std::size_t i=states[before].exits.size();i<next.exits.size();++i)
                        returnedGuard=booleanNot(booleanAnd(booleanNot(returnedGuard),booleanNot(next.exits[i].condition)));
                    next.live=booleanNot(returnedGuard);equation(out.state,number(state(std::move(next))));
                    equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));
                    auto code=terms().make("Loop",{number(loop),terms().make("Values",initial),check.code,condition,terms().make("Values",afterCheck),body.code,terms().make("Values",afterBody),stopped,terms().make("Returns",cases),checkAgain,terms().make("Types",storageTypes),terms().make("Slots",descriptors),terms().make("Ready",initialReady),terms().make("Ready",checkReady),terms().make("Ready",bodyReady)});
                    equation(out.code,sequence(prefix,code));
                });
            });
        });
    });
}
