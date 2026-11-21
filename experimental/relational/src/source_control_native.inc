// A physical call slot does not erase the pure relationships that produced it.
// A certificate receives exactly the actual argument and capture facts.
// Logical receiver views retain their shared roots and designated paths.
// Expression parameters and private nested calls stay opaque.
std::optional<std::vector<Ref>> Session::nativeControlResult(Id temporary) {
    if(auto found=nativeControlResults.find(temporary);found!=nativeControlResults.end()) return found->second;
    auto found=nativeControlCalls.find(temporary);if(found==nativeControlCalls.end()) return {};
    const auto call=found->second;
    SupportScope detachedSupport(supportInputs,nullptr),detachedEffects(effectInputs,nullptr),detachedRuntime(runtimeInputs,nullptr);
    ++nativeProofAttempts;
    auto retained=nativeControlRequests.find(temporary);
    if(retained==nativeControlRequests.end()) {
        // The actual argument/capture snapshot fixes this construction. Retain
        // its obligations, not a failed answer: every retry still receives fresh
        // bindings and rechecks its current parent facts and proof barriers.
        Request proof;Ref type,value,captureValues;const Id boundary=cells.size();
        proof.goals=constructed([&]{
            std::vector<Id> arguments;
            for(std::size_t i=0;i<call.types.size();++i) {
                auto view=call.receivers.find(i);
                if(view==call.receivers.end()) arguments.push_back(known(call.types[i],call.values[i],call.site));
                else {
                    Cell receiver;receiver.kind=Cell::Receiver;receiver.expanded=true;
                    receiver.site=call.site;receiver.scope=call.scope;receiver.location=view->second.location;
                    receiver.receiverPath=view->second.path;receiver.target=known(call.types[i],{},call.site);
                    arguments.push_back(addCell(std::move(receiver)));
                }
            }
            auto entry=states[call.before];entry.controlShadow=true;entry.live=number(1);entry.exits.clear();entry.conditions.clear();
            entry.captureProofIdentity=nextCaptureProof++;
            for(std::size_t i=0;i<call.captures.size();++i) {
                Id slot=call.captures[i];entry.locationTypes[slot]=call.captureTypes[i];
                if(call.captureInputs[i]) entry.values[slot]=known(call.captureTypes[i],call.captureValues[i],call.site);
                else entry.values.erase(slot);
                entry.initialized[slot]=number(call.captureInputs[i] ? 1 : 0);
            }
            auto body=instantiate(call.definition,arguments,call.site,call.scope,true,{},true);
            auto executed=flow(body,state(std::move(entry)));
            type=demand(executed.value,Facet::Type);value=demand(executed.value,Facet::Value);
            captureValues=fresh("native control returned captures");
            later({executed.state},[this,call,captureValues](const auto& closed){
                Id end=closed[0]->number.template convert_to<Id>();
                const auto finalState=states[end];std::vector<Ref> returnedCaptures;
                for(std::size_t i=0;i<call.captures.size();++i) if(call.captureOutputs[i]) {
                    Id slot=call.captures[i];auto outputValue=finalState.values.find(slot);
                    if(outputValue==finalState.values.end()) {pending("native control capture output is not initialized");return;}
                    requireInitialized(slot,end);returnedCaptures.push_back(demand(outputValue->second,Facet::Value));
                }
                equation(captureValues,terms().make("Values",returnedCaptures));
            });
            proof.observation={type,value,captureValues};proof.realization={executed.state,executed.code};
        });
        proof.overrideBundle=[this,boundary](const std::string& name)->std::optional<std::vector<Goal>> {
            if(auto barrier=abstractProofBarrier(name)) return barrier;
            const std::string prefix="source_property_";if(!name.starts_with(prefix)) return {};
            auto split=name.find('_',prefix.size());if(split==std::string::npos) return {};
            Id cell=std::stoul(name.substr(prefix.size(),split-prefix.size()));auto facet=static_cast<Facet>(std::stoul(name.substr(split+1)));
            // The supplied arguments are fresh, grounded facts. Other private
            // values must come from their defining relations; an older unknown
            // value is not a new choice for this certificate to guess.
            if(cell<boundary && cells[cell].kind==Cell::Known && (facet==Facet::Type || facet==Facet::Value)) {
                auto original=facet==Facet::Type ? cells[cell].knownType : cells[cell].knownValue;
                if(!original || !ground(original)) return std::vector<Goal>{unknown("native control proof needs a supplied parent fact")};
            }
            return {};
        };
        retained=nativeControlRequests.emplace(temporary,NativeControlRequest{std::move(proof),{}}).first;
        ++nativeProofConstructions;
    } else ++nativeProofReuses;
    auto search=options.search;search.budget.reset();
    const auto ordinary=std::min(std::size_t(20000),search.maxWork);
    search.maxWork=retained->second.effort.next(ordinary,std::min(std::size_t(160000),search.maxWork));
    if(search.maxWork>ordinary) ++nativeProofBoosts;
    const auto checked=Machine(program,search).solve(retained->second.proof);
    nativeProofWork+=checked.stats.work;
    if(checked.outcome==Outcome::Exhausted) ++nativeProofExhausted;
    if(checked.outcome!=Outcome::Resolved) return {};
    std::vector<Ref> values;
    for(const auto& leaf:leaves(checked.selected->observation[0],checked.selected->observation[1]))
        if(nativeScalar(leaf.type)) values.push_back(leaf.value);
    // The materialized ABI reserves a return slot even for NoValue or a
    // wholly static result. Leave that dummy unknown; captures begin at one.
    if(values.empty()) values.push_back({});
    std::size_t output=0;
    for(std::size_t i=0;i<call.captures.size();++i) if(call.captureOutputs[i]) {
        const auto& returnedValue=checked.selected->observation[2]->arguments[output++];
        for(const auto& leaf:leaves(call.captureTypes[i],returnedValue))
            if(nativeScalar(leaf.type)) values.push_back(leaf.value);
    }
    nativeControlResults.emplace(temporary,values);nativeControlRequests.erase(temporary);return values;
}
Ref Session::controlMeaning(Ref value) {
    std::map<const Term*,Ref> memo;
    std::function<Ref(const Ref&)> visit=[&](const Ref& input)->Ref {
        if(auto found=memo.find(input.get());found!=memo.end()) return found->second;
        auto expression=input->name=="Runtime" ? input->arguments[0] : input;
        if(expression->name=="CallValue" && expression->arguments.size()==2) {
            auto temporary=expression->arguments[0]->number.template convert_to<Id>();
            auto index=expression->arguments[1]->number.template convert_to<std::size_t>();
            if(auto values=nativeControlResult(temporary);values && index<values->size() && values->at(index)) {
                // A private result is proof input only. It is never substituted
                // into emitted C or published as an execution state.
                // Captured inputs can themselves be earlier physical results.
                // Compose only already supplied certificates; a cycle keeps
                // its original opaque slot instead of becoming an assumption.
                memo.emplace(input.get(),input);
                auto result=visit(values->at(index));memo[input.get()]=result;return result;
            }
        }
        if(input->arguments.empty()) return input;
        std::vector<Ref> parts;bool changed=false;
        for(const auto& child:input->arguments) {auto next=visit(child);changed=changed || next!=child;parts.push_back(next);}
        auto result=changed ? terms().make(input->name,parts) : input;memo.emplace(input.get(),result);return result;
    };
    return visit(value);
}
