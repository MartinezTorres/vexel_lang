// A projection names a property of a logical owner. It never substitutes the
// owner's initializer for its interface: cyclic joins may change that interface.
// The outer category is another independent projection. In particular an
// array's category never asks for its element, extent, or producer execution.
void Session::typeCategory(Ref type,Ref result) {
    later({type},[this,result](const auto& args){
        const auto& name=args[0]->name;
        if(name=="Array" || name=="Record" || name=="String" || name=="NoValue") equation(result,terms().make(name));
        else if(name=="UInt" || name=="SInt" || name=="Exact" || name=="Bool" || name=="Fixed" || name=="Float") equation(result,terms().make("Scalar"));
        else pending("outer type category remains open");
    });
}
void Session::typeProperty(Ref type,Facet facet,Ref result) {
    if(facet==Facet::Type) equation(result,type);
    else if(facet==Facet::Category) typeCategory(type,result);
    else if(facet==Facet::Shape) later({type},[this,result](const auto& facts){
        Ref leaf=facts[0];while(leaf->name=="Array") leaf=leaf->arguments[0];
        if(leaf->name!="Record" && leaf->name!="String" && leaf->name!="NoValue" && !scalarFormat(leaf)) {
            pending("array rank needs a closed element category");return;
        }
        equation(result,shapedType(terms(),terms().make("ShapeScalar"),arrayShape(facts[0])));
    });
    else if(facet==Facet::Identity) equation(type,terms().make("Record",{result,fresh("projected record fields"),fresh("projected record captures")}));
    else if(facet==Facet::Element) request.goals.push_back(call("element_type",{type,result}));
    else if(facet==Facet::Length) {
        auto element=fresh("projected element"),length=fresh("projected length");
        equation(type,terms().make("Array",{element,length}));equation(result,length);
    } else throw std::logic_error("unsupported type projection");
}
void Session::annotationCategory(Id site,Id scope,Ref result) {
    const auto& spec=graph.nodes[site];
    if(spec.text=="(tuple)") equation(result,terms().make("Record"));
    else if(!spec.children.empty()) equation(result,terms().make("Array"));
    else if(spec.body!=none) equation(result,demand(expression(spec.body,scope),Facet::Category));
    else if(unsignedWidth(spec.text) || signedWidth(spec.text) || spec.text=="b") equation(result,terms().make("Scalar"));
    else if(spec.text=="s") equation(result,terms().make("String"));
    else if(record(scope,spec.text).site!=none) equation(result,terms().make("Record"));
    else typeCategory(annotation(site,scope),result);
}
void Session::category(Id id,Ref result) {
    const auto& c=cells[id];
    if(!broadcastName(id).empty()) {
        if(Id selected=operatorCall(id);selected!=none) {operatorProperty(id,selected,Facet::Category,result);return;}
        later({demand(c.items[0],Facet::Category),demand(c.items[1],Facet::Category)},[this,id,result](const auto& operands){
            if(operands[0]->name=="Array" || operands[1]->name=="Array") equation(result,terms().make("Array"));
            else typeCategory(demand(id,Facet::Type),result);
        });return;
    }
    if(c.kind==Cell::Projection) {projectedProperty(c.target,c.names,result,Facet::Category);return;}
    if(c.kind==Cell::Element || (c.kind==Cell::SyntaxCell && graph.nodes[c.site].syntax==Syntax::Index)) {
        Id owner=c.kind==Cell::Element ? c.target : c.items[0];
        later({demand(owner,Facet::Category)},[this,id,result](const auto& categories){
            if(categories[0]->name=="String") {equation(result,terms().make("Scalar"));return;}
            later({demand(id,Facet::Shape)},[this,id,result](const auto& shapes){
                if(shapes[0]->name=="Array") equation(result,terms().make("Array"));
                else typeCategory(demand(id,Facet::Type),result);
            });
        });return;
    }
    if(c.kind==Cell::Record) {equation(result,terms().make("Record"));return;}
    if(c.kind==Cell::Alias || c.kind==Cell::Receiver || c.kind==Cell::SchemaSymbol ||
       ((c.kind==Cell::Bound || c.kind==Cell::Thunk) && c.annotation==none && c.target!=none)) {
        equation(result,demand(c.target,Facet::Category));return;
    }
    if(c.kind==Cell::Store) {equation(result,demand(c.location,Facet::Category));return;}
    if(c.kind==Cell::Dispatch) {dispatch(id,Facet::Category,result);return;}
    if(c.annotation!=none) {
        const auto& spec=graph.nodes[c.annotation];
        if(c.kind==Cell::Bound && spec.body!=none && spec.children.empty() && (global(id) || c.field)) aliasProperty(id,Facet::Category,result);
        else annotationCategory(c.annotation,c.scope,result);
        return;
    }
    if(c.kind==Cell::Invocation) {
        const auto& fn=graph.nodes[c.definition.site];
        if(fn.annotation!=none) {
            Id scope=environment(c.definition.scope);
            for(std::size_t i=0;i<c.items.size();++i) {
                const auto& param=graph.nodes[fn.children[i]];
                environments[scope].values[param.text]=bound(fn.children[i],scope,param.annotation,c.items[i]);
            }
            annotationCategory(fn.annotation,scope,result);return;
        }
    }
    if(c.kind==Cell::SyntaxCell) {
        const auto& n=graph.nodes[c.site];
        if(n.syntax==Syntax::Tuple) {equation(result,terms().make("Record"));return;}
        if(n.syntax==Syntax::Array || (n.syntax==Syntax::Binary && n.text=="..")) {equation(result,terms().make("Array"));return;}
        if(n.syntax==Syntax::Cast) {annotationCategory(graph.nodes[c.site].children[0],c.scope,result);return;}
    }
    typeCategory(demand(id,Facet::Type),result);
}
Id Session::projection(Id owner,std::vector<std::string> path) {
    if(path.empty()) return owner;
    expand(owner);
    if(cells[owner].kind==Cell::Projection) {
        auto joined=cells[owner].names;joined.insert(joined.end(),path.begin(),path.end());
        owner=cells[owner].target;path=std::move(joined);
    }
    auto key=std::make_pair(owner,path);
    if(auto found=projections.find(key);found!=projections.end()) return found->second;
    Cell cell;cell.kind=Cell::Projection;cell.expanded=true;cell.target=owner;
    cell.names=std::move(path);cell.site=cells[owner].site;cell.scope=cells[owner].scope;
    Id id=addCell(std::move(cell));projections.emplace(std::move(key),id);return id;
}
void Session::projectionOwner(Id owner,const std::function<void(Id)>& follow) {
    const auto& c=cells[owner];
    if(c.flowWrapper) {
        std::set<Id> seen;
        std::function<bool(Id)> transfers=[&](Id at) {
            if(at==none || !seen.insert(at).second) return false;
            expand(at);const auto& part=cells[at];
            if(part.kind==Cell::Alias) {
                for(Id prefix:part.prefix) if(prefix!=part.target) {
                    std::set<Id> calls;
                    if(!effectSignature(cells[prefix].site,cells[prefix].scope,calls).second) return true;
                }
                return transfers(part.target);
            }
            if(part.kind==Cell::Bound && part.target!=none) return transfers(part.target);
            if(part.site!=none && graph.nodes[part.site].syntax==Syntax::Return) return true;
            if(part.site!=none && graph.nodes[part.site].syntax==Syntax::Select)
                for(Id branch:part.items) if(transfers(branch)) return true;
            return false;
        };
        if(transfers(c.target)) {
            // Keep the function's lexical completion owner. Evaluating
            // only its body would give a return no enclosing target.
            auto executed=flow(owner,queryState(c.scope));follow(executed.value);return;
        }
    }
    bool changes=false,normal=true;
    for(Id part:c.prefix) if(part!=c.target) {
        changes=changes || changesProjectionState(part);
        std::set<Id> calls;
        normal=normal && effectSignature(cells[part].site,cells[part].scope,calls).second;
    }
    if(!normal) {
        auto executed=flow(owner,queryState(c.scope));follow(executed.value);return;
    }
    if(changes) {
        auto prefix=c.prefix;prefix.erase(std::remove(prefix.begin(),prefix.end(),c.target),prefix.end());Id target=c.target;
        flowItems(prefix,queryState(c.scope),[this,target,follow](const auto&,Id at,Ref){
            follow(frozenProperty(target,at));
        });return;
    }
    follow(c.target);return;
}
void Session::projectedProperty(Id owner,const std::vector<std::string>& path,Ref result,Facet facet) {
    if(path.empty()) {equation(result,(facet==Facet::Type ? observedType(owner) : demand(owner,facet)));return;}
    expand(owner);const auto& c=cells[owner];
    auto follow=[this,path,result,facet](Id target){equation(result,demand(projection(target,path),facet));};
    if(c.kind==Cell::Alias) {projectionOwner(owner,follow);return;}
    if(c.kind==Cell::Projection) {follow(projection(c.target,c.names));return;}
    if(c.kind==Cell::Store) {follow(c.location);return;}
    if(c.kind==Cell::Receiver) {follow(c.target);return;}
    if(c.kind==Cell::Thunk && c.annotation==none) {follow(c.target);return;}
    if(c.kind==Cell::SyntaxCell && graph.nodes[c.site].syntax==Syntax::Tuple) {
        for(std::size_t i=0;i<c.items.size();++i) if(path[0]=="__"+std::to_string(i)) {
            equation(result,demand(projection(c.items[i],{path.begin()+1,path.end()}),facet));return;
        }
        conflict();return;
    }
    if(c.kind==Cell::Record) {
        auto found=std::find(c.names.begin(),c.names.end(),path[0]);
        if(found==c.names.end()) {conflict();return;}
        Id field=c.items[static_cast<std::size_t>(found-c.names.begin())];
        equation(result,demand(projection(field,{path.begin()+1,path.end()}),facet));return;
    }
    if(c.kind==Cell::Bound || c.kind==Cell::Input) {
        if(c.annotation==none && c.target!=none) {follow(c.target);return;}
        if(c.annotation!=none) {
            const auto& spec=graph.nodes[c.annotation];
            if(spec.text=="(tuple)") {
                for(std::size_t i=0;i<spec.children.size();++i) if(path[0]=="__"+std::to_string(i)) {
                    Id initial=c.target==none ? none : projection(c.target,{path[0]});
                    Id field=bound(c.site,c.scope,spec.children[i],initial);
                    equation(result,demand(projection(field,{path.begin()+1,path.end()}),facet));return;
                }
                conflict();return;
            }
            if(spec.body!=none && spec.children.empty()) {
                auto select=[this,owner,path,result,facet](const auto&){
                    const auto& cell=cells[owner];
                    if(openAliasComponents.contains(owner)) pending("type component has an unclosed query relationship",cell.site);
                    if(equationGroups.contains(owner)) {
                        equationProperty(owner,path);
                        if(facet!=Facet::Type) typeProperty(demand(projection(owner,path),Facet::Type),facet,result);
                        later({result},[this,owner,path,result,facet](const auto&){
                            const auto& binding=cells[owner];
                            equation(result,demand(projection(expression(graph.nodes[binding.annotation].body,binding.scope),path),facet));
                        });return;
                    }
                    if(auto component=aliasComponents.find(owner);component!=aliasComponents.end()) {
                        Ref joined;auto nominalType=demand(owner,Facet::Identity);
                        for(Id member:component->second) if(cells[member].target!=none) {
                            Id supplied=cells[member].target;
                            equation(nominalType,demand(supplied,Facet::Identity));
                            auto projected=demand(projection(supplied,path),facet);
                            if(!joined) joined=projected;
                            else if(facet==Facet::Category || facet==Facet::Length || facet==Facet::Identity || facet==Facet::Shape) equation(joined,projected);
                            else {auto next=fresh("common projected type");commonType(joined,projected,next);joined=next;}
                        }
                        if(joined) equation(result,joined);
                    }
                    Id query=expression(graph.nodes[cell.annotation].body,cell.scope);
                    equation(result,demand(projection(query,path),facet));
                };
                if(global(owner) || c.field) later({aliasesReady(c.scope)},select);
                else select(std::vector<Ref>{});
                return;
            }
            if(spec.body==none && spec.children.empty() && record(c.scope,spec.text).site!=none) {
                if(c.target==none) follow(recordPrototype(record(c.scope,spec.text),c.annotation));
                else {equation(demand(owner,Facet::Identity),demand(c.target,Facet::Identity));follow(c.target);}
                return;
            }
        }
    }
    if(c.kind==Cell::Dispatch) {
        later({demand(owner,Facet::Target)},[this,owner,path,result,facet](const auto& args){
            Definition def{args[0]->arguments[0]->number.template convert_to<Id>(),args[0]->arguments[1]->number.template convert_to<Id>()};
            auto key=std::make_pair(owner,def.site);Id target;
            if(auto found=selectedCalls.find(key);found!=selectedCalls.end()) target=found->second;
            else {target=instantiate(def,cells[owner].items,cells[owner].site,cells[owner].scope);selectedCalls.emplace(key,target);}
            equation(result,demand(projection(target,path),facet));
        });return;
    }
    Ref type=observedType(owner);
    for(const auto& member:path) {
        auto next=fresh("projected member type");memberValue(type,{},member,next,{});type=next;
    }
    typeProperty(type,facet,result);
}
bool Session::changesProjectionState(Id id) {
    std::set<std::pair<Id,Id>> seen;
    std::function<bool(Id,Id)> visit=[&](Id site,Id scope) {
        if(site==none || !seen.emplace(site,scope).second) return false;
        const auto& n=graph.nodes[site];
        if(n.syntax==Syntax::Type || n.syntax==Syntax::RecordDecl || n.syntax==Syntax::Function || n.syntax==Syntax::Probe || n.syntax==Syntax::MethodProbe) return false;
        if((n.syntax==Syntax::Assign || n.syntax==Syntax::Destructure) || n.syntax==Syntax::Return) return true;
        if(n.syntax==Syntax::Name && n.text.starts_with("$")) return true;
        if(n.syntax==Syntax::Call) {
            auto definitions=functions(scope,n.text);
            if(definitions.empty()) return true;
            for(const auto& def:definitions) if(!graph.nodes[def.site].external && visit(graph.nodes[def.site].body,def.scope)) return true;
        }
        for(Id child:n.children) if(visit(child,scope)) return true;
        return n.body!=none && visit(n.body,scope);
    };
    return visit(cells[id].site,cells[id].scope);
}
Id Session::frozenScope(Id lexical,Id at) {
    Id frozen=environment(lexical);environments[frozen].queryState=at;std::set<std::string> names;
    for(Id env=lexical;env!=none;env=environments[env].parent)
        for(const auto& [name,cell]:environments[env].values) if(names.insert(name).second) {
            auto found=states[at].values.find(location(cell));
            if(found!=states[at].values.end()) environments[frozen].values[name]=found->second;
            else if(auto type=states[at].locationTypes.find(location(cell));type!=states[at].locationTypes.end()) {
                Id value=known(type->second,{},cells[cell].site);cells[value].location=location(cell);
                environments[frozen].values[name]=value;
            }
        }
    return frozen;
}
Id Session::frozenProperty(Id id,Id at) {
    if(auto found=states[at].values.find(location(id));found!=states[at].values.end()) return found->second;
    if(auto type=states[at].locationTypes.find(location(id));type!=states[at].locationTypes.end()) {
        Id value=known(type->second,{},cells[id].site);cells[value].location=location(id);return value;
    }
    expand(id);const auto& c=cells[id];
    if(c.kind==Cell::Known || c.kind==Cell::Input || global(id)) return id;
    if(c.kind==Cell::Record) {
        Cell copy=c;copy.properties.clear();copy.items.clear();copy.definition.scope=frozenScope(c.definition.scope,at);
        copy.recordScope=environment(copy.definition.scope);
        for(std::size_t i=0;i<c.items.size();++i) {
            const auto& field=cells[c.items[i]];
            Id value=bound(field.site,copy.recordScope,field.annotation,field.target==none ? none : frozenProperty(field.target,at),true);
            environments[copy.recordScope].values[c.names[i]]=value;copy.items.push_back(value);
        }
        return addCell(std::move(copy));
    }
    if(c.kind==Cell::Bound) return bound(c.site,frozenScope(c.scope,at),c.annotation,c.target==none ? none : frozenProperty(c.target,at),c.field);
    if(c.kind==Cell::Projection) return projection(frozenProperty(c.target,at),c.names);
    if(c.kind==Cell::Alias && c.site!=none && graph.nodes[c.site].syntax==Syntax::Name) {
        if(auto found=states[at].values.find(location(c.target));found!=states[at].values.end()) return found->second;
    }
    if(c.site!=none && c.scope!=none) return expression(c.site,frozenScope(c.scope,at));
    return id;
}
void Session::identity(Id id,Ref result) {
    expand(id);const auto& c=cells[id];
    if(c.kind==Cell::Projection) {projectedProperty(c.target,c.names,result,Facet::Identity);return;}
    if(c.kind==Cell::Dispatch) {dispatch(id,Facet::Identity,result);return;}
    if(c.kind==Cell::Record) {equation(result,nominal(c));return;}
    if(c.kind==Cell::SyntaxCell && graph.nodes[c.site].syntax==Syntax::Tuple) {equation(result,terms().make("Tuple",{number(c.items.size())}));return;}
    if(c.kind==Cell::Alias || c.kind==Cell::Receiver || (c.kind==Cell::Thunk && c.annotation==none)) {equation(result,demand(c.target,Facet::Identity));return;}
    if(c.kind==Cell::Store) {equation(result,demand(c.location,Facet::Identity));return;}
    if(c.kind==Cell::Bound || c.kind==Cell::Input) {
        if(c.annotation==none && c.target!=none) {equation(result,demand(c.target,Facet::Identity));return;}
        if(c.annotation!=none) {
            const auto& spec=graph.nodes[c.annotation];
            if(spec.text=="(tuple)") {equation(result,terms().make("Tuple",{number(spec.children.size())}));return;}
            if(spec.body!=none && spec.children.empty()) {
                auto select=[this,id,result](const auto&){
                    const auto& cell=cells[id];
                    if(openAliasComponents.contains(id)) pending("type component has an unclosed query relationship",cell.site);
                    if(auto component=aliasComponents.find(id);component!=aliasComponents.end())
                        for(Id member:component->second) if(cells[member].target!=none)
                            equation(result,demand(cells[member].target,Facet::Identity));
                    equation(result,demand(expression(graph.nodes[cell.annotation].body,cell.scope),Facet::Identity));
                };
                if(global(id) || c.field) later({aliasesReady(c.scope)},select);
                else select(std::vector<Ref>{});
                return;
            }
            if(spec.body==none && spec.children.empty()) {
                auto def=record(c.scope,spec.text);
                if(def.site!=none) {Cell recordCell;recordCell.definition=def;equation(result,nominal(recordCell));return;}
            }
        }
    }
    equation(demand(id,Facet::Type),terms().make("Record",{result,fresh("record fields"),fresh("record captures")}));
}
void Session::flowProjection(Id id,Id before,const Flow& out) {
    auto owner=flow(cells[id].target,before);auto path=cells[id].names;
    later({owner.state},[this,id,owner,path,out](const auto& ends){
        continueFlow(ends[0]->number.template convert_to<Id>(),owner.code,out,[this,id,owner,path](Id at,const Flow& result){
            Ref type=demand(owner.value,Facet::Type),value=demand(owner.value,Facet::Value);
            for(const auto& member:path) {
                auto nextType=fresh("member type"),nextValue=fresh("member value");
                memberValue(type,value,member,nextType,nextValue);type=nextType;value=nextValue;
            }
            equation(cells[result.value].knownType,type);equation(cells[result.value].knownValue,value);
            equation(result.state,number(at));equation(result.code,terms().make("Done"));
        });
    });
}
