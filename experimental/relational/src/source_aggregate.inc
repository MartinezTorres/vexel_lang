Ref Session::structure(const Ref& type,const std::function<Ref(const Ref&)>& scalar) {
    if(type->name=="NoValue") return terms().make("NoValue");
    std::vector<Ref> values;
    if(type->name=="Array") {
        for(std::size_t i=0;i<arrayCount(type);++i) values.push_back(structure(type->arguments[0],scalar));
        return terms().make("Values",values);
    }
    if(type->name=="Record") {
        for(const auto& field:type->arguments[1]->arguments) values.push_back(structure(field->arguments[0],scalar));
        return terms().make("RecordValue",{type->arguments[0],terms().make("Values",values)});
    }
    return scalar(type);
}
void Session::selectValue(Ref type,Ref condition,Ref left,Ref right,Ref result) {
    later({type,condition},[this,left,right,result](const auto& facts){
        const auto& t=facts[0];const auto& cond=facts[1];
        if(cond->kind==Term::Number) {equation(result,cond->number!=0 ? left : right);return;}
        if(aggregate(t)) {
            std::vector<Ref> a,b,merged;
            auto first=structure(t,[&](const Ref& leaf){
                a.push_back(fresh("then branch leaf"));b.push_back(fresh("else branch leaf"));merged.push_back(fresh("selected branch leaf"));
                selectValue(leaf,cond,a.back(),b.back(),merged.back());return a.back();
            });
            std::size_t at=0;auto second=structure(t,[&](const Ref&){return b[at++];});at=0;
            equation(left,first);equation(right,second);equation(result,structure(t,[&](const Ref&){return merged[at++];}));return;
        }
        // Strings are immutable compile-time values. A runtime branch can
        // preserve one string fact but cannot choose two different contents.
        if(t->name=="String") {equation(left,right);equation(result,left);return;}
        selectScalarValue(t,cond,left,right,result);
    });
}
void Session::selectScalarValue(Ref type,Ref condition,Ref left,Ref right,Ref result) {
    later({type,condition,left,right},[this,result](const auto& args){
        const auto& t=args[0];const auto& cond=args[1];const auto& a=args[2];const auto& b=args[3];
        if(identical(a,b)) {equation(result,a);return;}
        if(t->name=="Bool" && a->kind==Term::Number && b->kind==Term::Number) {equation(result,a->number!=0 ? cond : booleanNot(cond));return;}
        if(t->name=="NoValue") {equation(result,terms().make("NoValue"));return;}
        auto al=fresh("then lower"),ah=fresh("then upper"),bl=fresh("else lower"),bh=fresh("else upper"),lo=fresh("merge lower"),hi=fresh("merge upper");
        request.goals.push_back(call("value_range",{a,al,ah}));request.goals.push_back(call("value_range",{b,bl,bh}));
        request.goals.push_back(call("minimum",{al,bl,lo}));request.goals.push_back(call("maximum",{ah,bh,hi}));
        equation(result,terms().make("Runtime",{terms().make("Select",{cond,a,b}),lo,hi}));
    });
}
void Session::indexValue(Ref type,Ref array,Ref index,Ref result) {
    later({type},[this,array,index,result](const auto& types){
        const auto& t=types[0];
        if(t->name=="String") {
            later({array},[this,index,result](const auto& values){
                if(values[0]->name!="Bytes") {conflict();return;}
                indexValue(terms().make("Array",{terms().make("UInt",{number(8)}),number(values[0]->arguments.size())}),
                           terms().make("Values",values[0]->arguments),index,result);
            });return;
        }
        if(t->name!="Array") {conflict();return;}
        auto count=arrayCount(t);std::vector<Ref> items;
        for(std::size_t i=0;i<count;++i) items.push_back(fresh("indexed element"));
        equation(array,terms().make("Values",items));
        // A variable index remains a relation: source finite choices need not
        // become ground before they constrain an element of an array.
        auto name="structural_index_"+std::to_string(count);
        if(!program.relations.contains(name)) {
            std::vector<Clause> clauses;
            for(std::size_t i=0;i<count;++i) {
                std::vector<Ref> v;for(std::size_t j=0;j<count;++j) v.push_back(fresh("element"));
                clauses.push_back({{terms().make("Values",v),number(i),v[i]},{},"element"});
            }
            program.define(name,3,std::move(clauses));
        }
        later({index},[this,t,array,index,result,name,count](const auto& indices){
            if(indices[0]->kind==Term::Number) {request.goals.push_back(call(name,{array,index,result}));return;}
            if(indices[0]->name!="Runtime") {conflict();return;}
            if(count==0) {conflict();return;}
            later({array},[this,t,index,result,count](const auto& values){
                const auto& elements=values[0]->arguments;
                std::vector<std::vector<Leaf>> flat;for(const auto& e:elements) flat.push_back(leaves(t->arguments[0],e));
                std::size_t at=0;
                auto selected=structure(t->arguments[0],[&](const Ref&){
                    std::vector<Ref> choices;Ref lower,upper;
                    for(std::size_t i=0;i<count;++i) {
                        auto v=flat[i][at].value,lo=fresh("indexed low"),hi=fresh("indexed high");
                        request.goals.push_back(call("value_range",{v,lo,hi}));
                        if(i==0) {lower=lo;upper=hi;}
                        else {auto l=fresh("index lower"),h=fresh("index upper");request.goals.push_back(call("minimum",{lower,lo,l}));request.goals.push_back(call("maximum",{upper,hi,h}));lower=l;upper=h;}
                        choices.push_back(v);
                    }
                    ++at;return terms().make("Runtime",{terms().make("Index",{index,terms().make("Values",choices)}),lower,upper});
                });
                equation(result,selected);
            });
        });
    });
}
void Session::memberValue(Ref type,Ref value,const std::string& name,Ref resultType,Ref result) {
    later({type},[this,value,name,resultType,result](const auto& types){
        const auto& t=types[0];
        if(t->name!="Record") {conflict();return;}
        const auto& fields=t->arguments[1]->arguments;
        auto f=std::find_if(fields.begin(),fields.end(),[&](const Ref& field){return field->name==name;});
        if(f==fields.end()) {conflict();return;}
        equation(resultType,(*f)->arguments[0]);
        if(value) {
            std::vector<Ref> values;for(std::size_t i=0;i<fields.size();++i) values.push_back(fresh("member value"));
            equation(value,terms().make("RecordValue",{t->arguments[0],terms().make("Values",values)}));
            equation(result,values[static_cast<std::size_t>(f-fields.begin())]);
        }
    });
}
void Session::flowRecord(Id id,Id before,const Flow& out) {
    auto original=cells[id];std::vector<Id> arguments;
    for(Id field:original.items) arguments.push_back(cells[field].target);
    flowItems(arguments,before,[this,original,out](const auto& values,Id end,Ref code){
        continueFlow(end,code,out,[this,original,values](Id at,const Flow& result){
            Cell recordCell=original;recordCell.flowPure=true;recordCell.properties.clear();recordCell.items.clear();
            Id frozen=environment(original.definition.scope);std::set<std::string> names;
            for(Id env=original.definition.scope;env!=none;env=environments[env].parent)
                for(const auto& [name,cell]:environments[env].values) if(names.insert(name).second) {
                    auto found=states[at].values.find(location(cell));
                    if(found!=states[at].values.end()) environments[frozen].values[name]=found->second;
                }
            recordCell.definition.scope=frozen;recordCell.recordScope=environment(frozen);
            for(std::size_t i=0;i<values.size();++i) {
                const auto& field=cells[original.items[i]];
                Id value=bound(field.site,recordCell.recordScope,field.annotation,values[i],true);
                environments[recordCell.recordScope].values[original.names[i]]=value;recordCell.items.push_back(value);
            }
            Id value=addCell(std::move(recordCell));
            equation(cells[result.value].knownType,demand(value,Facet::Type));equation(cells[result.value].knownValue,demand(value,Facet::Value));
            equation(result.state,number(at));equation(result.code,recordValidations.at(value));
        });
    });
}
void Session::flowMember(Id id,Id before,const Flow& out) {
    const auto& cell=cells[id];auto owner=flow(expression(graph.nodes[cell.site].children[0],cell.scope),before);
    later({owner.state},[this,id,owner,out](const auto& ends){
        continueFlow(ends[0]->number.template convert_to<Id>(),owner.code,out,[this,id,owner](Id at,const Flow& result){
            memberValue(demand(owner.value,Facet::Type),demand(owner.value,Facet::Value),graph.nodes[cells[id].site].text,cells[result.value].knownType,cells[result.value].knownValue);
            equation(result.state,number(at));equation(result.code,terms().make("Done"));
        });
    });
}
void Session::place(Id site,Id scope,Id before,const std::function<void(Id,std::vector<Ref>,Id,Ref)>& finish) {
    const auto& n=graph.nodes[site];
    if(n.syntax==Syntax::Name) {
        Id slot=lookup(scope,n.text);
        if(slot==none) {
            pending("unknown assignment location",site);
            return;
        }
        slot=location(slot);
        if(cells[slot].kind==Cell::Receiver) finish(cells[slot].location,cells[slot].receiverPath,before,terms().make("Done"));
        else finish(slot,{},before,terms().make("Done"));
        return;
    }
    if(n.syntax==Syntax::Group) {place(n.children[0],scope,before,finish);return;}
    if(n.syntax==Syntax::Member || n.syntax==Syntax::Index) {
        place(n.children[0],scope,before,[this,site,scope,finish](Id slot,auto path,Id at,Ref code){
            const auto& node=graph.nodes[site];
            if(node.syntax==Syntax::Member) {path.push_back(terms().make("Field",{terms().make(node.text)}));finish(slot,path,at,code);return;}
            auto index=flow(expression(node.children[1],scope),at);
            path.push_back(terms().make("Subscript",{demand(index.value,Facet::Value)}));
            later({index.state},[this,slot,path,code,index,finish](const auto& args){finish(slot,path,args[0]->number.template convert_to<Id>(),sequence(code,index.code));});
        });return;
    }
    pending("assignment requires a logical location",site);
}
void Session::placeType(Ref type,const std::vector<Ref>& path,Ref result,Ref checks) {
    resolvePlaceType(type,path,[this,result,checks](Ref resolved,Ref completion){
        equation(result,resolved);equation(checks,completion);
    });
}
void Session::resolvePlaceType(Ref type,const std::vector<Ref>& path,
                               const std::function<void(Ref,Ref)>& present,
                               const std::function<void(Diagnostic)>& absent) {
    if(path.empty()) {present(type,terms().make("Done"));return;}
    later({type,path[0]},[this,path,present,absent](const auto& args){
        const auto& t=args[0];const auto& step=args[1];Ref next,guard=terms().make("Done");
        if(step->name=="Field") {
            auto missing=[this,absent]{if(absent) absent(Diagnostic::MissingMember);else conflict();};
            if(t->name!="Record") {missing();return;}
            const auto& fields=t->arguments[1]->arguments;
            auto f=std::find_if(fields.begin(),fields.end(),[&](const Ref& field){return field->name==step->arguments[0]->name;});
            if(f==fields.end()) {missing();return;}next=(*f)->arguments[0];
        } else if(step->name=="Subscript" && t->name=="Array") {
            auto count=arrayCount(t);const auto& index=step->arguments[0];next=t->arguments[0];
            if(index->kind==Term::Number) {
                if(index->number<0 || index->number>=count) {conflict();return;}
            } else if(index->name=="Runtime") {
                if(index->arguments[1]->number<0 || index->arguments[2]->number>=count)
                    guard=terms().make("CheckIndex",{index,number(count)});
            } else {conflict();return;}
        } else {conflict();return;}
        resolvePlaceType(next,{path.begin()+1,path.end()},[this,present,guard](Ref resolved,Ref tail){
            present(resolved,sequence(guard,tail));
        },absent);
    });
}
void Session::replaceValue(Ref type,Ref value,const std::vector<Ref>& path,Id replacement,Ref result,Ref assigned) {
    if(path.empty()) {conversion(replacement,type,demand(replacement,Facet::Value),assigned);equation(result,assigned);return;}
    later({type,value,path[0]},[this,path,replacement,result,assigned](const auto& args){
        const auto& t=args[0];const auto& old=args[1];const auto& step=args[2];
        std::vector<Ref> rest(path.begin()+1,path.end());
        if(step->name=="Field" && t->name=="Record") {
            const auto& fields=t->arguments[1]->arguments;auto values=old->arguments[1]->arguments;
            auto found=std::find_if(fields.begin(),fields.end(),[&](const Ref& f){return f->name==step->arguments[0]->name;});
            if(found==fields.end()) {conflict();return;}
            auto at=static_cast<std::size_t>(found-fields.begin());auto updated=fresh("updated field");
            replaceValue((*found)->arguments[0],values[at],rest,replacement,updated,assigned);values[at]=updated;
            equation(result,terms().make("RecordValue",{t->arguments[0],terms().make("Values",values)}));return;
        }
        if(step->name!="Subscript" || t->name!="Array") {conflict();return;}
        auto values=old->arguments;const auto& index=step->arguments[0];
        if(index->kind==Term::Number) {
            if(index->number<0 || index->number>=values.size()) {conflict();return;}
            auto at=index->number.template convert_to<std::size_t>();auto updated=fresh("updated element");
            replaceValue(t->arguments[0],values[at],rest,replacement,updated,assigned);values[at]=updated;
        } else if(index->name=="Runtime") {
            auto selected=fresh("previous element"),updated=fresh("new element");
            indexValue(t,old,index,selected);replaceValue(t->arguments[0],selected,rest,replacement,updated,assigned);
            for(std::size_t i=0;i<values.size();++i) {
                auto cond=terms().make("Runtime",{terms().make("Compare",{terms().make("=="),index,number(i)}),number(0),number(1)}),merged=fresh("updated element");
                selectValue(t->arguments[0],cond,updated,values[i],merged);values[i]=merged;
            }
        } else {conflict();return;}
        equation(result,terms().make("Values",values));
    });
}
void Session::flowAssign(Id id,Id before,const Flow& out) {
    const auto& assignment=cells[id];const auto& node=graph.nodes[assignment.site];
    place(node.children[0],assignment.scope,before,[this,id,out](Id slot,const auto& path,Id designated,Ref prefix){
        continueFlow(designated,prefix,out,[this,id,slot,path](Id at,const Flow& result){
            flowAssignAt(id,slot,path,at,result);
        });
    });
}
void Session::flowAssignAt(Id id,Id slot,const std::vector<Ref>& path,Id at,const Flow& result,bool builtin) {
    const auto& source=cells[id];const auto& assignment=graph.nodes[source.site];
    if(!builtin && assignment.text!="=" && !functions(source.scope,"."+assignment.text.substr(0,assignment.text.size()-1)).empty()) {
        auto type=fresh("operator receiver type"),checks=fresh("operator receiver bounds");
        placeType(locationType(slot,at),path,type,checks);
        Cell receiver;receiver.kind=Cell::Receiver;receiver.expanded=true;receiver.site=source.site;receiver.scope=source.scope;
        receiver.location=slot;receiver.receiverPath=path;receiver.target=known(type,{},source.site);
        Id call=methodCall(source.site,source.scope,"."+assignment.text.substr(0,assignment.text.size()-1),
                           {addCell(std::move(receiver)),expression(assignment.children[1],source.scope)},true);
        later({demand(call,Facet::Target)},[this,id,slot,path,at,result,call,checks](const auto& selected){
            if(selected[0]->name=="Builtin") {flowAssignAt(id,slot,path,at,result,true);return;}
            Cell method=cells[call];method.properties.clear();method.allowBuiltin=false;auto applied=flow(addCell(std::move(method)),at);
            later({applied.state},[this,applied,slot,path,result,checks](const auto& ends){
                continueFlow(ends[0]->number.template convert_to<Id>(),sequence(checks,applied.code),result,[this,applied,slot,path](Id end,const Flow& stored){storePlace(slot,path,applied.value,end,stored,false);});
            });
        });return;
    }
    if(cells[slot].readonly || states[at].immutableRoots.contains(slot)) {conflict();return;}
    const auto& assignmentCell=cells[id];const auto& assignmentNode=graph.nodes[assignmentCell.site];
    if(assignmentNode.text.starts_with('.')) {
        auto previous=flow(slot,at);
        later({previous.state},[this,id,slot,path,previous,result](const auto& loaded){
            auto type=fresh("dotted compound place"),checks=fresh("dotted compound bounds");placeType(demand(previous.value,Facet::Type),path,type,checks);
            continueFlow(loaded[0]->number.template convert_to<Id>(),sequence(previous.code,checks),result,[this,id,slot,path,previous](Id start,const Flow& destination){
                auto rhs=flow(expression(graph.nodes[cells[id].site].children[1],cells[id].scope),start);
                later({rhs.state},[this,id,slot,path,previous,rhs,destination](const auto& ends){
                    continueFlow(ends[0]->number.template convert_to<Id>(),rhs.code,destination,[this,id,slot,path,previous,rhs](Id end,const Flow& stored){flowBroadcastStore(id,slot,path,previous.value,rhs.value,end,stored);});
                });
            });
        });return;
    }
    if(global(slot)) {flowGlobalAssign(id,slot,path,at,result);return;}
    auto checks=fresh("assignment bounds"),assignedType=fresh("assignment location type");
    auto containerType=locationType(slot,at);
    placeType(containerType,path,assignedType,checks);
    auto evaluate=[this,id,slot,path,checks](Id start,Id previous,Ref earlier,const Flow& destination){
        auto rhs=flow(expression(graph.nodes[cells[id].site].children[1],cells[id].scope),start);
        later({rhs.state},[this,id,slot,path,checks,destination,previous,earlier,rhs](const auto& ends){
            continueFlow(ends[0]->number.template convert_to<Id>(),sequence(earlier,sequence(checks,rhs.code)),destination,[this,id,slot,path,previous,rhs](Id end,const Flow& stored){
                auto next=states[end];Ref type=locationType(slot,end),old=fresh("unread initial value");
                if(auto found=next.values.find(slot);found!=next.values.end()) {
                    type=demand(found->second,Facet::Type);
                    if(!path.empty()) {old=demand(found->second,Facet::Value);requireInitialized(slot,end);}
                } else if(!path.empty()) {pending("field update needs an initialized containing value",cells[id].site);return;}
                Id replacement=rhs.value;const auto& syntax=graph.nodes[cells[id].site];
                if(previous!=none) {
                    Ref partType=demand(previous,Facet::Type),part=demand(previous,Facet::Value);
                    for(const auto& step:path) {
                        auto value=fresh("compound previous value"),subtype=fresh("compound location type");
                        if(step->name=="Field") memberValue(partType,part,step->arguments[0]->name,subtype,value);
                        else {equation(partType,terms().make("Array",{subtype,fresh("indexed length")}));indexValue(partType,part,step->arguments[0],value);}
                        partType=subtype;part=value;
                    }
                    Cell operation;operation.kind=Cell::Arithmetic;operation.flowPure=true;operation.expanded=true;operation.site=cells[id].site;
                    operation.items={known(partType,part),rhs.value};operation.inputName=syntax.text.substr(0,syntax.text.size()-1);replacement=addCell(std::move(operation));
                }
                auto updated=fresh("updated aggregate"),assigned=fresh("assigned value");
                replaceValue(type,old,path,replacement,updated,assigned);next.values[slot]=known(type,updated,cells[id].site);next.initialized[slot]=number(1);
                captureUse(slot,end,true);
                auto ignored=fresh("validated place bounds");placeType(type,path,cells[stored.value].knownType,ignored);
                equation(cells[stored.value].knownValue,assigned);equation(stored.state,number(state(std::move(next))));
                equation(stored.code,sequence(arithmeticCompletion(replacement),validateSchema(type,updated)));
            });
        });
    };
    if(assignmentNode.text=="=") evaluate(at,none,terms().make("Done"),result);
    else {
        auto previous=flow(slot,at);
        later({previous.state},[this,id,previous,path,checks,result,evaluate](const auto& loaded){
            compoundFlow(graph.nodes[cells[id].site].text,previous.value,path,loaded[0]->number.template convert_to<Id>(),sequence(previous.code,checks),result,[this,previous,evaluate](Id start,const Flow& destination){
                evaluate(start,previous.value,terms().make("Done"),destination);
            });
        });
    }
}

