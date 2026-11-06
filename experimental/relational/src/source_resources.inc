// Resources supply immutable input facts. Snapshots belong to the input
// revision, so backtracking and repeated demands cannot repeat host actions.
void Session::resourceProperty(Id id,Facet facet,Ref result) {
    const auto node=graph.nodes[cells[id].site];
    const auto string=terms().make("String");
    auto bytes=[this](const std::string& contents) {
        std::vector<Ref> values;values.reserve(contents.size());
        for(unsigned char byte:contents) values.push_back(number(byte));
        return terms().make("Bytes",values);
    };
    auto stringProperty=[this,id,facet,result,string,bytes](const std::function<std::string()>& read) {
        if(facet==Facet::Type) equation(result,string);
        else if(facet==Facet::Element) equation(result,terms().make("UInt",{number(8)}));
        else if(facet==Facet::Value) {
            auto contents=read();
            if(contents.size()>options.maxFileBytes) throw ConstructionLimit("resource byte budget exhausted");
            equation(result,bytes(contents));
        }
        else if(facet==Facet::Length) request.goals.push_back(call("extent_value",{string,demand(id,Facet::Value),result}));
        else conflict();
    };
    if(node.key=="process") {
        stringProperty([this,node]{
            if(!options.allowProcesses) throw Error(node.span,"process imports require --allow-processes");
            auto directory=graph.files[node.span.file].path.parent_path().string();
            auto key=vx::quote(directory)+":"+vx::quote(node.text);
            auto found=graph.processSnapshots->find(key);
            if(found==graph.processSnapshots->end()) {
                auto output=runProcess({"/bin/sh","-c",node.text},{},options.processTimeoutMs,false,directory,options.maxFileBytes);
                found=graph.processSnapshots->emplace(key,std::move(output)).first;
            }
            if(found->second.second=="timeout" || found->second.second=="response-too-large")
                throw ConstructionLimit("process import exhausted its budget: "+found->second.second);
            if(found->second.second!="received") throw Error(node.span,"process import failed: "+found->second.second);
            return found->second.first;
        });return;
    }
    auto& snapshots=*graph.inputSnapshots;
    const auto selection="resource-path:"+graph.location(node.span)+":"+node.text;
    auto selected=snapshots.find(selection);
    std::error_code error;
    if(selected==snapshots.end()) {
        auto candidate=graph.resolve(node.text,node.span);
        auto target=snapshots.find("path-target:"+std::filesystem::absolute(candidate).lexically_normal().string());
        auto resolved=target==snapshots.end() ? std::filesystem::weakly_canonical(candidate,error) : std::filesystem::path(target->second);
        if(error) throw Error(node.span,"cannot resolve resource: "+node.text);
        selected=snapshots.emplace(selection,resolved.string()).first;
    }
    const std::filesystem::path path=selected->second;auto key=path.string();
    auto dependency=path;
    while(!std::filesystem::exists(dependency,error) && dependency.has_parent_path() && dependency!=dependency.parent_path()) dependency=dependency.parent_path();
    if(error) throw Error(node.span,"cannot inspect resource dependency: "+key);
    graph.dependencies.insert(dependency.string());
    auto kind=snapshots.find("resource-kind:"+key);
    if(kind==snapshots.end()) {
        bool exists=std::filesystem::exists(path,error);
        if(error) throw Error(node.span,"cannot inspect resource: "+key);
        bool directory=std::filesystem::is_directory(path,error) || (!exists && !path.has_extension());
        if(error && exists) throw Error(node.span,"cannot inspect resource kind: "+key);
        kind=snapshots.emplace("resource-kind:"+key,directory ? "directory" : "file").first;
    }
    if(kind->second=="file") {
        stringProperty([this,path,node]{return graph.files[graph.read(path,false,node.span)].bytes;});return;
    }
    auto directory=graph.directorySnapshots->find(key);
    if(directory==graph.directorySnapshots->end()) {
        std::vector<std::string> names;error.clear();
        if(std::filesystem::exists(path,error)) {
            std::filesystem::directory_iterator entries(path,error),end;
            if(error) throw Error(node.span,"cannot enumerate resource: "+key);
            for(;entries!=end;entries.increment(error)) {
                if(error) throw Error(node.span,"cannot enumerate resource: "+key);
                if(entries->is_regular_file(error)) names.push_back(entries->path().filename().string());
                if(error) throw Error(node.span,"cannot inspect resource entry: "+key);
                if(names.size()>10000) throw ConstructionLimit("directory resource exceeds aggregate budget");
            }
        }
        if(error) throw Error(node.span,"cannot enumerate resource: "+key);
        std::sort(names.begin(),names.end());directory=graph.directorySnapshots->emplace(key,std::move(names)).first;
    }
    auto tuple=tupleType(terms(),{string,string});
    if(facet==Facet::Type) equation(result,terms().make("Array",{tuple,number(directory->second.size())}));
    else if(facet==Facet::Element) equation(result,tuple);
    else if(facet==Facet::Length) equation(result,number(directory->second.size()));
    else if(facet==Facet::Value) {
        std::vector<Ref> entries;std::size_t size=0;
        for(const auto& name:directory->second) {
            const auto contents=graph.files[graph.read(path/name,false,node.span)].bytes;
            if(contents.size()>options.maxFileBytes-size) throw ConstructionLimit("directory contents exceed resource byte budget");
            size+=contents.size();
            entries.push_back(terms().make("RecordValue",{tuple->arguments[0],terms().make("Values",{bytes(name),bytes(contents)})}));
        }
        equation(result,terms().make("Values",entries));
    } else conflict();
}
