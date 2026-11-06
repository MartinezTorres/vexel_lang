#include "source_observation.hpp"
#include <algorithm>

namespace vx::relational {
std::optional<std::vector<Ref>> closeSourceObservation(Terms& arena,const std::vector<Ref>& observations,
    const std::map<std::string,Ref>& artifacts,const std::vector<std::string>& observableArtifacts,const OutputProjection& frameOutput) {
    auto terms=[&arena]()->Terms& {return arena;};
    auto number=[&arena](Id n){return arena.number(n);};
    // A call's completion is observed even if its result is discarded. Its
    // unused return leaves are not. Close demanded native slots transitively
    // through recursive calls before constructing the comparison graph.
    std::map<Id,Ref> producers;
    std::set<const Term*> indexed;
    std::function<void(const Ref&)> index=[&](const Ref& term) {
        if(!indexed.insert(term.get()).second) return;
        if(term->name=="Invoke" && term->arguments.size()==5)
            producers.emplace(term->arguments[0]->number.convert_to<Id>(),term);
        for(const auto& child:term->arguments) index(child);
    };
    for(const auto& [name,artifact]:artifacts) {(void)name;index(artifact);}
    for(const auto& value:observations) index(value);
    bool complete=true;
    std::map<Id,std::set<Id>> demandedOutputs;
    std::set<std::string> active;
    std::vector<Ref> work=observations;
    auto activate=[&](const std::string& name,bool frame) {
        if(!active.insert(name).second) return;
        auto found=artifacts.find(name);if(found==artifacts.end()) {complete=false;return;}
        if(frame) work.push_back(found->second->arguments[4]);
        else work.push_back(found->second);
    };
    for(const auto& name:observableArtifacts) activate(name,false);
    std::set<const Term*> scanned;
    std::function<void(const Ref&)> scan=[&](const Ref& term) {
        if(!scanned.insert(term.get()).second) return;
        if(term->name=="Invoke" && term->arguments.size()==5)
            activate("runtime_frame_"+show(term->arguments[1]),true);
        else if(term->name=="Load" && term->arguments.size()==3)
            activate("storage_"+show(term->arguments[1]),false);
        else if(term->name=="Store" && (term->arguments.size()==3 || term->arguments.size()==4))
            activate("storage_"+show(term->arguments[0]),false);
        else if(term->name=="CallValue" && term->arguments.size()==2) {
            auto producer=producers.find(term->arguments[0]->number.convert_to<Id>());
            if(producer!=producers.end()) {
                Id frame=producer->second->arguments[1]->number.convert_to<Id>(),output=term->arguments[1]->number.convert_to<Id>();
                auto name="runtime_frame_"+std::to_string(frame);activate(name,true);
                if(demandedOutputs[frame].insert(output).second) {
                    auto found=artifacts.find(name);
                    if(found==artifacts.end()) complete=false;
                    else if(auto leaf=frameOutput(found->second,output)) work.push_back(leaf->value);
                    else complete=false;
                }
            }
        }
        for(const auto& child:term->arguments) scan(child);
    };
    for(std::size_t i=0;i<work.size();++i) {auto term=work[i];scan(term);}
    if(!complete) return {};
    struct Locals {
        std::map<Id,Id> temporaries,loops;
        std::map<std::pair<Id,Id>,Id> slots;
        std::map<const Term*,Ref> normalized;
    };
    std::map<std::pair<std::string,Id>,Id> references;
    std::vector<std::pair<std::string,Id>> queue;
    auto identity=[&](const Ref& value,std::map<Id,Id>& names) {
        if(value->kind!=Term::Number || value->number<0) {complete=false;return value;}
        Id old=value->number.convert_to<Id>();
        auto [at,inserted]=names.emplace(old,names.size());(void)inserted;return number(at->second);
    };
    auto reference=[&](const std::string& kind,const Ref& value) {
        if(value->kind!=Term::Number || value->number<0) {complete=false;return value;}
        auto key=std::make_pair(kind,value->number.convert_to<Id>());
        auto [at,inserted]=references.emplace(key,references.size());
        if(inserted) queue.push_back(key);
        return number(at->second);
    };
    auto slot=[&](const Ref& loop,const Ref& value,Locals& local) {
        if(loop->kind!=Term::Number || value->kind!=Term::Number) {complete=false;return value;}
        auto key=std::make_pair(loop->number.convert_to<Id>(),value->number.convert_to<Id>());
        auto [at,inserted]=local.slots.emplace(key,local.slots.size());(void)inserted;return number(at->second);
    };
    std::function<Ref(const Ref&,Locals&)> visit=[&](const Ref& value,Locals& local)->Ref {
        if(value->kind!=Term::Constructor || value->arguments.empty()) return value;
        if(auto found=local.normalized.find(value.get());found!=local.normalized.end()) return found->second;
        auto children=value->arguments;auto name=value->name;
        if(name=="Function" && children.size()==9) {
            std::vector<Ref> selected;
            for(Id output:demandedOutputs[children[0]->number.convert_to<Id>()]) {
                auto leaf=frameOutput(value,output);
                if(!leaf) {complete=false;return value;}
                selected.push_back(terms().make("Result",{leaf->key,leaf->type,leaf->value}));
            }
            // Unused signature facets are not observations either. Operation
            // terms and demanded leaves retain the types that affect behavior.
            children={children[0],children[4],terms().make("Results",selected)};
            name="FunctionObservation";
        }
        // Only these numeric positions are adapter-local identities. Widths,
        // constants, ABI indices and nominal identities retain their meaning.
        std::set<std::size_t> replaced;
        auto replace=[&](std::size_t at,Ref part){children[at]=std::move(part);replaced.insert(at);};
        if((name=="Invoke" && children.size()==5) || (name=="Load" && children.size()==3)) {
            replace(0,identity(children[0],local.temporaries));
            replace(1,reference(name=="Invoke" ? "runtime_frame_" : "storage_",children[1]));
            // The caller's backing-buffer size is a realization detail.
            if(name=="Invoke") replace(4,terms().make("OutputStorage"));
        } else if((name=="CallValue" && children.size()==2) || (name=="ExternalCall" && children.size()==4)) {
            if(name=="CallValue") {
                auto producer=producers.find(children[0]->number.convert_to<Id>());
                if(producer!=producers.end()) {
                    auto found=artifacts.find("runtime_frame_"+show(producer->second->arguments[1]));
                    if(found==artifacts.end()) complete=false;
                    else if(auto leaf=frameOutput(found->second,children[1]->number.convert_to<Id>())) replace(1,leaf->key);
                    else complete=false;
                }
            }
            replace(0,identity(children[0],local.temporaries));
        } else if(name=="Store" && (children.size()==3 || children.size()==4)) replace(0,reference("storage_",children[0]));
        else if((name=="FunctionObservation" && children.size()==3) || (name=="Storage" && children.size()==6))
            replace(0,reference(name=="FunctionObservation" ? "runtime_frame_" : "storage_",children[0]));
        else if(name=="Loop" && children.size()==15) {
            auto original=children[0];replace(0,identity(original,local.loops));
            std::vector<Ref> descriptors;
            for(const auto& field:children[11]->arguments)
                descriptors.push_back(terms().make("Slot",{slot(original,field->arguments[0],local),field->arguments[1]}));
            replace(11,terms().make("Slots",descriptors));
        } else if((name=="LoopSlotValue" && children.size()==3) || (name=="LoopReady" && children.size()==2)) {
            auto original=children[0];replace(0,identity(original,local.loops));replace(1,slot(original,children[1],local));
        } else if((name=="LoopIndex" && children.size()==1) ||
                  ((name=="LoopValue" || name=="LoopFlag" || name=="LoopReturned") && children.size()==2))
            replace(0,identity(children[0],local.loops));
        for(std::size_t i=0;i<children.size();++i) if(!replaced.contains(i)) children[i]=visit(children[i],local);
        auto result=terms().make(name,std::move(children));local.normalized.emplace(value.get(),result);return result;
    };
    Locals rootLocals;std::vector<Ref> values,traces,definitions;
    for(const auto& root:observations) values.push_back(visit(root,rootLocals));
    // Empty and NoValue results still have observable execution. Its presence
    // must not depend on whether the output happens to have a scalar leaf.
    for(const auto& name:observableArtifacts) {
        auto found=artifacts.find(name);if(found==artifacts.end()) return {};
        traces.push_back(visit(found->second,rootLocals));
    }
    for(std::size_t i=0;i<queue.size();++i) {
        const auto [kind,id]=queue[i];auto found=artifacts.find(kind+std::to_string(id));
        if(found==artifacts.end()) return {};
        Locals local;definitions.push_back(visit(found->second,local));
    }
    if(!complete) return {};
    return std::vector<Ref>{terms().make("Observation",{terms().make("Values",values),
        terms().make("Execution",traces),terms().make("Definitions",definitions)})};
}
}
