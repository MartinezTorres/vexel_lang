// Boundary facts are supplied by the embedding adapter. A read-only footprint
// is still an external observation, and exclusion of feedback says nothing
// about purity. Neither fact is inferred from the sink's C signature or name.
void Session::validateContracts() {
    for(const auto& [name,contract]:options.boundaries) {
        (void)contract;
        bool declared=std::any_of(graph.nodes.begin(),graph.nodes.end(),[&](const Node& node){
            return node.syntax==Syntax::Function && node.external && node.key!="bundled-math" && node.text==name;
        });
        if(!declared) throw std::invalid_argument("boundary contract needs an external function declaration: "+name);
    }
}
Ref Session::externalAccesses(const std::string& name) {
    auto found=options.boundaries.find(name);std::vector<Ref> accesses;
    if(found==options.boundaries.end() || (found->second.reads.empty() && found->second.writes.empty()))
        accesses.push_back(terms().make("Access",{terms().make("external-world"),number(1)}));
    else {
        for(const auto& resource:found->second.reads)
            accesses.push_back(terms().make("Access",{terms().make("external:"+resource),number(0)}));
        for(const auto& resource:found->second.writes)
            accesses.push_back(terms().make("Access",{terms().make("external:"+resource),number(1)}));
    }
    return terms().make("Accesses",accesses);
}
