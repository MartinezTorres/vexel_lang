// Native ABI indices map back to logical leaves for semantic observation.
// The same capture can follow differently shaped unused return values.
std::optional<std::vector<Ref>> Session::closeObservation(const std::vector<Ref>& observations,
                                                        const std::map<std::string,Ref>& artifacts) {
    auto output=[this](const Ref& frame,Id requested)->std::optional<ObservedOutput> {
        Id offset=0;
        for(const auto& leaf:leaves(frame->arguments[3],frame->arguments[5])) if(nativeScalar(leaf.type)) {
            if(offset++==requested) return ObservedOutput{leaf.type,leaf.value,terms().make("Return",{terms().make(leaf.path)})};
        }
        offset=std::max(Id(1),offset);
        const auto& types=frame->arguments[2]->arguments;
        for(std::size_t i=0;i<types.size();++i) if(frame->arguments[8]->arguments[i]->number!=0)
            for(const auto& leaf:leaves(types[i],frame->arguments[6]->arguments[i])) if(nativeScalar(leaf.type)) {
                if(offset++==requested) return ObservedOutput{leaf.type,leaf.value,terms().make("Capture",{number(i),terms().make(leaf.path)})};
            }
        return {};
    };
    return closeSourceObservation(terms(),observations,artifacts,observableArtifacts,output);
}
