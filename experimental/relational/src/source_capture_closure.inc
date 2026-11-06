// A capture prototype preserves static facts and abstracts native storage.
// Its type and value stay lazy: merely appearing in a lexical environment does
// not require an unused overload's capture to have a type or an initializer.
void Session::capturePrototypeValue(Id id,Ref result) {
    const auto original=cells[id];auto type=demand(original.target,Facet::Type);
    later({type},[this,id,result,original](const auto& types){
        const auto& t=types[0];auto flat=leaves(t);
        auto project=[this,id,result,t,original](const Ref& actual) {
            auto fields=actual ? leaves(t,actual) : leaves(t);std::size_t at=0;
            auto value=structure(t,[&](const Ref& leaf){
                auto index=at++;auto previous=fields[index].value;
                if(!nativeScalar(leaf) || (!original.abstractNative && previous && !runtimeValue(previous))) return previous;
                auto bits=scalarBits(leaf);bool sign=scalarSigned(leaf);
                return terms().make("Runtime",{terms().make("CaptureProbeInput",{number(id),number(index)}),
                    number(sign ? -(Integer(1)<<(bits-1)) : Integer(0)),number((Integer(1)<<(bits-(sign ? 1 : 0)))-1)});
            });
            equation(result,value);
        };
        if(original.abstractNative && std::all_of(flat.begin(),flat.end(),[](const Leaf& leaf){return nativeScalar(leaf.type);})) project({});
        else later({demand(original.target,Facet::Value)},[project](const auto& values){project(values[0]);});
    });
}