void Session::flowDestructure(Id id,Id before,const Flow& out) {
    const auto& assignment=cells[id];auto rhs=flow(assignment.items[0],before);
    later({rhs.state},[this,id,rhs,out](const auto& ends){
        continueFlow(ends[0]->number.template convert_to<Id>(),rhs.code,out,[this,id,rhs](Id at,const Flow& result){
            later({demand(rhs.value,Facet::Type),demand(rhs.value,Facet::Value)},[this,id,at,result](const auto& facts){
                const auto& type=facts[0];const auto& value=facts[1];const auto& destinations=cells[id].items;
                if(!tupleType(type) || type->arguments[1]->arguments.size()+1!=destinations.size()) {conflict();return;}
                std::vector<Id> stores;auto entry=states[at];
                for(std::size_t i=1;i<destinations.size();++i) {
                    Cell store=cells[destinations[i]];store.properties.clear();
                    store.target=known(type->arguments[1]->arguments[i-1]->arguments[0],value->arguments[1]->arguments[i-1],store.site);
                    // New bindings must retain their original logical slot.
                    // FlowState owns the values; these stores only name writes.
                    if(store.kind==Cell::Bound) {
                        auto fieldType=type->arguments[1]->arguments[i-1]->arguments[0];
                        entry.values[destinations[i]]=known(fieldType,fresh("uninitialized tuple destination"),store.site);
                        entry.initialized[destinations[i]]=number(0);
                        store.kind=Cell::Store;store.location=destinations[i];store.previous=destinations[i];
                    }
                    stores.push_back(addCell(std::move(store)));
                }
                flowSequence(stores,none,state(std::move(entry)),result);
            });
        });
    });
}
