#include "term_index.hpp"
#include <algorithm>

namespace vx::relational {
std::optional<Id> TermIndex::lookup(const Ref& value) {
    auto found=observed.find(value.get());
    if(found==observed.end()) return {};
    if(!found->second.owner.expired()) return found->second.identity;
    observed.erase(found);return {};
}
Id TermIndex::intern(const Ref& value) {
    if(auto found=lookup(value)) return *found;
    // Canonical keys own exact structure, not its original allocation. Drop
    // expired address observations at an amortized growth threshold so weak
    // control blocks do not retain every transient allocation indefinitely.
    if(observed.size()>=nextSweep) {
        std::erase_if(observed,[](const auto& item){return item.second.owner.expired();});
        nextSweep=observed.size()+std::max<std::size_t>(16384,observed.size()/2);
    }
    // Explicit postorder also handles deeply nested terms without consuming
    // the host call stack. A repeated child is processed only once.
    std::vector<std::pair<Ref,bool>> pending{{value,false}};
    while(!pending.empty()) {
        auto [term,expanded]=std::move(pending.back());pending.pop_back();
        if(lookup(term)) continue;
        if(!expanded && !term->arguments.empty()) {
            pending.emplace_back(term,true);
            for(auto it=term->arguments.rbegin();it!=term->arguments.rend();++it)
                if(!lookup(*it)) pending.emplace_back(*it,false);
            continue;
        }
        std::vector<Id> arguments;arguments.reserve(term->arguments.size());
        for(const auto& argument:term->arguments) arguments.push_back(observed.at(argument.get()).identity);
        // Variable debug names are not part of identity, matching identical().
        Key key{term->kind,term->kind==Term::Variable ? term->variable : 0,
            term->kind==Term::Number ? term->number.str() : term->kind==Term::Constructor ? term->name : "",
            std::move(arguments)};
        auto [entry,inserted]=canonical.try_emplace(std::move(key),canonical.size());
        (void)inserted;observed.insert_or_assign(term.get(),Observed{term,entry->second});
    }
    return observed.at(value.get()).identity;
}
}
