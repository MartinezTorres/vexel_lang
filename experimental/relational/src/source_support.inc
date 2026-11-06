// Dependency support is a term produced by each property and each delayed
// builder. It closes only after the selected construction has supplied its
// edges. Other search contexts can bind those same promises differently.
void Session::matchingProperty(Id argument,Facet facet,const std::function<void(Ref)>& use) {
    use(demand(argument,facet));
}
void Session::matchScore(Ref supplied,Ref target,Id argument,Ref result,bool invariant,bool signatureOnly) {
    later({supplied,target},[this,argument,result,invariant,signatureOnly](const auto& types){
        const auto& source=types[0];const auto& destination=types[1];
        if(identical(source,destination)) {equation(result,number(2));return;}
        if(source->name.starts_with("Opaque") || destination->name.starts_with("Opaque")) {pending("an opaque interface cannot decide overload applicability");return;}
        if(destination->name=="RecordPattern") {
            equation(result,number(source->name=="Record" && identical(source->arguments[0],destination->arguments[0]) ? 2 : -1));return;
        }
        std::function<bool(const Ref&)> opaque=[&](const Ref& type){
            return type->name.starts_with("Opaque") || std::any_of(type->arguments.begin(),type->arguments.end(),opaque);
        };
        if(opaque(source) || opaque(destination)) {pending("partial opaque properties cannot decide overload applicability");return;}
        if(destination->name=="Array") {
            if(source->name!="Array" || !identical(source->arguments[1],destination->arguments[1])) {equation(result,number(-1));return;}
            // Exact literals need representability evidence. Typed array
            // applicability otherwise reads only the structural interface.
            std::function<bool(const Ref&,const Ref&)> needsEvidence=[&](const Ref& from,const Ref& to) {
                if(identical(from,to) || to->name=="RecordPattern") return false;
                if(from->name=="Exact") return scalarFormat(to).has_value();
                if(from->name=="Array" && to->name=="Array") return needsEvidence(from->arguments[0],to->arguments[0]);
                if(tupleType(from) && tupleType(to) && identical(from->arguments[0],to->arguments[0])) {
                    const auto& a=from->arguments[1]->arguments;const auto& b=to->arguments[1]->arguments;
                    for(std::size_t i=0;i<a.size();++i) if(needsEvidence(a[i]->arguments[0],b[i]->arguments[0])) return true;
                }
                return false;
            };
            bool needValues=needsEvidence(source->arguments[0],destination->arguments[0]);
            auto elements=[this,source,destination,result,invariant,signatureOnly](Ref evidence){
                std::vector<Ref> values,scores;
                for(std::size_t i=0;i<arrayCount(source);++i) {
                    auto value=fresh("match element value"),score=fresh("match element score");values.push_back(value);scores.push_back(score);
                    matchScore(source->arguments[0],destination->arguments[0],known(source->arguments[0],value),score,invariant,signatureOnly);
                }
                if(evidence) equation(evidence,terms().make("Values",values));
                later(scores,[this,result](const auto& items){
                    equation(result,number(std::all_of(items.begin(),items.end(),[](const Ref& s){return s->number>=0;}) ? 1 : -1));
                });
            };
            if(needValues && !signatureOnly) matchingProperty(argument,Facet::Value,elements);
            else elements({});
            return;
        }
        if(tupleType(source) && tupleType(destination)) {
            if(!identical(source->arguments[0],destination->arguments[0])) {equation(result,number(-1));return;}
            std::vector<Ref> scores;const auto& a=source->arguments[1]->arguments;const auto& b=destination->arguments[1]->arguments;
            for(std::size_t i=0;i<a.size();++i) {
                auto score=fresh("tuple field match");scores.push_back(score);
                matchScore(a[i]->arguments[0],b[i]->arguments[0],projection(argument,{a[i]->name}),score,invariant,signatureOnly);
            }
            later(scores,[this,result](const auto& items){
                equation(result,number(std::all_of(items.begin(),items.end(),[](const Ref& s){return s->number>=0;}) ? 1 : -1));
            });return;
        }
        if(aggregate(source) || aggregate(destination)) {equation(result,number(-1));return;}
        if(invariant) {equation(result,number(-1));return;}
        if(signatureOnly && source->name=="Exact" && scalarFormat(destination)) {equation(result,number(1));return;}
        auto score=[this,source,destination,result](Ref evidence){
            request.goals.push_back(call("argument_score",{source,destination,evidence,result}));
        };
        if(source->name=="Exact") matchingProperty(argument,Facet::Value,score);
        else score(terms().make("NoValueNeeded"));
    });
}
void Session::matchArgument(Id annotationSite,Id scope,Id argument,Ref result,bool invariant,bool signatureOnly) {
    if(annotationSite==none) {equation(result,number(0));return;}
    const auto& spec=graph.nodes[annotationSite];auto definition=record(scope,spec.text);
    if(spec.text=="(tuple)" || (spec.body==none && spec.children.empty() && definition.site!=none)) {
        matchingProperty(argument,Facet::Category,[this,annotationSite,scope,argument,result,invariant,signatureOnly,definition](Ref categoryFact){
        later({categoryFact},[this,annotationSite,scope,argument,result,invariant,signatureOnly,definition](const auto& category){
            if(category[0]->name.starts_with("Opaque")) {pending("an opaque category cannot decide overload applicability");return;}
            if(category[0]->name!="Record") {equation(result,number(-1));return;}
            matchingProperty(argument,Facet::Identity,[this,annotationSite,scope,argument,result,invariant,signatureOnly,definition](Ref identityFact){
            later({identityFact},[this,annotationSite,scope,argument,result,invariant,signatureOnly,definition](const auto& identities){
                if(identities[0]->name.starts_with("Opaque")) {pending("an opaque identity cannot decide overload applicability");return;}
                const auto& annotationNode=graph.nodes[annotationSite];
                if(annotationNode.text!="(tuple)") {Cell expected;expected.definition=definition;equation(result,number(identical(identities[0],nominal(expected)) ? 2 : -1));return;}
                if(!identical(identities[0],terms().make("Tuple",{number(annotationNode.children.size())}))) {equation(result,number(-1));return;}
                std::vector<Ref> scores;
                for(std::size_t i=0;i<annotationNode.children.size();++i) {
                    auto score=fresh("tuple pattern score");scores.push_back(score);
                    matchArgument(annotationNode.children[i],scope,projection(argument,{"__"+std::to_string(i)}),score,invariant,signatureOnly);
                }
                later(scores,[this,result](const auto& items){
                    bool applicable=std::all_of(items.begin(),items.end(),[](const Ref& score){return score->number>=0;});
                    bool exact=std::all_of(items.begin(),items.end(),[](const Ref& score){return score->number==2;});
                    equation(result,number(applicable ? (exact ? 2 : 1) : -1));
                });
            });
            });
        });
        });return;
    }
    // Nested array patterns describe nominal families, without constructing
    // an unrelated candidate's dependent schema or captured environment.
    auto match=[this,annotationSite,scope,argument,result,invariant,signatureOnly]{
    std::function<Ref(Id)> pattern=[&](Id site)->Ref {
        const auto& node=graph.nodes[site];
        if(node.text=="(tuple)") {std::vector<Ref> fields;for(Id field:node.children) fields.push_back(pattern(field));return tupleType(terms(),fields);}
        Ref target;auto recordDefinition=record(scope,node.text);
        if(node.body==none && recordDefinition.site!=none) {Cell identity;identity.definition=recordDefinition;target=terms().make("RecordPattern",{nominal(identity)});}
        else target=annotationBase(site,scope);
        for(Id dimension:node.children) target=terms().make("Array",{target,demand(expression(dimension,scope),Facet::Value)});
        return target;
    };
    auto target=pattern(annotationSite),category=fresh("argument pattern category");typeCategory(target,category);
    matchingProperty(argument,Facet::Category,[this,argument,target,category,result,invariant,signatureOnly](Ref suppliedCategory){
    later({suppliedCategory,category},[this,argument,target,result,invariant,signatureOnly](const auto& categories){
        if(categories[0]->name.starts_with("Opaque") || categories[1]->name.starts_with("Opaque")) {pending("an opaque category cannot decide overload applicability");return;}
        if(!identical(categories[0],categories[1])) {equation(result,number(-1));return;}
        matchingProperty(argument,Facet::Type,[this,argument,target,result,invariant,signatureOnly](Ref type){
            matchScore(type,target,argument,result,invariant,signatureOnly);
        });
    });
    });
    };
    match();
}
void Session::scoreArguments(Definition definition,const std::vector<Id>& arguments,
                             std::size_t at,Id scope,Ref total,Ref result,bool signatureOnly) {
    if(at==arguments.size()) {equation(result,total);return;}
    const auto& fn=graph.nodes[definition.site];const auto& parameter=graph.nodes[fn.children[at]];
    auto score=fresh("argument specificity");
    matchArgument(parameter.annotation,scope,arguments[at],score,at<fn.receiverCount && mutableReceiver(arguments[at]),signatureOnly);
    later({score},[this,definition,arguments,at,scope,total,result,signatureOnly](const auto& scores){
        if(scores[0]->number<0) {equation(result,number(-1));return;}
        const auto& param=graph.nodes[graph.nodes[definition.site].children[at]];
        Id next=environment(scope);environments[next].values[param.text]=bound(graph.nodes[definition.site].children[at],scope,param.annotation,arguments[at]);
        auto sum=fresh("specificity sum");request.goals.push_back(add(total,scores[0],sum));
        scoreArguments(definition,arguments,at+1,next,sum,result,signatureOnly);
    });
}
void Session::captureSupport(Ref tree,const std::map<Id,std::string>& lexical,
                             const std::set<Id>& owned,std::set<Property> visited,Ref result) {
    later({tree},[this,lexical,owned,visited=std::move(visited),result](const auto& trees) mutable {
        std::vector<Property> keys;
        std::function<void(const Ref&)> walk=[&](const Ref& node){
            if(node->name=="Property") keys.emplace_back(node->arguments[0]->number.template convert_to<Id>(),
                static_cast<Facet>(node->arguments[1]->number.template convert_to<unsigned>()));
            else for(const auto& child:node->arguments) walk(child);
        };
        walk(trees[0]);std::vector<Ref> parts;
        // Do not mutate the callback's captured visited set: cached builders
        // can run with different ground inputs in sibling contexts.
        auto seen=visited;
        for(auto key:keys) {
            if(owned.contains(key.first) || !seen.insert(key).second) continue;
            if(auto found=lexical.find(key.first);found!=lexical.end()) {
                // Reading a global through execution first asks whether it is
                // persistent. Its schema parameter is the resulting pure
                // value, never the storage-classification bit.
                auto facet=key.second==Facet::Storage ? Facet::Execution : key.second;
                std::string prefix=facet==Facet::Value || facet==Facet::Execution ? "" : facet==Facet::Type ? "type:" : facet==Facet::Length ? "length:" : facet==Facet::Identity ? "identity:" : facet==Facet::Category ? "category:" : facet==Facet::Shape ? "shape:" : "element:";
                parts.push_back(terms().make("Captures",{terms().make(prefix+found->second,{demand(key.first,facet)})}));
            } else {
                (void)demand(key.first,key.second);
                auto part=fresh("transitive captures");captureSupport(supports.at(key),lexical,owned,seen,part);parts.push_back(part);
            }
        }
        later(parts,[this,result](const auto& closures){
            std::map<std::string,Ref> captures;
            for(const auto& closure:closures) for(const auto& field:closure->arguments) {
                auto [at,inserted]=captures.emplace(field->name,field);
                if(!inserted) equation(at->second,field);
            }
            std::vector<Ref> fields;for(const auto& [name,value]:captures) {
                // These stronger projections determine the same category.
                // Omit redundant evidence, but preserve a category-only use.
                if(name.starts_with("category:")) {
                    const auto owner=name.substr(9);
                    if(captures.contains("type:"+owner) || captures.contains("identity:"+owner) ||
                       (value->arguments[0]->name=="Array" && (captures.contains("length:"+owner) || captures.contains("element:"+owner)))) continue;
                }
                fields.push_back(value);
            }
            equation(result,terms().make("Captures",fields));
        });
    });
}
