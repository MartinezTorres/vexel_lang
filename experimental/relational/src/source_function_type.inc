// A successful abstract execution proves an interface for every native input
// value. It contributes no source value and runs no emitted boundary code.
// Failure of this sufficient proof says nothing about a concrete invocation:
// a later constant condition may select a branch with a different interface.
bool Session::isolatedFunctionInterface(Id id) {
    const auto& cell=cells[id];const auto& fn=graph.nodes[cell.definition.site];
    if(fn.receiverCount || std::any_of(fn.children.begin(),fn.children.end(),[this](Id param){return graph.nodes[param].expression;})) return false;
    // Automatic captures need the caller's reaching-state interface. A
    // prototype rooted at state zero cannot silently substitute their old
    // initializers. This boundary can be widened once captures are explicit.
    if(!captures(cell.definition.scope,cell.definition.site).empty()) return false;
    const auto arguments=cell.flowWrapper ? cell.prefix : cell.items;
    for(Id argument:arguments) {
        Id actual=cells[argument].kind==Cell::Bound ? cells[argument].target : argument;
        if(actual==none || cells[actual].site==none) continue;
        std::set<Id> calls;
        if(!effectSignature(cells[actual].site,cells[actual].scope,calls).second) return false;
    }
    return true;
}
Ref Session::functionType(Id id) {
    if(auto found=functionTypes.find(id);found!=functionTypes.end()) return found->second;
    auto result=fresh("value-independent function interface");functionTypes.emplace(id,result);
    const auto& cell=cells[id];
    auto fallback=[&] {
        if(cell.kind==Cell::Invocation) invocation(id,Facet::Type,result,false);
        else equation(result,observedType(id,true));
    };
    // Preserve existing partial relationships before asking for all argument
    // types. A direct field projection must not close an unrelated sibling.
    Id direct=id;
    if(cell.kind==Cell::Invocation) direct=instantiate(cell.definition,cell.items,cell.site,cell.scope,true,{},true);
    bool execution=false;(void)observedType(direct,true,&execution);
    if(!execution) {equation(result,observedType(direct,true));return result;}
    if(!isolatedFunctionInterface(id)) {fallback();return result;}
    const auto arguments=cell.flowWrapper ? cell.prefix : cell.items;
    if(arguments.empty()) {fallback();return result;}
    std::vector<Ref> types;for(Id argument:arguments) types.push_back(demand(argument,Facet::Type));
    later(types,[this,id,result](const auto& facts){closeFunctionType(id,result,facts);});return result;
}
void Session::closeFunctionType(Id id,Ref result,const std::vector<Ref>& types,const std::map<Property,Ref>& inputs) {
    auto fallback=[&] {
        if(cells[id].kind==Cell::Invocation) invocation(id,Facet::Type,result,false);
        else equation(result,observedType(id,true));
    };
    for(const auto& type:types) for(const auto& leaf:leaves(type)) if(!nativeScalar(leaf.type)) {fallback();return;}
    SupportScope detachedSupport(supportInputs,nullptr),detachedEffects(effectInputs,nullptr),detachedRuntime(runtimeInputs,nullptr);
    const Id boundary=cells.size();Request proof;Ref answer;
    proof.goals=constructed([&]{
        const auto original=cells[id];std::vector<Id> arguments;
        for(std::size_t i=0;i<types.size();++i) {
            auto value=fresh("universal native argument");runtime(types[i],terms().make("TypeProbeInput",{number(id),number(i),number(0)}),value);
            arguments.push_back(known(types[i],value,original.site));
        }
        Id body=instantiate(original.definition,arguments,original.site,original.scope,true,{},true);
        answer=demand(flow(body,0).value,Facet::Type);
    });
    proof.observation={answer};
    proof.overrideBundle=[this,boundary,inputs](const std::string& name)->std::optional<std::vector<Goal>> {
        if(auto barrier=abstractProofBarrier(name)) return barrier;
        const std::string prefix="source_property_";if(!name.starts_with(prefix)) return {};
        auto split=name.find('_',prefix.size());if(split==std::string::npos) return {};
        Id cell=std::stoul(name.substr(prefix.size(),split-prefix.size()));auto facet=static_cast<Facet>(std::stoul(name.substr(split+1)));
        if(cell<boundary && cells[cell].kind==Cell::Known && (facet==Facet::Type || facet==Facet::Value)) {
            auto original=facet==Facet::Type ? cells[cell].knownType : cells[cell].knownValue;
            auto found=inputs.find({cell,facet});
            if(!original && found==inputs.end()) return std::vector<Goal>{unknown("function interface has no supplied value")};
            if(found!=inputs.end() || (original && ground(original))) return std::vector<Goal>{
                equal(cells[cell].properties.at(facet),found!=inputs.end() ? found->second : original),equal(supports.at({cell,facet}),terms().make("Edges"))};
            // Do not let the private execution solve a parent's unsupplied
            // promise by guessing. Retry only with evidence from that parent.
            return std::vector<Goal>{awaitInput(fresh("opaque signature input"),name)};
        }
        if(facet==Facet::Type && (cells[cell].flowWrapper || cells[cell].kind==Cell::Invocation) &&
           graph.nodes[cells[cell].definition.site].annotation==none) {
            return constructed([&]{
                if(cells[cell].kind==Cell::Invocation) invocation(cell,facet,cells[cell].properties.at(facet),false);
                else equation(cells[cell].properties.at(facet),observedType(cell,true));
                equation(supports.at({cell,facet}),terms().make("Edges"));
            });
        }
        return {};
    };
    auto search=options.search;search.budget=searchBudget;auto checked=Machine(program,search).solve(proof);
    if(checked.outcome==Outcome::Exhausted) throw ConstructionLimit("function interface proof exhausted its search budget");
    if(checked.outcome==Outcome::Resolved) {
        auto type=checked.selected->observation[0];
        std::function<bool(const Ref&)> symbolic=[&](const Ref& term) {
            if(term->name=="Runtime" || term->name=="TypeProbeInput") return true;
            return std::any_of(term->arguments.begin(),term->arguments.end(),symbolic);
        };
        if(!symbolic(type)) {equation(result,type);return;}
    }
    if(checked.outcome==Outcome::Unresolved && !checked.inputs.empty()) {
        std::vector<Property> keys;std::vector<Ref> values;
        for(const auto& name:checked.inputs) {
            const std::string prefix="source_property_";if(!name.starts_with(prefix)) continue;
            auto split=name.find('_',prefix.size());if(split==std::string::npos) continue;
            Property key{std::stoul(name.substr(prefix.size(),split-prefix.size())),static_cast<Facet>(std::stoul(name.substr(split+1)))};
            if(inputs.contains(key) || cells[key.first].kind!=Cell::Known) continue;
            keys.push_back(key);values.push_back(demand(key.first,key.second));
        }
        if(!keys.empty()) {
            later(values,[this,id,result,types,inputs,keys](const auto& facts){
                auto evidence=inputs;for(std::size_t i=0;i<keys.size();++i) evidence[keys[i]]=facts[i];
                closeFunctionType(id,result,types,evidence);
            });return;
        }
    }
    fallback();
}
