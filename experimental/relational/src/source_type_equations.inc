// Candidate domains are derived in isolated requests. They are proposals, not
// selected types. The owning request chooses among them while enforcing all
// original equations, witnesses and consumers in its ordinary search contexts.
void Session::equationProperty(Id owner,const std::vector<std::string>& path) {
    const auto group=equationGroups.at(owner);auto key=std::make_pair(group[0],path);
    if(!equationBundles.contains(key)) {
        auto name="type_equations_"+std::to_string(equationBundles.size());equationBundles.emplace(key,name);
        auto body=constructed([&]{later({},[this,group,path](const auto&){closeTypeEquations(group,path);});});
        program.bundles.emplace(name,std::move(body));
    }
    request.goals.push_back(require(equationBundles.at(key)));
}
void Session::closeTypeEquations(const std::vector<Id>& owners,const std::vector<std::string>& path,const std::map<Property,Ref>& inputs) {
    SupportScope detachedSupport(supportInputs,nullptr),detachedEffects(effectInputs,nullptr),detachedRuntime(runtimeInputs,nullptr);
    std::map<Id,Ref> candidates;
    std::map<Id,std::string> domains;
    std::map<Id,std::map<std::string,Ref>> supplied;
    const std::set<Id> boundary(owners.begin(),owners.end());
    auto run=[&](Request proof) {
        for(const auto& [key,value]:inputs) proof.goals.push_back(equal(key.second==Facet::Type ? cells[key.first].knownType : cells[key.first].knownValue,value));
        auto search=options.search;search.budget=searchBudget;
        auto result=Machine(program,search).solve(proof);
        if(result.outcome==Outcome::Exhausted) throw ConstructionLimit("type candidate revision exhausted its search budget");
        return result;
    };
    auto assumptions=[&,this]() {
        Request proof;
        proof.overrideBundle=[this,boundary,path,candidates,domains](const std::string& name)->std::optional<std::vector<Goal>> {
            const std::string prefix="source_property_";
            if(!name.starts_with(prefix)) return {};
            auto split=name.find('_',prefix.size());if(split==std::string::npos) return {};
            Id id=std::stoul(name.substr(prefix.size(),split-prefix.size()));
            auto facet=static_cast<Facet>(std::stoul(name.substr(split+1)));
            if(cells[id].kind==Cell::Known && (facet==Facet::Type || facet==Facet::Value)) {
                auto input=facet==Facet::Type ? cells[id].knownType : cells[id].knownValue;
                return std::vector<Goal>{equal(cells[id].properties.at(facet),input),awaitInput(input,name),equal(supports.at({id,facet}),terms().make("Edges"))};
            }
            if(facet!=Facet::Type && facet!=Facet::Element && facet!=Facet::Length && facet!=Facet::Identity && facet!=Facet::Category && facet!=Facet::Shape) return {};
            Id owner=id;std::vector<std::string> selected;std::set<Id> seen;
            while(seen.insert(owner).second && !boundary.contains(owner)) {
                expand(owner);const auto& cell=cells[owner];
                if(cell.kind==Cell::Projection) {auto joined=cell.names;joined.insert(joined.end(),selected.begin(),selected.end());selected=std::move(joined);owner=cell.target;}
                else if(cell.kind==Cell::Alias || ((cell.kind==Cell::Bound || cell.kind==Cell::Thunk) && cell.annotation==none && cell.target!=none)) owner=cell.target;
                else if(cell.kind==Cell::Store) owner=cell.location;
                else break;
            }
            if(!boundary.contains(owner)) return {};
            // A partial record proof needs its nominal family, not its schema.
            if(!path.empty() && selected.empty() && facet==Facet::Identity && cells[owner].target!=none) {
                return constructed([&]{equation(cells[id].properties.at(facet),demand(cells[owner].target,Facet::Identity));equation(supports.at({id,facet}),terms().make("Edges"));});
            }
            auto found=candidates.find(owner);
            if(found==candidates.end() || (!path.empty() && (selected!=path || (facet!=Facet::Type && facet!=Facet::Category && facet!=Facet::Shape))))
                return std::vector<Goal>{unknown("type candidate property has no supplied interface")};
            auto candidate=found->second;
            return constructed([&]{
                request.goals.push_back(call(domains.at(owner),{candidate}));
                equation(supports.at({id,facet}),terms().make("Edges"));
                auto result=cells[id].properties.at(facet);
                if(facet==Facet::Type && (path==selected || selected.empty())) {equation(result,candidate);return;}
                // Projection waits on this context's candidate, never on a
                // selected candidate from a preceding private search.
                later({candidate},[this,path,selected,facet,result](const auto& facts){
                    Ref value=facts[0];
                    if(path.empty()) for(const auto& member:selected) {
                        if(value->name!="Record") {conflict();return;}
                        const auto& fields=value->arguments[1]->arguments;
                        auto field=std::find_if(fields.begin(),fields.end(),[&](const Ref& item){return item->name==member;});
                        if(field==fields.end()) {conflict();return;}value=(*field)->arguments[0];
                    }
                    if(facet==Facet::Type) equation(result,value);
                    else if(facet==Facet::Category) typeCategory(value,result);
                    else if(facet==Facet::Shape) typeProperty(value,facet,result);
                    else if(facet==Facet::Identity && value->name=="Record") equation(result,value->arguments[0]);
                    else if((facet==Facet::Element || facet==Facet::Length) && value->name=="Array") equation(result,value->arguments[facet==Facet::Element ? 0 : 1]);
                    else conflict();
                });
            });
        };
        return proof;
    };
    auto property=[this,&path](Id id) {return path.empty() ? observedType(id) : demand(projection(id,path),Facet::Type);};
    auto prepareDomains=[&]() {
        candidates.clear();domains.clear();
        for(const auto& [owner,values]:supplied) if(!values.empty()) {
            std::vector<Ref> alternatives;for(const auto& [key,value]:values) {(void)key;alternatives.push_back(value);}
            auto name="type_candidates_"+std::to_string(program.relations.size());
            program.domain(name,alternatives);domains.emplace(owner,name);
            candidates.emplace(owner,fresh("provisional interface"));
        }
    };
    std::set<std::tuple<Id,std::string,std::string>> joinedPairs;
    for(;;) {
        prepareDomains();const auto context=assumptions();auto revised=supplied;
        std::set<std::string> needed,unfinishedReasons;bool unfinished=false;
        auto discover=[&](Id owner,const Result& result) {
            // Even an unfinished search can supply a justified proposal. It
            // cannot prove that no additional proposal exists.
            if(result.selected) {auto value=result.selected->observation[0];revised[owner].emplace(show(value),value);}
            if(result.alternative) {auto value=result.alternative->observation[0];revised[owner].emplace(show(value),value);}
            if(result.outcome==Outcome::Unresolved) {unfinished=true;unfinishedReasons.insert(result.pending.begin(),result.pending.end());}
            needed.insert(result.inputs.begin(),result.inputs.end());
        };
        for(Id owner:owners) {
            const auto& c=cells[owner];std::vector<Id> expressionsToCheck;
            if(c.target!=none) expressionsToCheck.push_back(c.target);
            expressionsToCheck.push_back(expression(graph.nodes[c.annotation].body,c.scope));
            for(Id expressionToCheck:expressionsToCheck) {
                Request proof=context;proof.proposalsOnly=true;Ref answer;
                proof.goals=constructed([&]{answer=property(expressionToCheck);});proof.observation={answer};
                // Ask specifically for a new interface. This reuses ordinary
                // constraint propagation and branch pruning, including when
                // many candidate vectors produce the same interface.
                for(const auto& [key,value]:supplied[owner]) {(void)key;proof.goals.push_back(different(answer,value));}
                discover(owner,run(std::move(proof)));
            }
            // Structural joins can combine fields from distinct witnesses.
            // Keep the operands too: a provisional join cannot erase a later
            // solution selected by a nonmonotone overload relationship.
            for(auto a=supplied[owner].begin();a!=supplied[owner].end();++a)
                for(auto b=std::next(a);b!=supplied[owner].end();++b) {
                    if(!joinedPairs.emplace(owner,a->first,b->first).second) continue;
                    Request proof;Ref answer;
                    proof.goals=constructed([&]{answer=fresh("common candidate interface");commonType(a->second,b->second,answer);});proof.observation={answer};
                    discover(owner,run(std::move(proof)));
                }
        }
        bool changed=false;
        for(const auto& [owner,values]:revised) if(values.size()!=supplied[owner].size()) changed=true;
        if(changed) {supplied=std::move(revised);continue;}
        if(!needed.empty()) {
            std::vector<Property> keys;std::vector<Ref> values;
            for(const auto& name:needed) {
                const std::string prefix="source_property_";auto split=name.find('_',prefix.size());
                Property key{std::stoul(name.substr(prefix.size(),split-prefix.size())),static_cast<Facet>(std::stoul(name.substr(split+1)))};
                if(inputs.contains(key)) continue;
                keys.push_back(key);values.push_back(key.second==Facet::Type ? cells[key.first].knownType : cells[key.first].knownValue);
            }
            if(!keys.empty()) {
                later(values,[this,owners,path,inputs,keys](const auto& facts){
                    auto evidence=inputs;for(std::size_t i=0;i<keys.size();++i) evidence[keys[i]]=facts[i];
                    closeTypeEquations(owners,path,evidence);
                });return;
            }
        }
        // A closed contradiction can be established with the same private
        // boundary before publishing domains. In particular it avoids chasing
        // ever-deeper field projections of a candidate already proved scalar.
        auto validation=context;
        validation.goals=constructed([&]{
            for(Id owner:owners) {
                if(!domains.contains(owner)) {pending("type relationship has no supplied interface");continue;}
                auto type=candidates.at(owner);request.goals.push_back(call(domains.at(owner),{type}));
                const auto& c=cells[owner];
                equation(type,property(expression(graph.nodes[c.annotation].body,c.scope)));
                if(c.target!=none) commonType(property(c.target),type,type);
            }
        });
        if(!unfinished && run(std::move(validation)).outcome==Outcome::Contradictory) {conflict();return;}
        if(unfinished) {
            pending("type candidate domain closure remains unfinished");
            for(const auto& reason:unfinishedReasons) pending(reason);
            return;
        }
        // Do not select a type inside this builder. Choices stay in the owning
        // request, so value requirements and other consumers can still reject
        // one candidate without losing another. Only demanded observations
        // determine ambiguity, exactly as for other finite source choices.
        for(Id owner:owners) {
            if(!domains.contains(owner)) {pending("type relationship has no supplied interface");continue;}
            auto type=demand(projection(owner,path),Facet::Type);
            request.goals.push_back(call(domains.at(owner),{type}));
            const auto& c=cells[owner];
            equation(type,property(expression(graph.nodes[c.annotation].body,c.scope)));
            if(c.target!=none) commonType(property(c.target),type,type);
            if(!path.empty() && c.target!=none) equation(demand(owner,Facet::Identity),demand(c.target,Facet::Identity));
        }
        return;
    }
}
