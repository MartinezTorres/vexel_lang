// The iteration owns a completion target independently of the implementation's
// loops. Its supplied body retains the caller's return target through forwarding.
void Session::flowCustomIteration(Id id,Id before,const Flow& out) {
    const auto& source=cells[id];const auto& node=graph.nodes[source.site];
    Cell body;body.kind=Cell::Thunk;body.expanded=true;body.thunkActual=true;
    body.site=node.children[1];body.scope=source.scope;body.target=expression(body.site,body.scope);
    body.iterationTarget=id;body.returnTarget=states[before].returnTarget;body.loopTargets=states[before].loopTargets;
    body.loopTargets.push_back(id);body.sortedIteration=node.text=="@@";
    Id call=methodCall(source.site,source.scope,"."+node.text,{source.items[0],addCell(std::move(body))},true);
    later({demand(call,Facet::Target)},[this,id,call,before,out](const auto& selected){
        if(selected[0]->name=="Builtin") {
            Cell builtin=cells[id];builtin.properties.clear();builtin.builtin=true;
            auto execution=flow(addCell(std::move(builtin)),before);
            equation(cells[out.value].knownType,demand(execution.value,Facet::Type));equation(cells[out.value].knownValue,demand(execution.value,Facet::Value));
            equation(out.state,execution.state);equation(out.code,execution.code);return;
        }
        Cell invocation=cells[call];invocation.properties.clear();invocation.allowBuiltin=false;
        auto execution=flow(addCell(std::move(invocation)),before);
        later({execution.state},[this,id,before,execution,out](const auto& ends){
            auto next=states[ends[0]->number.template convert_to<Id>()];std::vector<FlowExit> remaining;
            for(const auto& exit:next.exits) {
                if(exit.target==id && exit.kind=="break") next.live=booleanNot(booleanAnd(booleanNot(next.live),booleanNot(exit.condition)));
                else remaining.push_back(exit);
            }
            next.exits=std::move(remaining);next.returnTarget=states[before].returnTarget;next.loopTargets=states[before].loopTargets;
            equation(out.state,number(state(std::move(next))));equation(out.code,execution.code);
            equation(cells[out.value].knownType,terms().make("NoValue"));equation(cells[out.value].knownValue,terms().make("NoValue"));
        });
    });
}
void Session::flowIterationBody(Id id,Id before,const Flow& out) {
    const auto& body=cells[id];Id item=lookup(body.useScope,"_");
    if(item==none) {pending("iteration method must bind _ before invoking its body",body.site);return;}
    auto supplied=flow(item,before);
    later({supplied.state},[this,id,before,supplied,out](const auto& ends){
        continueFlow(ends[0]->number.template convert_to<Id>(),supplied.code,out,[this,id,before,supplied](Id at,const Flow& result){
            const auto& thunk=cells[id];Id scope=environment(thunk.scope);
            Id snapshot=known(demand(supplied.value,Facet::Type),demand(supplied.value,Facet::Value),thunk.site);
            cells[snapshot].readonly=true;environments[scope].values["_"]=snapshot;
            if(thunk.sortedIteration) later({demand(snapshot,Facet::Value)},[this,id](const auto& values){
                if(runtimeValue(values[0])) pending("custom sorted iteration needs compile-time delivered elements",cells[id].site);
            });
            auto entry=states[at];entry.returnTarget=thunk.returnTarget;entry.loopTargets=thunk.loopTargets;
            auto execution=flow(expression(thunk.site,scope),state(std::move(entry)));
            later({execution.state},[this,id,before,execution,result](const auto& ended){
                auto next=states[ended[0]->number.template convert_to<Id>()];std::vector<FlowExit> remaining;
                for(const auto& exit:next.exits) {
                    if(exit.target==cells[id].iterationTarget && exit.kind=="continue") next.live=booleanNot(booleanAnd(booleanNot(next.live),booleanNot(exit.condition)));
                    else remaining.push_back(exit);
                }
                next.exits=std::move(remaining);next.returnTarget=states[before].returnTarget;next.loopTargets=states[before].loopTargets;
                equation(result.state,number(state(std::move(next))));equation(result.code,execution.code);
                equation(cells[result.value].knownType,terms().make("NoValue"));equation(cells[result.value].knownValue,terms().make("NoValue"));
            });
        });
    });
}
