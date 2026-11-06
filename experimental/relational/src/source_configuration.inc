// Configuration is ordinary demanded source data with a small, typed target
// vocabulary. CLI contributions enter the same equations. No option overwrites
// a source requirement and no candidate mutates the session's configuration.
void Session::configure() {
    struct Spec {std::string role;unsigned value,lower,upper;};
    const std::map<std::string,Spec> specs{
        {"ram_bytes",{"fact",16384,16384,16384}},
        {"rom_bank_bytes",{"fact",8192,8192,8192}},
        {"payload_bytes",{"require",options.payloadBudget,0,65535}},
        {"stack_bytes",{"require",1024,32,12288}},
        {"mapper_segments",{"require",0,0,255}},
        {"call_depth",{"require",0,0,65535}},
        {"stack_proof",{"require",0,0,1}},
        {"bank_chunk_operations",{"prefer",128,1,4096}},
        {"frame_ram_bytes",{"prefer",options.frameRamBytes,0,12288}}
    };
    for(const auto& [key,spec]:specs) {(void)spec;configuration.emplace(key,fresh("configuration "+key));}
    for(const auto& [name,count]:options.streams) streamCapacities.emplace(name,number(count));
    std::map<std::string,std::vector<Ref>> contributions;
    auto contribute=[&](const std::string& role,const std::string& key,Ref value,const std::string& origin) {
        auto found=specs.find(key);
        if(found==specs.end()) throw std::invalid_argument("unknown configuration relationship: "+key+" at "+origin);
        if(found->second.role!=role) throw std::invalid_argument(key+" requires the "+found->second.role+" relationship at "+origin);
        request.goals.push_back(lessEqual(number(found->second.lower),value));
        request.goals.push_back(lessEqual(value,number(found->second.upper)));
        contributions[key].push_back(value);configurationOrigins[key].push_back(origin);
    };
    for(const auto& item:options.configuration)
        contribute(item.role,item.key,number(item.value),"command line "+item.role+" "+item.key+"="+std::to_string(item.value));
    auto declarationsList=moduleDeclarations(graph,graph.nodes[graph.files[0].node].body);
    std::set<Id> top(declarationsList.begin(),declarationsList.end());
    const std::set<std::string> globalTags{"fact","require","prefer","source","stream","no_feedback","reads","writes"};
    std::set<std::string> annotatedSources;
    for(Id site=0;site<graph.nodes.size();++site) for(const auto& tag:graph.nodes[site].attributes) {
        if(!globalTags.contains(tag.name)) continue;
        if(!top.contains(site)) throw Error(tag.span,"configuration relationships belong to module declarations");
        const auto& node=graph.nodes[site];
        if(tag.name=="fact" || tag.name=="require" || tag.name=="prefer") {
            if(tag.arguments.size()!=2) throw Error(tag.span,tag.name+" needs a relationship name and value");
            const auto& key=graph.nodes[tag.arguments[0]];
            if(key.syntax!=Syntax::Name && key.syntax!=Syntax::String) throw Error(tag.span,"configuration key must be a name or string");
            contribute(tag.name,key.text,observedValue(expression(tag.arguments[1],0)),graph.location(tag.span)+" "+tag.text);
        } else if(tag.name=="source" || tag.name=="stream") {
            if(node.syntax!=Syntax::Function || node.external) throw Error(tag.span,"source/stream needs a defined function");
            if(tag.name=="source" && !tag.arguments.empty()) throw Error(tag.span,"source takes no arguments");
            if(tag.name=="stream") {
                if(tag.arguments.size()!=1) throw Error(tag.span,"stream needs a slot count expression");
                auto count=observedValue(expression(tag.arguments[0],0));
                auto [entry,inserted]=streamCapacities.emplace(node.text,count);
                if(!inserted) equation(entry->second,count);
                options.streams.emplace(node.text,0); // Membership; values remain solver facts.
            }
            annotatedSources.insert(node.text);
        } else {
            if(node.syntax!=Syntax::Function || !node.external) throw Error(tag.span,"boundary facts need an external function");
            auto& contract=options.boundaries[node.text];
            if(tag.name=="no_feedback") {
                if(!tag.arguments.empty()) throw Error(tag.span,"no_feedback takes no arguments");
                contract.noFeedback=true;
            } else {
                if(tag.arguments.size()!=1) throw Error(tag.span,"resource footprint needs one resource name");
                const auto& name=graph.nodes[tag.arguments[0]];
                if(name.syntax!=Syntax::Name && name.syntax!=Syntax::String) throw Error(tag.span,"resource identity must be a name or string");
                (tag.name=="reads" ? contract.reads : contract.writes).insert(name.text);
            }
        }
    }
    for(const auto& name:annotatedSources)
        if(std::find(options.sources.begin(),options.sources.end(),name)==options.sources.end()) options.sources.push_back(name);
    if(!options.streams.empty()) {
        if(options.activation!="serial" && options.activation!="stream") throw std::invalid_argument("stream pools require serial execution");
        if(!options.eventOrder.empty() || !options.accessOrder.empty() || !options.occurrenceSources.empty())
            throw std::invalid_argument("stream pools own their dynamic lifetimes; finite occurrence edges do not apply");
        for(const auto& name:options.sources) if(!options.streams.contains(name))
            throw std::invalid_argument("all sources in a stream projection need a bounded pool: "+name);
        options.activation="stream";
    }
    for(const auto& [name,count]:streamCapacities) {
        request.goals.push_back(lessEqual(number(1),count));request.goals.push_back(lessEqual(count,number(255)));
        request.goals.push_back(witness("stream_"+name,count));observableArtifacts.push_back("stream_"+name);
    }
    for(const auto& [key,spec]:specs) {
        auto result=configuration.at(key);
        const auto& values=contributions[key];
        if(values.empty()) {equation(result,number(spec.value));configurationOrigins[key].push_back("target default");}
        else if(spec.role=="prefer") {
            // Multiple local-buffer allowances combine conservatively. They
            // never make a semantically valid program contradictory.
            later(values,[this,result](const auto& args){
                Integer bound=args[0]->number;for(const auto& arg:args) bound=std::min(bound,arg->number);
                equation(result,number(bound));
            });
        } else for(const auto& value:values) equation(result,value);
        request.goals.push_back(witness("configuration_"+key,result));
        // Requirements/facts are part of the observable input contract. Hints
        // influence realization only and cannot select a semantic observation.
        if(spec.role!="prefer") observableArtifacts.push_back("configuration_"+key);
    }
}
void Session::mapperStorage(Id id,Ref persistent) {
    const auto& declaration=graph.nodes[cells[id].site];
    auto tag=std::find_if(declaration.attributes.begin(),declaration.attributes.end(),[](const Metadata& item){return item.name=="mapper";});
    if(tag==declaration.attributes.end()) return;
    if(declaration.external || !tag->arguments.empty()) throw Error(tag->span,"mapper attaches without arguments to owned persistent storage");
    auto size=fresh("mapped storage size");mapperSizes.emplace(id,size);
    later({persistent,demand(id,Facet::Type)},[this,size](const auto& args){
        if(args[0]->number==0) {equation(size,number(0));return;}
        unsigned bytes=0;
        for(const auto& leaf:leaves(args[1])) {
            if(!nativeScalar(leaf.type)) {pending("mapper storage needs native scalar leaves");return;}
            bytes+=nativeBytes(leaf.type);
        }
        equation(size,number(bytes));
    });
    auto total=fresh("mapper occupied bytes"),capacity=fresh("mapper capacity bytes");
    request.goals.push_back(add(mapperExtent ? mapperExtent : number(0),size,total));mapperExtent=total;
    request.goals.push_back(multiply(configuration.at("mapper_segments"),number(16384),capacity));
    request.goals.push_back(lessEqual(total,capacity));
    request.goals.push_back(witness("mapper_"+std::to_string(id),size));
}
unsigned Session::configured(const Model& model,const std::string& key) const {
    return model.witnesses.at("configuration_"+key)->number.convert_to<unsigned>();
}
