// Persistent locations are named graph resources. No source address or host
// value is used to classify a declaration. The initializer's effect promise
// distinguishes an equation from a lazy runtime initializer.
struct BoundaryBinding {std::string space;unsigned address=0;};
BoundaryBinding msxBinding(const Node& declaration,const Ref& type,const Ref& addressValue={}) {
    BoundaryBinding binding;
    if(declaration.key!="backend-binding") return binding;
    for(const auto& tag:declaration.attributes) {
        bool port=tag.name=="msx_port",memory=tag.name=="msx_address";
        if(!port && !memory) continue;
        if(!binding.space.empty()) throw Error(declaration.span,"one address-space binding is required");
        if(tag.arguments.size()!=1 || !addressValue || addressValue->kind!=Term::Number ||
           addressValue->number<0 || addressValue->number>(port ? 255 : 65535))
            throw Error(declaration.span,"invalid MSX binding address");
        binding.space=port ? "port" : "address";binding.address=addressValue->number.convert_to<unsigned>();
    }
    if(binding.space.empty()) throw Error(declaration.span,"MSX binding requires msx_address or msx_port metadata");
    if(binding.space=="port" && !(type->name=="UInt" && type->arguments[0]->number==8)) throw Error(declaration.span,"MSX port needs #u8");
    unsigned size=0;
    for(const auto& leaf:leaves(type)) {
        if(!nativeScalar(leaf.type)) throw Error(declaration.span,"MSX binding needs concrete native scalar leaves");
        size+=nativeBytes(leaf.type);
    }
    if(binding.address+size>65536u) throw Error(declaration.span,"MSX binding crosses the address space");
    return binding;
}
std::pair<bool,bool> Session::effectSignature(Id site,Id scope,std::set<Id>& calls) {
    // A conservative proof of (required boundary effect, no caller transfer).
    // This projection uses signatures, not reaching values. It can expose an
    // unconditional sink before an earlier global read has closed. The full
    // execution obligations still validate every expression and effect.
    if(site==none) return {false,true};
    const auto& n=graph.nodes[site];
    if(n.syntax==Syntax::Function || n.syntax==Syntax::RecordDecl || n.syntax==Syntax::Type || n.syntax==Syntax::Probe || n.syntax==Syntax::MethodProbe) return {false,true};
    if(n.syntax==Syntax::Name) return {false,!n.text.starts_with("$")};
    // This is a shape query only for an array operand. Scalar absolute value
    // executes its operand and can transfer control; syntax alone cannot
    // establish either category for a general expression.
    if(n.syntax==Syntax::Unary && n.text=="|") return {false,false};
    if(n.syntax==Syntax::Loop || n.syntax==Syntax::Iterate || n.syntax==Syntax::Repeat) return {false,false};
    if(n.syntax==Syntax::Select || (n.syntax==Syntax::Binary && (n.text=="&&" || n.text=="||"))) {
        auto condition=effectSignature(n.children[0],scope,calls);
        if(!condition.second || condition.first) return condition;
        auto left=effectSignature(n.children[1],scope,calls);
        auto right=n.children.size()==3 ? effectSignature(n.children[2],scope,calls) : std::pair<bool,bool>{false,true};
        return {left.first && right.first,left.second && right.second};
    }
    if(n.syntax==Syntax::Call) {
        auto definitions=functions(scope,n.text);
        if(definitions.size()!=1) return {false,false};
        const auto& fn=graph.nodes[definitions[0].site];
        for(Id parameter:fn.children) if(graph.nodes[parameter].expression) return {false,false};
        for(Id argument:n.children) {auto part=effectSignature(argument,scope,calls);if(part.first || !part.second) return part;}
        if(fn.key=="bundled-math") return {false,true};
        if(fn.external) return {true,true};
        if(!calls.insert(definitions[0].site).second) return {false,true};
        auto body=effectSignature(fn.body,definitions[0].scope,calls);calls.erase(definitions[0].site);
        return {body.first,true}; // The callee consumes its own returns.
    }
    bool required=false,normal=true;
    for(Id child:n.children) {
        auto part=effectSignature(child,scope,calls);required=required || part.first;
        if(!part.second) {normal=false;break;}
    }
    if(n.syntax==Syntax::Return) normal=false;
    return {required,normal};
}
void Session::storage(Id id,Ref result) {
    if(!global(id)) {conflict();return;}
    const auto& cell=cells[id];const auto& declaration=graph.nodes[cell.site];
    mapperStorage(id,result);
    SupportScope detached(effectInputs,nullptr),detachedRuntime(runtimeInputs,nullptr);
    if(declaration.key=="backend-binding") {
        auto address=fresh("MSX address-space binding");bool found=false;
        for(const auto& tag:declaration.attributes) if(tag.name=="msx_port" || tag.name=="msx_address") {
            if(tag.arguments.size()!=1) throw Error(tag.span,"MSX binding needs one address expression");
            equation(address,observedValue(expression(tag.arguments[0],cell.scope)));found=true;
        }
        if(!found) {conflict();return;}
        request.goals.push_back(witness("binding_"+std::to_string(id),address));
        later({demand(id,Facet::Type),address},[this,id](const auto& values){
            try {(void)msxBinding(graph.nodes[cells[id].site],values[0],values[1]);}
            catch(const std::exception&) {conflict();}
        });
    }
    if(declaration.external || cell.target==none) {
        equation(result,number(1));
        request.goals.push_back(witness("storage_"+std::to_string(id),terms().make("Storage",{
            number(id),terms().make(declaration.text),demand(id,Facet::Type),
            terms().make(declaration.external ? "External" : "Uninitialized"),terms().make("Done"),terms().make("NoValue")})));
        return;
    }
    auto init=flow(cell.target,0);initializers.emplace(id,init);request.goals.push_back(booleanOr(init.effect,init.runtime,result));
    std::set<Id> calls;
    if(effectSignature(cells[cell.target].site,cell.scope,calls).first) equation(result,number(1));
    later({result},[this,id,init](const auto& args){
        if(args[0]->number==0) return;
        auto type=demand(id,Facet::Type),value=fresh("persistent initial value");
        conversion(init.value,type,demand(init.value,Facet::Value),value);
        request.goals.push_back(witness("storage_"+std::to_string(id),terms().make("Storage",{
            number(id),terms().make(graph.nodes[cells[id].site].text),type,terms().make("Lazy"),sequence(init.code,validateSchema(type,value)),value})));
    });
}
void Session::flowGlobal(Id id,Id before,const Flow& out) {
    // Mandatory external/uninitialized accesses publish their effect before
    // waiting for any type or initializer obligation.
    if(graph.nodes[cells[id].site].external || cells[id].target==none) boundaryEffect();
    auto persistent=demand(id,Facet::Storage);
    later({persistent},[this,id,before,out](const auto& args){
        Ref type=demand(id,Facet::Type);equation(cells[out.value].knownType,type);
        if(args[0]->number==0) {
            equation(out.state,number(before));
            const auto& init=initializers.at(id);
            conversion(init.value,type,demand(init.value,Facet::Value),cells[out.value].knownValue);
            equation(out.code,init.code);return;
        }
        equation(out.state,number(state(states[before])));
        boundaryEffect();Id temporary=nextTemporary++;
        runtime(type,terms().make("CallValue",{number(temporary),number(0)}),cells[out.value].knownValue);
        auto size=fresh("persistent leaf count");
        later({type},[this,size](const auto& types){equation(size,number(std::max(std::size_t(1),leaves(types[0]).size())));});
        equation(out.code,sequence(terms().make("Load",{number(temporary),number(id),size}),validateSchema(type,cells[out.value].knownValue)));
    });
}
void Session::flowGlobalStore(Id slot,Id value,Id before,const Flow& out,bool statement,Ref mask) {
    auto persistent=demand(slot,Facet::Storage);
    later({persistent},[this,slot,value,before,out,statement,mask](const auto& args){
        if(args[0]->number==0) {conflict();return;}
        boundaryEffect();auto type=demand(slot,Facet::Type),converted=fresh("persistent stored value");
        conversion(value,type,demand(value,Facet::Value),converted);
        equation(cells[out.value].knownType,statement ? terms().make("NoValue") : type);
        equation(cells[out.value].knownValue,statement ? terms().make("NoValue") : converted);
        equation(out.state,number(state(states[before])));
        std::vector<Ref> trace{number(slot),type,converted};if(mask) trace.push_back(mask);
        equation(out.code,sequence(validateSchema(type,converted),terms().make("Store",trace)));
    });
}
Ref Session::placeMask(const Ref& type,const std::vector<Ref>& path) {
    auto result=fresh("designated storage leaves");
    later({type,terms().make("Path",path)},[this,result](const auto& args){
        std::vector<Ref> masks;const auto& steps=args[1]->arguments;
        std::function<void(Ref,std::size_t,Ref)> visit=[&](Ref t,std::size_t depth,Ref selected) {
            if(depth==steps.size()) {for(const auto& leaf:leaves(t)) {(void)leaf;masks.push_back(selected);}return;}
            const auto& step=steps[depth];
            if(t->name=="Record" && step->name=="Field") {
                for(const auto& field:t->arguments[1]->arguments) {
                    if(field->name==step->arguments[0]->name) visit(field->arguments[0],depth+1,selected);
                    else for(const auto& leaf:leaves(field->arguments[0])) {(void)leaf;masks.push_back(number(0));}
                }
            } else if(t->name=="Array" && step->name=="Subscript") {
                for(std::size_t i=0;i<arrayCount(t);++i) {
                    Ref index=step->arguments[0],match;
                    if(index->kind==Term::Number) match=number(index->number==i ? 1 : 0);
                    else match=terms().make("Runtime",{terms().make("Compare",{terms().make("=="),index,number(i)}),number(0),number(1)});
                    visit(t->arguments[0],depth+1,booleanAnd(selected,match));
                }
            } else conflict();
        };
        visit(args[0],0,number(1));equation(result,terms().make("Mask",masks));
    });return result;
}
void Session::flowGlobalAssign(Id id,Id slot,const std::vector<Ref>& path,Id before,const Flow& out) {
    const auto& syntax=graph.nodes[cells[id].site];
    auto type=demand(slot,Facet::Type),partType=fresh("persistent place type"),checks=fresh("persistent bounds");
    placeType(type,path,partType,checks);
    auto evaluate=[this,id,slot,path,type,partType,checks](Id start,Id previous,Ref earlier,const Flow& destination){
        auto rhs=flow(expression(graph.nodes[cells[id].site].children[1],cells[id].scope),start);
        later({rhs.state},[this,id,slot,path,type,partType,checks,destination,previous,earlier,rhs](const auto& ends){
            continueFlow(ends[0]->number.template convert_to<Id>(),sequence(earlier,sequence(checks,rhs.code)),destination,[this,id,slot,path,type,partType,previous,rhs](Id end,const Flow& stored){
                auto commit=[this,id,slot,path,type,partType,previous,rhs,stored](Id at,Ref current,Ref prefix){
                    Id replacement=rhs.value;
                    if(previous!=none) {
                        Ref oldType=type,old=demand(previous,Facet::Value);
                        for(const auto& step:path) {
                            auto sub=fresh("compound persistent type"),part=fresh("compound persistent value");
                            if(step->name=="Field") memberValue(oldType,old,step->arguments[0]->name,sub,part);
                            else {equation(oldType,terms().make("Array",{sub,fresh("persistent array length")}));indexValue(oldType,old,step->arguments[0],part);}
                            oldType=sub;old=part;
                        }
                        Cell operation;operation.kind=Cell::Arithmetic;operation.flowPure=true;operation.expanded=true;operation.site=cells[id].site;
                        operation.items={known(oldType,old),rhs.value};const auto& spelling=graph.nodes[cells[id].site].text;
                        operation.inputName=spelling.substr(0,spelling.size()-1);replacement=addCell(std::move(operation));
                    }
                    auto updated=fresh("persistent updated container"),assigned=fresh("persistent assigned part");
                    replaceValue(type,current,path,replacement,updated,assigned);
                    Flow written{known(fresh("store type"),fresh("store value")),fresh("store state"),fresh("store code")};
                    flowGlobalStore(slot,known(type,updated),at,written,false,placeMask(type,path));
                    equation(cells[stored.value].knownType,partType);equation(cells[stored.value].knownValue,assigned);
                    equation(stored.state,written.state);equation(stored.code,sequence(arithmeticCompletion(replacement),sequence(prefix,written.code)));
                };
                if(path.empty()) commit(end,fresh("unread previous storage"),terms().make("Done"));
                else {
                    auto current=flow(slot,end);
                    later({current.state},[this,current,commit](const auto& loaded){commit(loaded[0]->number.template convert_to<Id>(),demand(current.value,Facet::Value),current.code);});
                }
            });
        });
    };
    if(syntax.text=="=") evaluate(before,none,terms().make("Done"),out);
    else {
        auto previous=flow(slot,before);
        later({previous.state},[this,id,previous,path,checks,out,evaluate](const auto& loaded){
            compoundFlow(graph.nodes[cells[id].site].text,previous.value,path,loaded[0]->number.template convert_to<Id>(),sequence(previous.code,checks),out,[this,previous,evaluate](Id at,const Flow& destination){
                evaluate(at,previous.value,terms().make("Done"),destination);
            });
        });
    }
}
void Session::flowExternal(Definition definition,const std::vector<Id>& arguments,Id before,const Flow& out) {
    boundaryEffect();const auto& fn=graph.nodes[definition.site];
    Id scope=environment(definition.scope);std::vector<Ref> types,values;
    for(std::size_t i=0;i<arguments.size();++i) {
        if(graph.nodes[fn.children[i]].annotation==none) {conflict();return;}
        environments[scope].values[graph.nodes[fn.children[i]].text]=arguments[i];
        types.push_back(demand(arguments[i],Facet::Type));values.push_back(demand(arguments[i],Facet::Value));
    }
    Ref result=fn.annotation==none ? terms().make("NoValue") : annotation(fn.annotation,scope);
    auto abiTypes=types;abiTypes.push_back(result);
    later(abiTypes,[this](const auto& closed){
        for(const auto& type:closed) for(const auto& leaf:leaves(type))
            if(!nativeScalar(leaf.type)) pending("external interface leaf needs a target representation: "+typeText(leaf.type));
    });
    auto symbol=terms().make(fn.text);Id temporary=nextTemporary++;
    request.goals.push_back(witness("external_"+std::to_string(definition.site),terms().make("External",{symbol,terms().make("Types",types),result})));
    equation(cells[out.value].knownType,result);
    runtime(result,terms().make("CallValue",{number(temporary),number(0)}),cells[out.value].knownValue);
    auto size=fresh("external result leaf count");
    later({result},[this,size](const auto& typesResolved){equation(size,number(std::max(std::size_t(1),leaves(typesResolved[0]).size())));});
    equation(out.state,number(state(states[before])));
    equation(out.code,sequence(terms().make("ExternalCall",{number(temporary),symbol,terms().make("Values",values),size}),validateSchema(result,cells[out.value].knownValue)));
}
Ref Session::boundaryAccesses(Ref code,std::set<Id> visitedFrames,std::set<Id> visitedStorage) {
    auto result=fresh("closed boundary access graph");
    later({code},[this,result,visitedFrames=std::move(visitedFrames),visitedStorage=std::move(visitedStorage)](const auto& traces){
        std::vector<Ref> parts;auto framesSeen=visitedFrames,storageSeen=visitedStorage;
        std::function<void(const Ref&)> visit=[&](const Ref& trace){
            if(trace->name=="ExternalCall") parts.push_back(externalAccesses(trace->arguments[1]->name));
            else if(trace->name=="Load" || trace->name=="Store") {
                Id slot=trace->arguments[trace->name=="Load" ? 1 : 0]->number.template convert_to<Id>();
                const auto& cell=cells[slot];bool external=graph.nodes[cell.site].external,lazy=cell.target!=none;
                parts.push_back(terms().make("Access",{terms().make(external ? "external-world" : "storage:"+std::to_string(slot)),number(external || lazy || trace->name=="Store" ? 1 : 0)}));
                if(!external && lazy && storageSeen.insert(slot).second) parts.push_back(boundaryAccesses(initializers.at(slot).code,framesSeen,storageSeen));
            } else if(trace->name=="Invoke") {
                Id frame=trace->arguments[1]->number.template convert_to<Id>();
                if(framesSeen.insert(frame).second) parts.push_back(boundaryAccesses(frameCodes.at(frame),framesSeen,storageSeen));
            } else if(trace->name=="Loop") {visit(trace->arguments[2]);visit(trace->arguments[5]);}
            else if(trace->name=="Sequence") for(const auto& child:trace->arguments) visit(child);
            else if(trace->name=="If") {visit(trace->arguments[1]);visit(trace->arguments[2]);}
        };
        visit(traces[0]);equation(result,terms().make("Accesses",parts));
    });return result;
}
