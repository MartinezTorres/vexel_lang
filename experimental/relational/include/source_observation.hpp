#pragma once
#include "machine.hpp"

namespace vx::relational {
struct ObservedOutput {Ref type,value,key;};
using OutputProjection=std::function<std::optional<ObservedOutput>(const Ref&,Id)>;

// The source adapter supplies native-slot decomposition; the graph closure
// reads only completed candidate terms and never the solver's binding store.
std::optional<std::vector<Ref>> closeSourceObservation(Terms& terms,
    const std::vector<Ref>& observations,const std::map<std::string,Ref>& artifacts,
    const std::vector<std::string>& observableArtifacts,const OutputProjection& frameOutput);
}
