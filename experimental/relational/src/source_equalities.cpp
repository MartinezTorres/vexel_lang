#include "source_demand.hpp"
#include "source_algebra.hpp"
#include "source_numeric.hpp"
#include <algorithm>
#include <array>
#include <variant>

namespace vx::relational {
namespace {
std::vector<Ref> flatten(const Ref& value) {
    std::vector<Ref> out;
    std::function<void(const Ref&)> visit=[&](const Ref& v) {
        if(v->name=="Values") for(const auto& child:v->arguments) visit(child);
        else if(v->name=="RecordValue") visit(v->arguments.at(1));
        else out.push_back(v);
    };visit(value);return out;
}
// Classes belong to one function return or one loop clock. Polynomial keys
// prove values equal after their declared conversion; they do not move a
// computation or a snapshot across that observation boundary.
struct Partition {
    struct Group {std::vector<std::size_t> members;};
    std::vector<Group> groups;
    TermIndex identities;
    using Part=std::variant<Id,ModularForm>;
    std::map<std::vector<Part>,std::size_t> classes;
    void add(std::size_t member,const Ref& type,const std::vector<Ref>& values) {
        std::vector<Part> key{identities.intern(type)};
        auto format=scalarFormat(type);
        for(const auto& value:values) {
            if(format && (format->kind==ScalarFormat::IntegerKind || format->kind==ScalarFormat::Bool))
                if(auto form=modularForm(value,format->bits,format->sign,identities)) {
                    key.emplace_back(std::move(*form));continue;
                }
            key.emplace_back(identities.intern(value));
        }
        auto [found,inserted]=classes.try_emplace(std::move(key),groups.size());
        if(inserted) groups.push_back({{member}});
        else groups[found->second].members.push_back(member);
    }
};
}

bool InterfaceDemand::resolveEqualities() {
    bool changed=false;
    auto converted=[&](const Ref& type,const Ref& value) {
        return terms_.make(scalarSigned(type) ? "SignedCast" : "Cast",{terms_.number(scalarBits(type)),value});
    };
    // A return and an output capture can name the same value. This equality
    // belongs to one completed invocation; it never aliases two call sites or
    // replaces a snapshot with a later read of the actual argument.
    for(auto& [id,fn]:functions_) {
        Partition partition;bool joined=false;
        for(std::size_t i=0;i<fn.results.size();++i) {
            auto value=normalize(converted(fn.resultTypes[i],fn.results[i]),id);
            if(discardable(value)) partition.add(i,fn.resultTypes[i],{value});
        }
        auto representative=[&](std::size_t i){while(fn.resultAliases.contains(i)) i=fn.resultAliases.at(i);return i;};
        for(const auto& group:partition.groups) for(auto i:group.members) {
            auto left=representative(group.members.front()),right=representative(i);
            if(left!=right) {
                fn.resultAliases.emplace(std::max(left,right),std::min(left,right));joined=true;
            }
        }
        if(joined) {changed=true;normalized_.clear();}
    }

    struct Carried {Leaf key;Owner owner;Ref type;std::array<Ref,3> transfers;};
    std::vector<Carried> carried;std::vector<std::vector<std::size_t>> groups;
    // Entry equality is the anchor. Compare only initialized, total, equally
    // typed leaves within one loop's state. A snapshot from an enclosing loop
    // or call can anchor equality here without being globally immutable.
    for(const auto& [id,loop]:loops_) {
        const auto& a=loop.trace->arguments;
        std::array<std::vector<Ref>,3> updates={flatten(a[1]),flatten(a[4]),flatten(a[6])};
        Partition partition;std::size_t at=0;
        for(const auto& slot:loop.slots) for(std::size_t i=0;i<slot.count;++i,++at) {
            Leaf key={id,slot.root,i};
            if(!ready_.contains({id,slot.root}) || invariants_.contains(key) || loopAliases_.contains(key)) continue;
            Carried candidate{key,loop.owner,a[10]->arguments.at(at),{}};
            for(std::size_t update=0;update<3;++update) candidate.transfers[update]=converted(candidate.type,updates[update].at(at));
            if(!std::all_of(candidate.transfers.begin(),candidate.transfers.end(),discardable)) continue;
            partition.add(carried.size(),candidate.type,{normalize(candidate.transfers[0],loop.owner)});
            carried.push_back(std::move(candidate));
        }
        for(auto& group:partition.groups) if(group.members.size()>1) groups.push_back(std::move(group.members));
    }
    const auto established=loopAliases_;
    auto install=[&] {
        loopAliases_=established;
        for(const auto& group:groups) {
            auto representative=carried[group.front()].key;
            for(auto i:group) representative=std::min(representative,carried[i].key);
            for(auto i:group) if(carried[i].key!=representative) loopAliases_.emplace(carried[i].key,representative);
        }
        normalized_.clear();
    };
    install();
    // Refine simultaneous hypotheses until every transfer preserves each
    // class. A split can invalidate another class, including in a nested loop,
    // so recheck all remaining classes. Classes only split: this terminates.
    for(bool split=true;split;) {
        split=false;std::vector<std::vector<std::size_t>> next;
        for(const auto& group:groups) {
            Partition partition;
            for(auto i:group) {
                const auto& leaf=carried[i];std::vector<Ref> signature;
                for(const auto& transfer:leaf.transfers) signature.push_back(normalize(transfer,leaf.owner));
                partition.add(i,leaf.type,signature);
            }
            if(partition.groups.size()>1) split=true;
            for(auto& part:partition.groups) if(part.members.size()>1) next.push_back(std::move(part.members));
        }
        groups=std::move(next);
        if(split) install();
    }
    if(loopAliases_.size()>established.size()) changed=true;
    return changed;
}
}