void Session::selectCaptures(Definition definition,const std::vector<Id>& arguments,Id site,Id scope,Id before,
                             const std::vector<Id>& candidates,const std::function<void(const CapturePlan&)>& finish,
                             const std::map<Property,Ref>& inputs) {
    const auto& fn=graph.nodes[definition.site];auto owner=std::make_pair(definition.site,definition.scope);
    auto conservative=[&](const std::vector<Id>& slots){
        CapturePlan plan;plan.slots=slots;plan.conservative=true;
        for(Id slot:slots) {
            auto ready=states[before].initialized.find(slot);
            bool absent=ready!=states[before].initialized.end() && ready->second.proof->kind==Term::Number && ready->second.proof->number==0;
            plan.inputs.push_back(states[before].values.contains(slot) && !absent);plan.outputs.push_back(true);
        }
        return plan;
    };
    auto fallback=[&]{finish(conservative(candidates));};
    // One conservative recursive interface is shared by the group. Failure
    // to establish it is an obligation on the source, not a reason to search
    // subsets or invent modes for omitted lexical state.
    if(states[before].captureProofs.contains(owner)) {fallback();return;}
    if(candidates.empty() ||
       std::any_of(fn.children.begin(),fn.children.end(),[this](Id parameter){return graph.nodes[parameter].expression;})) {fallback();return;}
    Outcome outcome;CapturePlan selected;std::vector<Property> needed;std::vector<Ref> supplied;
    {
        SupportScope detachedSupport(supportInputs,nullptr),detachedEffects(effectInputs,nullptr),detachedRuntime(runtimeInputs,nullptr);
        const Id boundary=cells.size(),proofIdentity=nextCaptureProof++;Request proof;
        // Only the supplied interface is opaque. Previously constructed
        // canonical record/projection relationships belong to their own
        // bundles; mistaking their internal Known cells for caller inputs
        // creates dependencies on a proof attempt that has already ended.
        std::set<Id> parentInputs,visited;
        std::function<void(Id)> input=[&](Id cell) {
            if(cell==none || !visited.insert(cell).second) return;
            const auto& value=cells[cell];
            if(value.kind==Cell::Known) {parentInputs.insert(cell);return;}
            if(value.target!=none) input(value.target);
            for(Id item:value.items) input(item);
        };
        for(Id argument:arguments) input(argument);
        for(const auto& [slot,value]:states[before].values) {(void)slot;input(value);}
        for(Id slot:candidates) for(const auto& kind:{"use","input","write"}) proof.observedWitnesses.insert("capture_"+std::string(kind)+"_"+std::to_string(slot));
        proof.goals=constructed([&]{
            auto prototype=[this,site](Id target,bool native,Id root=none) {
                Cell cell;cell.kind=Cell::CapturePrototype;cell.flowPure=true;cell.expanded=true;cell.site=site;
                cell.target=target;cell.abstractNative=native;cell.location=root;return addCell(std::move(cell));
            };
            auto entry=states[before];entry.frame=none;entry.initializationLoop=none;entry.conditions.clear();
            entry.exits.clear();entry.live=number(1);entry.captureProofs.insert(owner);
            entry.captureProofIdentity=proofIdentity;
            for(Id slot:candidates) if(auto found=entry.values.find(slot);found!=entry.values.end()) {
                found->second=prototype(found->second,true,slot);
                if(!entry.initialized.contains(slot)) entry.initialized[slot]=number(1);
            }
            auto incoming=entry.values;
            std::vector<Id> params;
            for(Id argument:arguments) params.push_back(cells[argument].kind==Cell::Receiver ? argument : prototype(argument,false));
            auto body=instantiate(definition,params,site,scope,true,{},true);
            auto execution=flow(body,state(std::move(entry)));
            // Writes are also a relationship between incoming and outgoing
            // states. This includes output-only receiver roots and every
            // assignment form, independently of its expression adapter.
            later({execution.state},[this,candidates,incoming](const auto& statesAt){
                Id at=statesAt[0]->number.template convert_to<Id>();
                for(Id slot:candidates) {
                    auto old=incoming.find(slot);auto now=states[at].values.find(slot);
                    if(now!=states[at].values.end() && (old==incoming.end() || old->second!=now->second)) captureUse(slot,at,true);
                }
            });
            // A completed body must close even when its value is discarded.
            proof.realization={execution.code,execution.state,demand(execution.value,Facet::Type),demand(execution.value,Facet::Value)};
        });
        proof.overrideBundle=[this,boundary,parentInputs,inputs](const std::string& name)->std::optional<std::vector<Goal>> {
            if(auto barrier=abstractProofBarrier(name)) return barrier;
            const std::string prefix="source_property_";if(!name.starts_with(prefix)) return {};
            auto split=name.find('_',prefix.size());if(split==std::string::npos) return {};
            Id cell=std::stoul(name.substr(prefix.size(),split-prefix.size()));auto facet=static_cast<Facet>(std::stoul(name.substr(split+1)));
            if(cell>=boundary || !parentInputs.contains(cell) || cells[cell].kind!=Cell::Known || (facet!=Facet::Type && facet!=Facet::Value)) return {};
            auto original=facet==Facet::Type ? cells[cell].knownType : cells[cell].knownValue;auto found=inputs.find({cell,facet});
            if(!original && found==inputs.end()) return std::vector<Goal>{unknown("capture proof has no supplied value")};
            if(found!=inputs.end() || ground(original)) return std::vector<Goal>{
                equal(cells[cell].properties.at(facet),found!=inputs.end() ? found->second : original),equal(supports.at({cell,facet}),terms().make("Edges"))};
            return std::vector<Goal>{awaitInput(fresh("opaque capture proof input"),name)};
        };
        auto search=options.search;search.budget=searchBudget;auto checked=Machine(program,search).solve(proof);outcome=checked.outcome;
        if(captureProofReports.size()<256) {
            auto name=[this](Id slot) {
                const auto& node=graph.nodes[cells[slot].site];
                return node.syntax==Syntax::Assign && !node.children.empty() && graph.nodes[node.children[0]].syntax==Syntax::Name ?
                    graph.nodes[node.children[0]].text : node.text;
            };
            std::ostringstream report;report<<"{\"function\":"<<vx::quote(fn.text)<<",\"location\":"<<vx::quote(graph.location(fn.span))
                <<",\"mode\":\"inferred\""
                <<",\"status\":"<<vx::quote(outcomeName(outcome))<<",\"work\":"<<checked.stats.work<<",\"candidates\":[";
            bool first=true;for(Id slot:candidates) {if(!first) report<<',';first=false;report<<vx::quote(name(slot));}
            report<<"],\"selected\":[";first=true;
            if(outcome==Outcome::Resolved) for(Id slot:candidates) if(checked.selected->witnesses.contains("capture_use_"+std::to_string(slot))) {
                if(!first) report<<',';
                first=false;report<<vx::quote(name(slot));
            }
            report<<"],\"pending\":[";first=true;for(const auto& item:checked.pending) {if(!first) report<<',';first=false;report<<vx::quote(item);}
            report<<"],\"inputs\":[";first=true;for(const auto& item:checked.inputs) {if(!first) report<<',';first=false;report<<vx::quote(item);}
            report<<"]}";captureProofReports.push_back(report.str());
        }
        if(outcome==Outcome::Resolved) for(Id slot:candidates)
            if(checked.selected->witnesses.contains("capture_use_"+std::to_string(slot))) {
                selected.slots.push_back(slot);
                selected.inputs.push_back(checked.selected->witnesses.contains("capture_input_"+std::to_string(slot)));
                selected.outputs.push_back(checked.selected->witnesses.contains("capture_write_"+std::to_string(slot)));
            }
        if(outcome==Outcome::Unresolved) for(const auto& name:checked.inputs) {
            const std::string prefix="source_property_";if(!name.starts_with(prefix)) continue;
            auto split=name.find('_',prefix.size());if(split==std::string::npos) continue;
            Property key{std::stoul(name.substr(prefix.size(),split-prefix.size())),static_cast<Facet>(std::stoul(name.substr(split+1)))};
            if(inputs.contains(key) || cells[key.first].kind!=Cell::Known) continue;
            needed.push_back(key);
        }
    }
    if(outcome==Outcome::Exhausted) throw ConstructionLimit("capture closure proof exhausted its search budget");
    if(outcome==Outcome::Resolved) {finish(selected);return;}
    if(!needed.empty()) {
        // Import through the parent's public property promise. A raw Known
        // initializer variable may be intentionally opaque in an enclosing
        // proof, even though that proof has supplied the property explicitly.
        for(const auto& key:needed) supplied.push_back(demand(key.first,key.second));
        later(supplied,[this,definition,arguments,site,scope,before,candidates,finish,inputs,needed](const auto& facts){
            auto evidence=inputs;for(std::size_t i=0;i<needed.size();++i) evidence[needed[i]]=facts[i];
            selectCaptures(definition,arguments,site,scope,before,candidates,finish,evidence);
        });return;
    }
    // Ordinary inference failed to close. Retain every lexical dependency in
    // one conservative interface and validate it in the owning request. If its
    // types or initialization do not close, the caller must supply them (or
    // move state into explicit parameters/results).
    fallback();
}
