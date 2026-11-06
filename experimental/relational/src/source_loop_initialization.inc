// Initialization is a safety fact distinct from a slot's stored bits. The
// inexpensive readiness fixed point forgets changing values at back edges.
// A sufficient numeric refinement relates the actual initial/update values
// before quantification and renaming, so numeric guards can justify readiness
// without treating a previous iteration's predicate as an invariant.
Ref Session::closeLoopInitialization(Id loop,Id before,Id checked,Id ended,Ref condition,Ref stopped,Ref checkAgain,Ref escaped,
                                     const std::vector<Id>& carried,Ref iterableType,Id firstTemporary) {
    auto result=fresh("loop initialization closure");
    std::vector<Ref> initial,check,body,reads,safe,invariants,numericSlots;
    const auto obligations=loopInitializations.at(loop).reads;
    for(Id slot:carried) {
        initial.push_back(initialized(slot,states[before]));check.push_back(initialized(slot,states[checked]));body.push_back(initialized(slot,states[ended]));
        const auto& info=loopInitializations.at(loop).slots.at(slot);
        if(info.initial!=none && reachingValue(slot,checked)==info.proxy && reachingValue(slot,ended)==info.proxy)
            invariants.push_back(terms().make("Invariant",{number(slot),demand(info.initial,Facet::Type),demand(info.initial,Facet::Value)}));
        else {
            auto type=demand(info.proxy,Facet::Type),startValue=fresh("loop initial numeric facts");
            if(info.initial!=none) equation(startValue,demand(info.initial,Facet::Value));
            else later({type},[this,startValue](const auto& types){equation(startValue,structure(types[0],[this](const Ref&){return number(0);}));});
            numericSlots.push_back(terms().make("NumericSlot",{number(slot),type,startValue,
                demand(reachingValue(slot,checked),Facet::Value),demand(reachingValue(slot,ended),Facet::Value)}));
        }
    }
    for(const auto& [slot,read]:obligations) {
        reads.push_back(read);auto proof=fresh("loop read safety");safe.push_back(proof);
        // Publish the parent obligation before the outgoing flow state. An
        // inner proof may close later than the enclosing body's state graph.
        initializationObligation(slot,proof,states[before].conditions,states[before].initializationLoop);
    }
    auto facts=terms().make("Initialization",{terms().make("Ready",initial),terms().make("Ready",check),terms().make("Ready",body),
        terms().make("Reads",reads),condition,stopped,checkAgain,escaped,terms().make("Premises",states[before].conditions),terms().make("Invariants",invariants),terms().make("NumericSlots",numericSlots)});
    later({facts},[this,loop,carried,iterableType,firstTemporary,safe,result](const auto& inputs){
        std::map<Id,std::vector<Leaf>> stable;
        for(const auto& invariant:inputs[0]->arguments[9]->arguments)
            stable.emplace(invariant->arguments[0]->number.template convert_to<Id>(),leaves(invariant->arguments[1],invariant->arguments[2]));
        std::function<Ref(const Ref&)> normalize=[&](const Ref& value)->Ref {
            if(value->name=="Runtime" && value->arguments[0]->name=="LoopSlotValue") {
                const auto& slot=value->arguments[0]->arguments;
                if(slot[0]->number==loop) if(auto found=stable.find(slot[1]->number.template convert_to<Id>());found!=stable.end())
                    return found->second.at(slot[2]->number.template convert_to<std::size_t>()).value;
            }
            if(value->arguments.empty()) return value;
            std::vector<Ref> children;bool changed=false;
            for(const auto& child:value->arguments) {auto next=normalize(child);changed=changed || next!=child;children.push_back(next);}
            return changed ? terms().make(value->name,children) : value;
        };
        const auto normalized=normalize(inputs[0]);const auto& f=normalized->arguments;
        struct Certificate {std::vector<Ref> safe,completed;std::size_t iterations=0,nodes=0;};
        auto calculate=[&](bool numeric)->Certificate {
        ControlProof proof;
        auto conjunction=[&](std::size_t a,std::size_t b){return proof.conjunction(a,b);};
        auto disjunction=[&](std::size_t a,std::size_t b){return proof.disjunction(a,b);};
        auto negate=[&](std::size_t a){return proof.negate(a);};
        std::vector<std::size_t> current,next,atCheck,atBody;std::map<std::string,std::size_t> rename;
        std::size_t entry=1;
        for(const auto& premise:f[8]->arguments) entry=conjunction(entry,proof.read(premise));
        for(std::size_t i=0;i<carried.size();++i) {
            auto now=terms().make("LoopReady",{number(loop),number(carried[i])});
            auto after=terms().make("NextLoopReady",{number(loop),number(carried[i])});
            current.push_back(proof.read(now));next.push_back(proof.read(after));rename.emplace(show(after),current.back());
            entry=conjunction(entry,proof.equivalent(current.back(),proof.read(f[0]->arguments[i])));
            atCheck.push_back(proof.read(f[1]->arguments[i]));atBody.push_back(proof.read(f[2]->arguments[i]));
        }
        std::size_t checkData=1,bodyData=1;
        if(numeric) {
            std::set<Id> needed;
            std::function<void(const Ref&)> collectDependencies=[&](const Ref& value){
                if(value->name=="LoopSlotValue" && value->arguments[0]->number==loop)
                    needed.insert(value->arguments[1]->number.template convert_to<Id>());
                for(const auto& child:value->arguments) collectDependencies(child);
            };
            for(std::size_t i=0;i<9;++i) collectDependencies(f[i]);
            std::size_t previous=0;
            do {
                previous=needed.size();
                for(const auto& item:f[10]->arguments) if(needed.contains(item->arguments[0]->number.template convert_to<Id>())) {
                    collectDependencies(item->arguments[3]);collectDependencies(item->arguments[4]);
                }
            } while(previous!=needed.size());
            auto same=[&](Ref a,Ref b){return proof.read(terms().make("Compare",{terms().make("=="),a,b}));};
            for(const auto& item:f[10]->arguments) {
                const auto& a=item->arguments;Id slot=a[0]->number.template convert_to<Id>();
                if(!needed.contains(slot)) continue;
                auto starts=leaves(a[1],a[2]),checks=leaves(a[1],a[3]),bodies=leaves(a[1],a[4]);
                for(std::size_t j=0;j<starts.size();++j) {
                    const auto& type=starts[j].type;if(!nativeScalar(type)) continue;
                    unsigned width=scalarBits(type);bool sign=scalarSigned(type);
                    auto minimum=number(sign ? -(Integer(1)<<(width-1)) : Integer(0));
                    auto maximum=number((Integer(1)<<(sign ? width-1 : width))-1);
                    auto now=terms().make("Runtime",{terms().make("LoopSlotValue",{number(loop),number(slot),number(j),type}),minimum,maximum});
                    auto after=terms().make("Runtime",{terms().make("NextLoopSlotValue",{number(loop),number(slot),number(j),type}),minimum,maximum});
                    auto names=proof.variableRenaming(after,now);
                    if(!names) continue;
                    rename.insert(names->begin(),names->end());
                    entry=conjunction(entry,same(now,starts[j].value));
                    checkData=conjunction(checkData,same(after,checks[j].value));
                    bodyData=conjunction(bodyData,same(after,bodies[j].value));
                }
            }
            if(iterableType) {
                auto count=arrayCount(iterableType);unsigned width=1;
                while(width<32 && (Integer(1)<<width)<=count) ++width;
                auto now=terms().make("Runtime",{terms().make("LoopIndex",{number(loop)}),number(0),number(count)});
                auto after=terms().make("Runtime",{terms().make("NextLoopIndex",{number(loop)}),number(0),number((Integer(1)<<width)-1)});
                if(auto names=proof.variableRenaming(after,now)) {
                    rename.insert(names->begin(),names->end());entry=conjunction(entry,same(now,number(0)));
                    checkData=conjunction(checkData,same(after,now));
                    auto increment=terms().make("Runtime",{terms().make("Arithmetic",{terms().make("+"),number(width),now,number(1)}),number(0),number((Integer(1)<<width)-1)});
                    bodyData=conjunction(bodyData,same(after,increment));
                }
            }
        }
        auto predicate=proof.read(f[4]),stop=proof.read(f[5]),again=proof.read(f[6]),escape=proof.read(f[7]);
        std::function<bool(const Ref&)> unstable=[&](const Ref& value) {
            if((value->name=="LoopReady" || value->name=="LoopSlotValue" || value->name=="LoopValue" || value->name=="LoopIndex" ||
                value->name=="LoopFlag" || value->name=="LoopReturned") && value->arguments[0]->number>=loop) return true;
            if(value->name=="CallValue" && value->arguments[0]->number>=firstTemporary) return true;
            return std::any_of(value->arguments.begin(),value->arguments.end(),unstable);
        };
        auto relation=[&](const std::vector<std::size_t>& values) {
            std::size_t combined=1;
            for(std::size_t i=0;i<values.size();++i) combined=conjunction(combined,proof.equivalent(next[i],values[i]));
            return combined;
        };
        auto checkEdge=conjunction(conjunction(conjunction(negate(predicate),again),relation(atCheck)),checkData);
        auto bodyEdge=conjunction(conjunction(conjunction(predicate,negate(stop)),relation(atBody)),bodyData);
        auto edges=disjunction(checkEdge,bodyEdge);
        auto advance=[&](std::size_t head) {
            return proof.substitute(proof.exists(conjunction(head,edges),unstable),rename);
        };
        // A nonempty fixed iterable enters its body once before any back
        // edge. A singleton has no second body activation. General runtime
        // predicates retain both outcomes unless their own facts close one.
        std::size_t firstRestriction=1,repeatedRestriction=1;
        if(iterableType) {
            auto count=arrayCount(iterableType);firstRestriction=count ? predicate : negate(predicate);
            if(count<=1) repeatedRestriction=negate(predicate);
        }
        auto first=conjunction(entry,firstRestriction);auto reached=advance(first);std::size_t iterations=0;
        for(;;) {
            auto expanded=disjunction(reached,advance(conjunction(reached,repeatedRestriction)));++iterations;
            if(expanded==reached) break;
            reached=expanded;
            if(iterations>100000) throw ConstructionLimit("loop initialization proof budget exhausted");
        }
        auto heads=disjunction(first,conjunction(reached,repeatedRestriction));
        Certificate certificate;
        for(std::size_t i=0;i<f[3]->arguments.size();++i) {
            const auto& read=f[3]->arguments[i]->arguments;auto failure=conjunction(heads,negate(proof.read(read[0])));
            for(std::size_t j=1;j<read.size();++j) failure=conjunction(failure,proof.read(read[j]));
            certificate.safe.push_back(proof.term(negate(proof.exists(failure,unstable)),terms()));
        }
        auto checkedExit=conjunction(conjunction(negate(predicate),negate(again)),negate(escape));
        auto bodyExit=conjunction(conjunction(predicate,stop),negate(escape));
        std::vector<Ref> completed;
        for(std::size_t i=0;i<carried.size();++i) {
            auto failure=disjunction(conjunction(checkedExit,negate(atCheck[i])),conjunction(bodyExit,negate(atBody[i])));
            failure=proof.exists(conjunction(heads,failure),unstable);
            completed.push_back(proof.term(negate(failure),terms()));
        }
        certificate.completed=std::move(completed);certificate.iterations=iterations;certificate.nodes=proof.size();return certificate;
        };
        auto certificate=calculate(false);
        auto missing=[](const Ref& value){return value->kind!=Term::Number || value->number!=1;};
        if(std::any_of(certificate.safe.begin(),certificate.safe.end(),missing) ||
           std::any_of(certificate.completed.begin(),certificate.completed.end(),missing)) {
            try {
                const auto refined=calculate(true);auto combined=certificate;ControlProof joined;
                for(std::size_t i=0;i<certificate.safe.size();++i)
                    combined.safe[i]=joined.term(joined.disjunction(joined.read(certificate.safe[i]),joined.read(refined.safe[i])),terms());
                for(std::size_t i=0;i<certificate.completed.size();++i)
                    combined.completed[i]=joined.term(joined.disjunction(joined.read(certificate.completed[i]),joined.read(refined.completed[i])),terms());
                combined.iterations+=refined.iterations;combined.nodes+=refined.nodes;certificate=std::move(combined);
            } catch(const ConstructionLimit&) {
                // An unfinished refinement is not a new safety fact.
            }
        }
        for(std::size_t i=0;i<safe.size();++i) equation(safe[i],certificate.safe[i]);
        equation(result,terms().make("Ready",certificate.completed));
        request.goals.push_back(witness("loop_initialization_"+std::to_string(loop),terms().make("InitializationProof",{
            number(carried.size()),number(safe.size()),number(certificate.iterations),number(certificate.nodes)})));
    });return result;
}
