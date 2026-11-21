// Configuration is ordinary demanded source data with a small, typed target
// vocabulary. CLI contributions enter the same equations. No option overwrites
// a source requirement and no candidate mutates the session's configuration.
void Session::configure() {
    struct Spec {std::string role;unsigned value,lower,upper;};
    std::map<std::string,Spec> specs{
        {"ram_bytes",{"fact",16384,16384,16384}},
        {"rom_bank_bytes",{"fact",8192,8192,8192}},
        {"payload_bytes",{"require",options.payloadBudget,0,65535}},
        {"stack_bytes",{"require",1024,32,12288}},
        {"mapper_segments",{"require",0,0,255}},
        {"call_depth",{"require",0,0,65535}},
        {"stack_proof",{"require",0,0,1}},
        {"bank_chunk_operations",{"prefer",128,1,4096}},
        {"frame_ram_bytes",{"prefer",options.frameRamBytes,0,12288}},
        {"value_frame_bytes",{"prefer",0,0,12288}},
        {"value_choice_branches",{"prefer",2,0,8}},
        {"value_table_bytes",{"prefer",512,0,2048}},
        {"value_table_search",{"prefer",16,0,64}},
        {"value_scratch_reuse",{"prefer",1,0,1}},
        {"value_output_schedule",{"prefer",1,0,1}},
        {"calculation_scratch_reuse",{"prefer",1,0,1}},
        {"calculation_result_sharing",{"prefer",1,0,1}},
        {"calculation_publication_reuse",{"prefer",1,0,1}},
        {"calculation_publication_narrow",{"prefer",1,0,1}},
        {"value_division_sharing",{"prefer",0,0,1}},
        {"calculation_division_sharing",{"prefer",0,0,1}},
        {"value_division_unroll",{"prefer",1,0,1}},
        {"calculation_division_unroll",{"prefer",1,0,1}},
        {"value_rotation_helpers",{"prefer",0,0,1}},
        {"calculation_rotation_helpers",{"prefer",0,0,1}},
        {"value_linear_terms",{"prefer",16,0,64}},
        {"calculation_linear_terms",{"prefer",16,0,64}},
        {"region_copy_schedule",{"prefer",1,0,1}},
        {"region_boundary_expansion",{"prefer",1,0,1}},
        {"region_boundary_overlay",{"prefer",1,0,2}},
        {"region_predicate_functions",{"prefer",1,0,1}},
        {"region_transfer_functions",{"prefer",1,0,1}}
    };
    for(const auto& [key,spec]:specs) {(void)spec;configuration.emplace(key,fresh("configuration "+key));}
    for(const auto& [name,count]:options.streams) streamCapacities.emplace(name,number(count));
    std::map<std::string,std::vector<Ref>> contributions;
    auto contribute=[&](const std::string& role,const std::string& key,Ref value,const std::string& origin) {
        // A physical preference may be scoped to an emitted function. Its
        // identity is recorded by the emitter; consumers do not infer owners
        // from module filenames. Keep CLI and source contributions identical.
        const std::string prefix="bank_chunk_operations.";
        if(key.starts_with(prefix) && !specs.contains(key)) {
            const auto symbol=key.substr(prefix.size());
            if(symbol.empty() || symbol.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                throw std::invalid_argument("invalid bank partition scope: "+key);
            specs.emplace(key,Spec{"prefer",128,1,4096});
            configuration.emplace(key,fresh("configuration "+key));
        }
        const std::string frequencyPrefix="binding_frequency.";
        if(key.starts_with(frequencyPrefix) && !specs.contains(key)) {
            const auto name=key.substr(frequencyPrefix.size());
            if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                throw std::invalid_argument("invalid binding frequency scope: "+key);
            specs.emplace(key,Spec{"prefer",1000000,0,1000000});
            configuration.emplace(key,fresh("configuration "+key));
        }
        const std::string expansionPrefix="boundary_expansion.";
        const std::string outputPrefix="value_output_schedule.";
        if(key.starts_with(outputPrefix) && !specs.contains(key)) {
            const auto name=key.substr(outputPrefix.size());
            if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                throw std::invalid_argument("invalid output schedule scope: "+key);
            specs.emplace(key,specs.at("value_output_schedule"));
            configuration.emplace(key,fresh("configuration "+key));
        }
        for(const std::string scratchPrefix:{"value_scratch_reuse.","calculation_scratch_reuse."})
            if(key.starts_with(scratchPrefix) && !specs.contains(key)) {
                const auto name=key.substr(scratchPrefix.size());
                if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                    throw std::invalid_argument("invalid scratch reuse scope: "+key);
                specs.emplace(key,specs.at(scratchPrefix.substr(0,scratchPrefix.size()-1)));
                configuration.emplace(key,fresh("configuration "+key));
            }
        const std::string resultPrefix="calculation_result_sharing.";
        if(key.starts_with(resultPrefix) && !specs.contains(key)) {
            const auto name=key.substr(resultPrefix.size());
            if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                throw std::invalid_argument("invalid result sharing scope: "+key);
            specs.emplace(key,specs.at("calculation_result_sharing"));
            configuration.emplace(key,fresh("configuration "+key));
        }
        for(const std::string publicationPrefix:{"calculation_publication_reuse.","calculation_publication_narrow."})
        if(key.starts_with(publicationPrefix) && !specs.contains(key)) {
            const auto name=key.substr(publicationPrefix.size());
            if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                throw std::invalid_argument("invalid publication scope: "+key);
            specs.emplace(key,specs.at(publicationPrefix.substr(0,publicationPrefix.size()-1)));
            configuration.emplace(key,fresh("configuration "+key));
        }
        for(const std::string divisionPrefix:{"value_division_sharing.","calculation_division_sharing."})
            if(key.starts_with(divisionPrefix) && !specs.contains(key)) {
                const auto name=key.substr(divisionPrefix.size());
                if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                    throw std::invalid_argument("invalid division sharing scope: "+key);
                specs.emplace(key,specs.at(divisionPrefix.substr(0,divisionPrefix.size()-1)));
                configuration.emplace(key,fresh("configuration "+key));
            }
        for(const std::string unrollPrefix:{"value_division_unroll.","calculation_division_unroll."})
            if(key.starts_with(unrollPrefix) && !specs.contains(key)) {
                const auto name=key.substr(unrollPrefix.size());
                if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                    throw std::invalid_argument("invalid division unroll scope: "+key);
                specs.emplace(key,specs.at(unrollPrefix.substr(0,unrollPrefix.size()-1)));
                configuration.emplace(key,fresh("configuration "+key));
            }
        for(const std::string rotationPrefix:{"value_rotation_helpers.","calculation_rotation_helpers."})
            if(key.starts_with(rotationPrefix) && !specs.contains(key)) {
                const auto name=key.substr(rotationPrefix.size());
                if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                    throw std::invalid_argument("invalid rotation helper scope: "+key);
                specs.emplace(key,specs.at(rotationPrefix.substr(0,rotationPrefix.size()-1)));
                configuration.emplace(key,fresh("configuration "+key));
            }
        for(const std::string linearPrefix:{"value_linear_terms.","calculation_linear_terms."})
            if(key.starts_with(linearPrefix) && !specs.contains(key)) {
                const auto name=key.substr(linearPrefix.size());
                if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                    throw std::invalid_argument("invalid linear combination scope: "+key);
                specs.emplace(key,specs.at(linearPrefix.substr(0,linearPrefix.size()-1)));
                configuration.emplace(key,fresh("configuration "+key));
            }
        for(const std::string tablePrefix:{"value_table_bytes.","value_table_search."})
            if(key.starts_with(tablePrefix) && !specs.contains(key)) {
                const auto name=key.substr(tablePrefix.size());
                if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                    throw std::invalid_argument("invalid value table binding scope: "+key);
                specs.emplace(key,specs.at(tablePrefix.substr(0,tablePrefix.size()-1)));
                configuration.emplace(key,fresh("configuration "+key));
            }
        if(key.starts_with(expansionPrefix) && !specs.contains(key)) {
            const auto name=key.substr(expansionPrefix.size());
            if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                throw std::invalid_argument("invalid boundary expansion scope: "+key);
            specs.emplace(key,Spec{"prefer",1,0,1});
            configuration.emplace(key,fresh("configuration "+key));
        }
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
    if(options.wholeProgram) for(auto site:declarationsList) {
        const auto& node=graph.nodes[site];
        if(!node.exported || node.external) continue;
        if(node.syntax==Syntax::Function) {
            bool namedOccurrence=std::any_of(options.occurrenceSources.begin(),options.occurrenceSources.end(),[&](const auto& item){return item.second==node.text;});
            if(!namedOccurrence && std::find(options.sources.begin(),options.sources.end(),node.text)==options.sources.end()) options.sources.push_back(node.text);
        } else {
            auto query="value:"+node.text;
            if(std::find(options.queries.begin(),options.queries.end(),query)==options.queries.end()) options.queries.push_back(query);
        }
    }
    std::set<Id> top(declarationsList.begin(),declarationsList.end());
    const std::set<std::string> globalTags{"fact","require","prefer","source","stream","no_feedback","reads","writes","c_expansion","c_binding","c_disjoint"};
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
        } else if(tag.name=="c_disjoint") {
            if(tag.arguments.size()<2) throw Error(tag.span,"c_disjoint needs at least two binding identities");
            BindingExclusion exclusion;exclusion.origin=graph.location(tag.span);
            for(auto argument:tag.arguments) {
                const auto& identity=graph.nodes[argument];
                if(identity.syntax!=Syntax::Name && identity.syntax!=Syntax::String)
                    throw Error(tag.span,"c_disjoint identities must be names or strings");
                exclusion.names.push_back(identity.text);
            }
            std::sort(exclusion.names.begin(),exclusion.names.end());
            if(std::adjacent_find(exclusion.names.begin(),exclusion.names.end())!=exclusion.names.end())
                throw Error(tag.span,"c_disjoint cannot exclude a binding from itself");
            bindingExclusions.push_back(std::move(exclusion));
        } else if(tag.name=="c_binding") {
            if(node.syntax!=Syntax::Function || node.external)
                throw Error(tag.span,"c_binding needs a defined source function");
            if(tag.arguments.size()!=4)
                throw Error(tag.span,"c_binding needs an identity, full/case0/case1/region entry, frequency and exclusive/open lifetime contract");
            auto label=[&](unsigned i) {
                const auto& n=graph.nodes[tag.arguments[i]];
                if(n.syntax!=Syntax::Name && n.syntax!=Syntax::String) throw Error(tag.span,"binding identity/entry/contract must be names or strings");
                return n.text;
            };
            auto name=label(0),entry=label(1),contract=label(3);
            if(name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
                throw Error(tag.span,"binding identity must contain only ASCII letters, digits or underscores");
            if(entry!="full" && entry!="case0" && entry!="case1" && entry!="region") throw Error(tag.span,"unknown C binding entry: "+entry);
            if(contract!="exclusive" && contract!="open") throw Error(tag.span,"unknown C binding lifetime contract: "+contract);
            auto weight=observedValue(expression(tag.arguments[2],0));
            request.goals.push_back(lessEqual(number(1),weight));
            request.goals.push_back(lessEqual(weight,number(1000000)));
            if(!valueBindings.emplace(name,ValueBinding{node.text,entry,contract,graph.location(tag.span),weight}).second)
                throw Error(tag.span,"duplicate C binding identity: "+name);
            request.goals.push_back(witness("value_binding_weight_"+name,weight));
            annotatedSources.insert(node.text);
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
            } else if(tag.name=="c_expansion") {
                if(tag.arguments.size()!=1) throw Error(tag.span,"c_expansion needs one C macro identifier");
                const auto& macro=graph.nodes[tag.arguments[0]];
                if(macro.syntax!=Syntax::Name && macro.syntax!=Syntax::String)
                    throw Error(tag.span,"c_expansion needs a name or string");
                contract.offerExpansion(macro.text);
            } else {
                if(tag.arguments.size()!=1) throw Error(tag.span,"resource footprint needs one resource name");
                const auto& name=graph.nodes[tag.arguments[0]];
                if(name.syntax!=Syntax::Name && name.syntax!=Syntax::String) throw Error(tag.span,"resource identity must be a name or string");
                (tag.name=="reads" ? contract.reads : contract.writes).insert(name.text);
            }
        }
    }
    // A full-lifetime exclusion is a supplied C embedding contract. It does
    // not establish self exclusion, close a source graph, or imply transitive
    // exclusion through another binding.
    for(const auto& exclusion:bindingExclusions) for(const auto& name:exclusion.names)
        if(!valueBindings.contains(name)) throw std::invalid_argument("unknown c_disjoint binding "+name+" at "+exclusion.origin);
    for(const auto& [key,spec]:specs) {
        (void)spec;const std::string prefix="binding_frequency.";
        if(key.starts_with(prefix) && !valueBindings.contains(key.substr(prefix.size())))
            throw std::invalid_argument("unknown binding frequency scope: "+key);
        const std::string scratchPrefix="value_scratch_reuse.";
        if(key.starts_with(scratchPrefix) && !valueBindings.contains(key.substr(scratchPrefix.size())))
            throw std::invalid_argument("unknown scratch reuse binding scope: "+key);
        const std::string outputPrefix="value_output_schedule.";
        if(key.starts_with(outputPrefix) && !valueBindings.contains(key.substr(outputPrefix.size())))
            throw std::invalid_argument("unknown output schedule binding scope: "+key);
        const std::string linearPrefix="value_linear_terms.";
        if(key.starts_with(linearPrefix) && !valueBindings.contains(key.substr(linearPrefix.size())))
            throw std::invalid_argument("unknown linear combination binding scope: "+key);
        const std::string divisionPrefix="value_division_sharing.";
        if(key.starts_with(divisionPrefix) && !valueBindings.contains(key.substr(divisionPrefix.size())))
            throw std::invalid_argument("unknown division sharing binding scope: "+key);
        const std::string unrollPrefix="value_division_unroll.";
        if(key.starts_with(unrollPrefix) && !valueBindings.contains(key.substr(unrollPrefix.size())))
            throw std::invalid_argument("unknown division unroll binding scope: "+key);
        const std::string rotationPrefix="value_rotation_helpers.";
        if(key.starts_with(rotationPrefix) && !valueBindings.contains(key.substr(rotationPrefix.size())))
            throw std::invalid_argument("unknown rotation helper binding scope: "+key);
        for(const std::string tablePrefix:{"value_table_bytes.","value_table_search."})
            if(key.starts_with(tablePrefix) && !valueBindings.contains(key.substr(tablePrefix.size())))
                throw std::invalid_argument("unknown value table binding scope: "+key);
        const std::string expansionPrefix="boundary_expansion.";
        if(key.starts_with(expansionPrefix)) {
            auto it=options.boundaries.find(key.substr(expansionPrefix.size()));
            if(it==options.boundaries.end() || it->second.expansion.empty())
                throw std::invalid_argument("boundary expansion preference needs an offered expansion: "+key);
        }
    }
    std::sort(bindingExclusions.begin(),bindingExclusions.end(),[](const auto& a,const auto& b) {
        return a.names!=b.names ? a.names<b.names : a.origin<b.origin;
    });
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
