// Common-type closure is a source relation. Opaque proof contexts establish
// which interfaces are equal without guessing a concrete representation.
Ref Session::aliasesReady(Id scope) {
    auto setup="type_alias_setup_"+std::to_string(scope);
    if(!aliasReadiness.contains(scope)) {
        auto ready=fresh("type relationship closure");aliasReadiness.emplace(scope,ready);
        auto body=constructed([&]{
            later({},[this,scope,ready](const auto&){prepareAliases(scope);equation(ready,number(1));});
        });program.bundles.emplace(setup,std::move(body));
    }
    request.goals.push_back(require(setup));
    return aliasReadiness.at(scope);
}
void Session::aliasProperty(Id id,Facet facet,Ref result) {
    later({aliasesReady(cells[id].scope)},[this,id,facet,result](const auto&){
        if(openAliasComponents.contains(id)) pending("type component has an unclosed query relationship",cells[id].site);
        if(facet==Facet::Type) {
            if(equationGroups.contains(id)) {equationProperty(id,{});return;}
            if(auto joined=aliasBundles.find(id);joined!=aliasBundles.end()) request.goals.push_back(require(joined->second));
            return;
        }
        if(equationGroups.contains(id)) {
            if(facet==Facet::Shape) {typeProperty(demand(id,Facet::Type),facet,result);return;}
            if(facet==Facet::Category) {typeCategory(demand(id,Facet::Type),result);return;}
            auto element=fresh("equation element"),length=fresh("equation length");
            equation(demand(id,Facet::Type),terms().make("Array",{element,length}));
            equation(result,facet==Facet::Length ? length : element);return;
        }
        if(auto component=aliasComponents.find(id);component!=aliasComponents.end()) {
            Ref joined;
            for(Id owner:component->second) if(cells[owner].target!=none) {
                auto supplied=demand(cells[owner].target,facet);
                if(facet==Facet::Length || facet==Facet::Category || facet==Facet::Shape) equation(result,supplied);
                else if(!joined) joined=supplied;
                else {auto next=fresh("common element property");commonType(joined,supplied,next);joined=next;}
            }
            if(joined) equation(result,joined);
        }
        const auto& cell=cells[id];equation(result,demand(expression(graph.nodes[cell.annotation].body,cell.scope),facet));
    });
}
void Session::commonType(Ref a,Ref b,Ref result) {
    later({a,b},[this,result](const auto& types){
        const auto& left=types[0];const auto& right=types[1];
        if(identical(left,right)) {equation(result,left);return;}
        if(left->name=="Array" && right->name=="Array") {
            equation(left->arguments[1],right->arguments[1]);auto element=fresh("common array element");
            commonType(left->arguments[0],right->arguments[0],element);
            equation(result,terms().make("Array",{element,left->arguments[1]}));return;
        }
        if(left->name=="Record" && right->name=="Record") {
            equation(left->arguments[0],right->arguments[0]);equation(left->arguments[2],right->arguments[2]);
            const auto& aFields=left->arguments[1]->arguments;const auto& bFields=right->arguments[1]->arguments;
            if(aFields.size()!=bFields.size()) {conflict();return;}
            std::vector<Ref> fields;
            for(std::size_t i=0;i<aFields.size();++i) {
                if(aFields[i]->name!=bFields[i]->name) {conflict();return;}
                auto field=fresh("common record field");commonType(aFields[i]->arguments[0],bFields[i]->arguments[0],field);
                fields.push_back(terms().make(aFields[i]->name,{field}));
            }
            equation(result,terms().make("Record",{left->arguments[0],terms().make("Fields",fields),left->arguments[2]}));return;
        }
        request.goals.push_back(call("promote",{left,right,result}));
    });
}
void Session::prepareAliases(Id scope) {
    if(!preparedAliasGroups.insert(scope).second) return;
    std::vector<Id> owners;bool queried=false;
    for(const auto& [name,id]:environments[scope].values) {
        (void)name;const auto& c=cells[id];
        if(c.kind!=Cell::Bound || c.scope!=scope || (!global(id) && !c.field)) continue;
        owners.push_back(id);
        if(c.annotation!=none && graph.nodes[c.annotation].body!=none) queried=true;
    }
    if(!queried) return;
    SupportScope detachedSupport(supportInputs,nullptr),detachedEffects(effectInputs,nullptr),detachedRuntime(runtimeInputs,nullptr);
    Request assumptions;
    std::set<Id> opaqueOwners;
    // Construct reusable obligations in isolation. Merely preparing a proof
    // must not demand every declaration in the main semantic request.
    (void)constructed([&]{
        for(Id owner:owners) {
            const auto& c=cells[owner];
            if(c.annotation==none || graph.nodes[c.annotation].body==none) continue;
            opaqueOwners.insert(owner);
            auto type=demand(owner,Facet::Type);
            auto bundle="source_property_"+std::to_string(owner)+"_"+std::to_string(static_cast<unsigned>(Facet::Type));
            assumptions.overrides[bundle]={equal(type,terms().make("OpaqueType",{number(owner)})),equal(supports.at({owner,Facet::Type}),terms().make("Edges"))};
        }
    });
    // Delayed builders may discover new projections during a proof. Keep the
    // same opaque boundary for those properties; reading a witness here would
    // incorrectly turn one example's field type into a universal equality.
    assumptions.overrideBundle=[this,opaqueOwners](const std::string& name)->std::optional<std::vector<Goal>> {
        const std::string prefix="source_property_";
        if(!name.starts_with(prefix)) return {};
        auto split=name.find('_',prefix.size());if(split==std::string::npos) return {};
        Id id=std::stoul(name.substr(prefix.size(),split-prefix.size()));
        auto facet=static_cast<Facet>(std::stoul(name.substr(split+1)));
        if(facet!=Facet::Type && facet!=Facet::Element && facet!=Facet::Length && facet!=Facet::Identity && facet!=Facet::Category && facet!=Facet::Shape) return {};
        Id owner=id;std::vector<std::string> path;std::set<Id> seen;
        while(seen.insert(owner).second && !opaqueOwners.contains(owner)) {
            expand(owner);const auto& cell=cells[owner];
            if(cell.kind==Cell::Projection) {auto joined=cell.names;joined.insert(joined.end(),path.begin(),path.end());path=std::move(joined);owner=cell.target;}
            else if(cell.kind==Cell::Alias || ((cell.kind==Cell::Bound || cell.kind==Cell::Thunk) && cell.annotation==none && cell.target!=none)) owner=cell.target;
            else if(cell.kind==Cell::Store) owner=cell.location;
            else break;
        }
        if(!opaqueOwners.contains(owner)) return {};
        std::vector<Ref> members;for(const auto& member:path) members.push_back(terms().make(member));
        auto opaque=path.empty() && facet==Facet::Type ? terms().make("OpaqueType",{number(owner)}) :
            terms().make("OpaqueProperty",{number(owner),number(static_cast<unsigned>(facet)),terms().make("Path",members)});
        return std::vector<Goal>{equal(cells[id].properties.at(facet),opaque),equal(supports.at({id,facet}),terms().make("Edges"))};
    };
    std::map<Id,Id> parent,aliases;
    std::map<Id,std::set<Id>> unfinished,relationships;
    for(Id owner:owners) parent[owner]=owner;
    auto references=[&](Id site,Id lexical) {
        std::set<Id> found,visitedFunctions;
        std::function<void(Id,Id,std::set<std::string>)> visit=[&](Id at,Id env,std::set<std::string> boundNames) {
            if(at==none) return;
            const auto& n=graph.nodes[at];
            if(n.syntax==Syntax::Name && !boundNames.contains(n.text)) {
                Id target=lookup(env,n.text);if(parent.contains(target)) found.insert(target);
            }
            if(n.syntax==Syntax::Function || n.syntax==Syntax::RecordDecl) return;
            if(n.syntax==Syntax::Block) for(Id child:n.children) if(graph.nodes[child].syntax==Syntax::Declare) boundNames.insert(graph.nodes[child].text);
            for(Id child:n.children) visit(child,env,boundNames);
            if(n.body!=none) visit(n.body,env,boundNames);
            if(n.annotation!=none) visit(n.annotation,env,boundNames);
            if(n.syntax==Syntax::Call) for(const auto& definition:functions(env,n.text)) if(visitedFunctions.insert(definition.site).second) {
                const auto& fn=graph.nodes[definition.site];std::set<std::string> parameters;
                for(Id parameter:fn.children) parameters.insert(graph.nodes[parameter].text);
                visit(fn.body,definition.scope,parameters);visit(fn.annotation,definition.scope,parameters);
            }
        };
        visit(site,lexical,{});return found;
    };
    std::function<Id(Id)> root=[&](Id id){return parent.at(id)==id ? id : parent[id]=root(parent.at(id));};
    for(Id owner:owners) {
        const auto& c=cells[owner];if(c.annotation==none || graph.nodes[c.annotation].body==none) continue;
        Request proof=assumptions;Ref answer;
        proof.goals=constructed([&]{answer=annotation(c.annotation,c.scope);});proof.observation={answer};
        auto search=options.search;search.budget=searchBudget;
        auto result=Machine(program,search).solve(proof);
        if(result.outcome==Outcome::Exhausted) throw ConstructionLimit("type equality proof exhausted its search budget");
        if(result.outcome!=Outcome::Resolved) {
            unfinished[owner]=references(graph.nodes[c.annotation].body,c.scope);
            relationships[owner]=unfinished[owner];continue;
        }
        const auto& type=result.selected->observation[0];
        std::function<void(const Ref&)> opaqueReferences=[&](const Ref& term){
            if(term->name.starts_with("Opaque") && !term->arguments.empty() && term->arguments[0]->kind==Term::Number)
                relationships[owner].insert(term->arguments[0]->number.template convert_to<Id>());
            for(const auto& child:term->arguments) opaqueReferences(child);
        };opaqueReferences(type);
        if(type->name!="OpaqueType") continue;
        Id other=type->arguments[0]->number.convert_to<Id>();if(!parent.contains(other)) continue;
        aliases[owner]=other;parent[root(owner)]=root(other);
    }
    std::map<Id,std::vector<Id>> components;
    for(Id owner:owners) components[root(owner)].push_back(owner);
    for(const auto& [representative,component]:components) {
        // Concrete and unannotated declarations anchor a component. Ordinary
        // equations already propagate their restriction; witnesses may not
        // widen an anchor. Every unanchored member must have a proved alias.
        if(std::any_of(component.begin(),component.end(),[&](Id id){return !aliases.contains(id);})) continue;
        bool open=false;
        for(const auto& [owner,possible]:unfinished) {
            (void)owner;
            for(Id dependency:possible) if(root(dependency)==representative) open=true;
        }
        auto name="type_alias_component_"+std::to_string(representative);
        auto body=constructed([&]{
            if(open) pending("type component has an unclosed query relationship",cells[component[0]].site);
            Ref joined;
            for(Id owner:component) if(cells[owner].target!=none) {
                auto supplied=observedType(cells[owner].target);
                if(!joined) joined=supplied;
                else {auto next=fresh("common alias interface");commonType(joined,supplied,next);joined=next;}
            }
            if(joined) for(Id owner:component) equation(demand(owner,Facet::Type),joined);
        });
        program.bundles.emplace(name,std::move(body));
        for(Id owner:component) {
            aliasBundles[owner]=name;
            aliasComponents[owner]=component;
            if(open) openAliasComponents.insert(owner);
        }
    }
    // Unproved transformations belong to one revisable candidate vector with
    // the cyclic relationships they can affect. Acyclic anchored requirements
    // continue through their ordinary equations without witness widening.
    std::map<Id,std::set<Id>> adjacent;
    for(const auto& [owner,related]:relationships) for(Id other:related)
        if(opaqueOwners.contains(other)) {adjacent[owner].insert(other);adjacent[other].insert(owner);}
    std::set<Id> visited;
    for(Id owner:opaqueOwners) {
        if(!visited.insert(owner).second) continue;
        std::vector<Id> group{owner};
        for(std::size_t at=0;at<group.size();++at) for(Id other:adjacent[group[at]]) if(visited.insert(other).second) group.push_back(other);
        bool transforming=std::any_of(group.begin(),group.end(),[&](Id member){return !aliases.contains(member);});
        if(!transforming) continue;
        std::set<Id> done,active;bool cyclic=false;
        std::function<void(Id)> cycle=[&](Id member){
            if(active.contains(member)) {cyclic=true;return;}
            if(!done.insert(member).second) return;
            active.insert(member);
            for(Id next:relationships[member]) if(opaqueOwners.contains(next)) cycle(next);
            active.erase(member);
        };
        for(Id member:group) cycle(member);
        if(!cyclic) continue;
        std::sort(group.begin(),group.end());
        for(Id member:group) {
            equationGroups[member]=group;aliasBundles.erase(member);aliasComponents.erase(member);openAliasComponents.erase(member);
        }
    }
}
