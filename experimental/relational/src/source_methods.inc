// A receiver denotes one logical root and a path designated at the call site.
// It is not a copied aggregate or a source pointer. Every access consults the
// current state of that root, so overlapping receiver views share all writes.
std::string Session::callName(Id id) const {return cells[id].syntheticCall ? cells[id].inputName : graph.nodes[cells[id].site].text;}
std::size_t Session::callReceivers(Id id) const {return cells[id].syntheticCall ? 1 : graph.nodes[cells[id].site].receiverCount;}
std::string Session::operatorName(Id id) const {
    const auto& cell=cells[id];
    if(cell.builtin || cell.syntheticCall || cell.scope==none || cell.site==none) return {};
    if(cell.kind==Cell::Arithmetic) return cell.inputName;
    if(cell.kind!=Cell::SyntaxCell) return {};
    const auto& node=graph.nodes[cell.site];
    return node.syntax==Syntax::Binary || node.syntax==Syntax::Unary ? node.text : std::string{};
}
Id Session::methodCall(Id site,Id scope,const std::string& name,const std::vector<Id>& arguments,bool allowBuiltin) {
    Cell call;call.kind=Cell::Dispatch;call.expanded=true;call.syntheticCall=true;call.allowBuiltin=allowBuiltin;
    call.site=site;call.scope=scope;call.inputName=name;call.items=arguments;call.prefix=arguments;return addCell(std::move(call));
}
Id Session::operatorCall(Id id) {
    if(auto found=operatorCalls.find(id);found!=operatorCalls.end()) return found->second;
    auto op=operatorName(id);if(op.empty() || functions(cells[id].scope,"."+op).empty()) return none;
    Id call=methodCall(cells[id].site,cells[id].scope,"."+op,cells[id].items,true);operatorCalls[id]=call;return call;
}
void Session::operatorProperty(Id id,Id call,Facet facet,Ref result) {
    later({demand(call,Facet::Target)},[this,id,call,facet,result](const auto& target){
        if(target[0]->name=="Builtin") {
            Cell base=cells[id];base.properties.clear();base.builtin=true;equation(result,demand(addCell(std::move(base)),facet));return;
        }
        equation(result,demand(call,facet));
    });
}
void Session::flowOperator(Id id,Id call,Id before,const Flow& out) {
    later({demand(call,Facet::Target)},[this,id,call,before,out](const auto& target){
        Cell selected=cells[target[0]->name=="Builtin" ? id : call];selected.properties.clear();selected.builtin=true;selected.allowBuiltin=false;
        auto execution=flow(addCell(std::move(selected)),before);
        equation(cells[out.value].knownType,demand(execution.value,Facet::Type));equation(cells[out.value].knownValue,demand(execution.value,Facet::Value));
        equation(out.state,execution.state);equation(out.code,execution.code);
    });
}
void Session::memberProbe(Id owner,const std::string& member,Ref result) {
    later({demand(owner,Facet::Category)},[this,owner,member,result](const auto& categories){
        if(categories[0]->name.starts_with("Opaque")) {pending("member presence needs a closed outer category");return;}
        if(categories[0]->name!="Record") {equation(result,number(0));return;}
        later({demand(owner,Facet::Identity)},[this,member,result](const auto& identities){
            const auto& identity=identities[0];
            if(identity->name=="Tuple") {
                auto count=identity->arguments[0]->number.template convert_to<std::size_t>();bool found=false;
                for(std::size_t i=0;i<count;++i) found=found || member=="__"+std::to_string(i);
                equation(result,number(found ? 1 : 0));return;
            }
            if(identity->name=="Nominal") {
                for(const auto& node:graph.nodes) if(node.syntax==Syntax::RecordDecl && node.identity==identity->arguments[1]->name) {
                    bool found=std::any_of(node.children.begin(),node.children.end(),[&](Id field){return graph.nodes[field].text==member;});
                    equation(result,number(found ? 1 : 0));return;
                }
            }
            pending("member presence needs a closed record identity");
        });
    });
}
bool Session::mutableReceiver(Id argument) {
    const auto& cell=cells[argument];
    if(cell.kind==Cell::Receiver) return !cell.temporaryReceiver;
    if(cell.flowPure || cell.site==none || cell.scope==none) return false;
    std::function<bool(Id)> placeSyntax=[&](Id site) {
        const auto& node=graph.nodes[site];
        if(node.syntax==Syntax::Group || node.syntax==Syntax::Member || node.syntax==Syntax::Index) return placeSyntax(node.children[0]);
        if(node.syntax!=Syntax::Name) return false;
        Id slot=lookup(cell.scope,node.text);
        if(slot==none || cells[slot].readonly) return false;
        slot=location(slot);
        return !cells[slot].readonly && (cells[slot].kind==Cell::Bound || cells[slot].kind==Cell::Receiver);
    };
    return placeSyntax(cell.site);
}
Id Session::captureLocation(Id value) const {
    Id slot=location(value);
    return cells[slot].kind==Cell::Receiver ? cells[slot].location : slot;
}
std::vector<Definition> Session::callCandidates(Id site,Id scope) const {
    const auto& node=graph.nodes[site];
    if(node.syntax==Syntax::Call) return functions(scope,node.text);
    std::string op;
    if(node.syntax==Syntax::Binary || node.syntax==Syntax::Unary) op=node.text;
    else if(node.syntax==Syntax::Iterate) op=node.text;
    else if(node.syntax==Syntax::Assign && node.text!="=") op=node.text.substr(0,node.text.size()-1);
    if(op.empty()) return {};
    auto candidates=functions(scope,"."+op);
    if(op.size()>1 && op[0]=='.' && op!="..") {
        auto scalar=functions(scope,op);candidates.insert(candidates.end(),scalar.begin(),scalar.end());
    }
    return candidates;
}
std::vector<Id> Session::callCaptures(Definition definition,const std::vector<Id>& arguments,bool visible) const {
    auto result=captures(definition.scope,visible ? none : definition.site);
    for(Id argument:arguments) if(cells[argument].kind==Cell::Receiver && !global(cells[argument].location)) result.push_back(cells[argument].location);
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
void Session::flowReceivers(const std::vector<Id>& arguments,const std::vector<bool>& deferred,std::size_t count,Id site,Id before,const std::function<void(std::vector<Id>,Id,Ref)>& finish) {
    if(!count) {flowArguments(arguments,deferred,before,finish);return;}
    if(arguments.empty()) {conflict();return;}
    later({states[before].live},[this,arguments,deferred,count,site,before,finish](const auto& live){
        if(live[0]->kind==Term::Number && live[0]->number==0) {finish(arguments,before,terms().make("Done"));return;}
        auto rest=std::vector<Id>(arguments.begin()+1,arguments.end());auto modes=std::vector<bool>(deferred.begin()+1,deferred.end());
        Id argument=arguments[0];
        auto next=[this,rest,modes,count,site,finish](Id receiver,Id at,Ref prefix) {
            flowReceivers(rest,modes,count-1,site,at,[this,receiver,prefix,finish](auto values,Id end,Ref code){values.insert(values.begin(),receiver);finish(values,end,sequence(prefix,code));});
        };
        if(mutableReceiver(argument)) {
            auto designate=[this,argument,next](Id slot,const std::vector<Ref>& path,Id at,Ref prefix) {
                later({states[at].live},[this,argument,next,slot,path,at,prefix](const auto& active){
                    if(active[0]->kind==Term::Number && active[0]->number==0) {next(argument,at,prefix);return;}
                    auto present=[this,argument,next,slot,path,at,prefix](Ref type,Ref checks){
                        Cell receiver;receiver.kind=Cell::Receiver;receiver.expanded=true;receiver.site=cells[argument].site;receiver.scope=cells[argument].scope;
                        receiver.location=slot;receiver.receiverPath=path;receiver.target=known(type,{},receiver.site);
                        next(addCell(std::move(receiver)),at,sequence(prefix,checks));
                    };
                    resolvePlaceType(locationType(slot,at),path,present);
                });
            };
            if(cells[argument].kind==Cell::Receiver) designate(cells[argument].location,cells[argument].receiverPath,before,terms().make("Done"));
            else place(cells[argument].site,cells[argument].scope,before,designate);
            return;
        }
        auto value=flow(argument,before);
        later({value.state},[this,argument,value,next,site](const auto& ends){
            Id at=ends[0]->number.template convert_to<Id>();
            Id slot=bound(site,cells[argument].scope,none,value.value);
            auto entry=states[at];entry.values[slot]=value.value;entry.initialized[slot]=number(1);
            Cell receiver;receiver.kind=Cell::Receiver;receiver.expanded=true;receiver.temporaryReceiver=true;
            receiver.site=site;receiver.scope=cells[argument].scope;receiver.location=slot;receiver.target=value.value;
            next(addCell(std::move(receiver)),state(std::move(entry)),value.code);
        });
    });
}
void Session::flowReceiver(Id id,Id before,const Flow& out) {
    const auto& receiver=cells[id];auto root=flow(receiver.location,before);auto path=receiver.receiverPath;
    later({root.state},[this,root,path,out](const auto& ends){
        continueFlow(ends[0]->number.template convert_to<Id>(),root.code,out,[this,root,path](Id at,const Flow& result){
            auto type=demand(root.value,Facet::Type),value=demand(root.value,Facet::Value);
            for(const auto& step:path) {
                auto partType=fresh("receiver part type"),part=fresh("receiver part value");
                if(step->name=="Field") memberValue(type,value,step->arguments[0]->name,partType,part);
                else {equation(type,terms().make("Array",{partType,fresh("receiver extent")}));indexValue(type,value,step->arguments[0],part);}
                type=partType;value=part;
            }
            equation(cells[result.value].knownType,type);equation(cells[result.value].knownValue,value);
            equation(result.state,number(at));equation(result.code,terms().make("Done"));
        });
    });
}
void Session::storePlace(Id slot,const std::vector<Ref>& path,Id value,Id before,const Flow& out,bool statement) {
    if(cells[slot].readonly || states[before].immutableRoots.contains(slot)) {conflict();return;}
    auto commit=[this,value,slot,path,out,statement](Id end,Ref type,Ref old,Ref prefix){
        auto updated=fresh("updated logical root"),assigned=fresh("stored logical part"),partType=fresh("logical part type"),checks=fresh("logical store bounds");
        placeType(type,path,partType,checks);replaceValue(type,old,path,value,updated,assigned);
        equation(cells[out.value].knownType,statement ? terms().make("NoValue") : partType);
        equation(cells[out.value].knownValue,statement ? terms().make("NoValue") : assigned);
        if(global(slot)) {
            Flow written{known(fresh("stored type"),fresh("stored value")),fresh("stored state"),fresh("stored code")};
            flowGlobalStore(slot,known(type,updated),end,written,true,placeMask(type,path));
            equation(out.state,written.state);equation(out.code,sequence(prefix,sequence(checks,written.code)));return;
        }
        auto next=states[end];next.values[slot]=known(type,updated);next.initialized[slot]=number(1);
        captureUse(slot,end,true);
        equation(out.state,number(state(std::move(next))));equation(out.code,sequence(prefix,sequence(checks,validateSchema(type,updated))));
    };
    auto type=locationType(slot,before);
    if(path.empty()) {commit(before,type,fresh("unread previous root"),terms().make("Done"));return;}
    auto previous=flow(slot,before);
    later({previous.state},[this,previous,commit](const auto& loaded){commit(loaded[0]->number.template convert_to<Id>(),demand(previous.value,Facet::Type),demand(previous.value,Facet::Value),previous.code);});
}
void Session::flowReceiverStore(Id id,Id before,const Flow& out) {
    auto receiver=cells[location(id)];Id slot=receiver.location;auto path=receiver.receiverPath;
    auto rhs=flow(cells[id].target,before);
    later({rhs.state},[this,rhs,slot,path,out](const auto& ends){
        continueFlow(ends[0]->number.template convert_to<Id>(),rhs.code,out,[this,rhs,slot,path](Id at,const Flow& result){storePlace(slot,path,rhs.value,at,result,true);});
    });
}

void Session::flowCall(Id id,Id before,const Flow& out) {
    const auto& c=cells[id];
        auto arguments=c.kind==Cell::Invocation ? c.items : c.prefix;
        auto definitions=c.kind==Cell::Invocation ? std::vector<Definition>{c.definition} : functions(c.scope,callName(id));
        std::vector<bool> deferred(arguments.size(),false);
        for(std::size_t i=0;i<arguments.size();++i) {
            std::optional<bool> expression;
            for(const auto& def:definitions) if(graph.nodes[def.site].children.size()==arguments.size()) {
                bool candidate=graph.nodes[graph.nodes[def.site].children[i]].expression;
                if(expression && *expression!=candidate) {pending("overloads disagree on expression argument mode",c.site);return;}
                expression=candidate;
            }
            deferred[i]=expression.value_or(false);
        }
        flowReceivers(arguments,deferred,callReceivers(id),c.site,before,[this,id,out,definitions](const auto& values,Id afterArguments,Ref prefix){
            continueFlow(afterArguments,prefix,out,[this,id,values,definitions](Id next,const Flow& invoked){
            Flow called{known(fresh("invoked type"),fresh("invoked value")),fresh("invoked state"),fresh("invoked code")};
            equation(cells[invoked.value].knownType,demand(called.value,Facet::Type));
            equation(cells[invoked.value].knownValue,demand(called.value,Facet::Value));equation(invoked.code,called.code);
            later({called.state},[this,values,invoked](const auto& ends){
                auto end=states[ends[0]->number.template convert_to<Id>()];
                for(Id value:values) if(cells[value].kind==Cell::Receiver && cells[value].temporaryReceiver) {
                    Id root=cells[value].location;end.values.erase(root);end.initialized.erase(root);
                }
                equation(invoked.state,number(state(std::move(end))));
            });
            const auto& callCell=cells[id];
            if(definitions.empty()) {
                pending("execution call has no definition",callCell.site);
                return;
            }
            if(definitions.size()==1 && !callReceivers(id)) {flowInvoke(definitions[0],values,callCell.site,callCell.scope,next,called);return;}
            Cell selection;selection.kind=Cell::Dispatch;selection.expanded=true;selection.flowPure=true;
            selection.site=callCell.site;selection.scope=callCell.scope;selection.items=values;selection.syntheticCall=callCell.syntheticCall;selection.inputName=callCell.inputName;Id selected=addCell(std::move(selection));
            later({demand(selected,Facet::Target)},[this,id,values,next,called](const auto& args){
                Definition def{args[0]->arguments[0]->number.template convert_to<Id>(),args[0]->arguments[1]->number.template convert_to<Id>()};
                flowInvoke(def,values,cells[id].site,cells[id].scope,next,called);
            });
            });
        });
}
