#pragma once

#include "term_index.hpp"

namespace vx::relational {
// Reuse optional value proofs within one source-input revision. Interpretations
// are proof inputs, never cached runtime executions. A dependency is the exact
// immutable value returned by the supplied interpretation callback.
class ConditionalProofs {
public:
    using Meaning=std::function<Ref(const Ref&)>;
    struct Stats {
        std::size_t queries=0,hits=0,misses=0,invalidated=0,validated=0;
        std::size_t nodes=0,exhausted=0,resets=0;
        std::size_t graphs=0,graphReuses=0,graphRetries=0;
    };
    struct Retention {std::size_t graphs=0,nodes=0,terms=0,operations=0;};
    explicit ConditionalProofs(std::size_t entries=4096,std::size_t identityCapacity=65536,std::size_t graphSlots=2)
        :entryLimit(entries),identityLimit(identityCapacity),graphs(entries && identityCapacity ? graphSlots : 0) {}
    Ref refine(Terms&,const Ref& value,const Ref& premises,const Meaning& meaning={});
    const Stats& stats() const {return counts;}
    std::size_t retained() const {return answers.size();}
    std::size_t keys() const {return identities.size();}
    Retention proofRetention() const;
private:
    using Key=std::tuple<bool,Id,Id>;
    struct Answer {
        Ref value;
        std::vector<std::pair<Ref,Ref>> interpretations;
    };
    struct Active {
        ConditionalProofs& owner;
        explicit Active(ConditionalProofs& at):owner(at) {++owner.depth;}
        ~Active();
    };
    std::size_t entryLimit,identityLimit,depth=0;
    bool resetPending=false;
    TermIndex identities;
    std::map<Key,std::shared_ptr<const Answer>> answers;
    struct Graph;
    // Nested interpretations use another depth slot. A live query never has
    // its node identities recycled by a nested proof or a retained-graph reset.
    std::vector<std::shared_ptr<Graph>> graphs;
    Stats counts;
};
}
