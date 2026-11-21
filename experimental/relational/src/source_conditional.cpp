#include "source_conditional.hpp"
#include "source_control_proof.hpp"

namespace vx::relational {
struct ConditionalProofs::Graph {
    ControlProof proof;
    std::size_t accounted=0;
};
ConditionalProofs::Retention ConditionalProofs::proofRetention() const {
    Retention result;
    for(const auto& graph:graphs) if(graph) {
        ++result.graphs;result.nodes+=graph->proof.size();
        result.terms+=graph->proof.termCount();result.operations+=graph->proof.operationCount();
    }
    return result;
}
ConditionalProofs::Active::~Active() {
    // Native interpretation can recursively ask for shadow-body proofs. Never
    // recycle identity numbers while an outer query still holds a key.
    if(--owner.depth==0 && owner.resetPending) {
        owner.answers.clear();owner.identities=TermIndex{};
        owner.resetPending=false;++owner.counts.resets;
    }
}
Ref ConditionalProofs::refine(Terms& terms,const Ref& value,const Ref& premises,const Meaning& interpretation) {
    Active active(*this);++counts.queries;
    std::optional<Key> key;
    if(entryLimit && identityLimit && !resetPending) {
        key=Key{static_cast<bool>(interpretation),identities.intern(value),identities.intern(premises)};
        if(identities.size()>identityLimit) {resetPending=true;key.reset();}
    }
    if(key) if(auto found=answers.find(*key);found!=answers.end()) {
        // The callback can itself use this cache. An immutable lease keeps the
        // entry alive even if such a nested query invalidates another answer.
        const auto answer=found->second;bool valid=true;
        try {
            for(const auto& [input,expected]:answer->interpretations) {
                ++counts.validated;
                if(!identical(interpretation(input),expected)) {valid=false;break;}
            }
        } catch(const ConstructionLimit&) {
            ++counts.invalidated;++counts.misses;++counts.exhausted;
            answers.erase(*key);return value;
        }
        if(valid) {++counts.hits;return answer->value;}
        ++counts.invalidated;answers.erase(*key);
    }
    ++counts.misses;
    const auto slot=depth-1;
    auto graph=slot<graphs.size() ? graphs[slot] : nullptr;
    const bool warm=static_cast<bool>(graph);
    auto fresh=[&] {
        graph=std::make_shared<Graph>();++counts.graphs;
        if(slot<graphs.size()) graphs[slot]=graph;
    };
    if(graph) ++counts.graphReuses;else fresh();
    auto account=[&] {counts.nodes+=graph->proof.size()-graph->accounted;graph->accounted=graph->proof.size();};
    auto release=[&] {if(slot<graphs.size() && graphs[slot]==graph) graphs[slot].reset();};
    Answer answer;bool interpretationExhausted=false;
    auto meaning=[&](const Ref& input) {
        if(!interpretation) return input;
        Ref output;
        try {output=interpretation(input);}
        catch(const ConstructionLimit&) {interpretationExhausted=true;throw;}
        if(key && !resetPending) answer.interpretations.emplace_back(input,output);
        return output;
    };
    auto attempt=[&] {
        auto& proof=graph->proof;
        std::size_t premise=1;
        for(const auto& condition:premises->arguments)
            premise=proof.select(premise,proof.read(meaning(condition)),0);
        std::function<Ref(const Ref&)> narrow=[&](const Ref& input)->Ref {
            if(input->name=="Runtime") {
                if(auto constant=proof.constantValue(meaning(input))) return terms.number(*constant);
                if(input->arguments[1]->kind==Term::Number && input->arguments[1]->number==0 &&
                   input->arguments[2]->kind==Term::Number && input->arguments[2]->number==1) {
                    auto condition=proof.read(meaning(input));
                    if(proof.select(premise,condition,1)==1) return terms.number(1);
                    if(proof.select(premise,condition,0)==0) return terms.number(0);
                }
                const auto& expression=input->arguments[0];
                if(expression->name=="Select") {
                    auto condition=proof.read(meaning(expression->arguments[0]));
                    if(proof.select(premise,condition,1)==1) return narrow(expression->arguments[1]);
                    if(proof.select(premise,condition,0)==0) return narrow(expression->arguments[2]);
                }
                return input;
            }
            if(input->kind!=Term::Constructor) return input;
            std::vector<Ref> children;bool changed=false;
            for(const auto& child:input->arguments) {
                auto next=narrow(child);changed|=next!=child;children.push_back(next);
            }
            return changed ? terms.make(input->name,children) : input;
        };
        answer.value=narrow(value);
    };
    for(unsigned pass=0;;++pass) {
        try {attempt();account();break;}
        catch(const ConstructionLimit&) {
            account();release();
            // Previous subgraphs must not steal the fresh query's node budget.
            // Retry once in isolation; an exhausted interpretation is a
            // different dependency failure, not a full Boolean graph.
            if(!pass && warm && !interpretationExhausted) {
                ++counts.graphRetries;answer=Answer{};fresh();continue;
            }
            ++counts.exhausted;return value;
        }
    }
    // Bounds apply between queries. One attempt keeps its original proof
    // budget; large expression or operation caches do not stay resident.
    if(graph->proof.full() || graph->proof.termCount()>identityLimit || graph->proof.operationCount()>262144) release();
    auto result=answer.value;
    if(key && !resetPending) {
        if(answers.size()>=entryLimit) resetPending=true;
        else answers.emplace(*key,std::make_shared<Answer>(std::move(answer)));
    }
    return result;
}
}
