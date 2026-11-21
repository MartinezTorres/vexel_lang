#pragma once
#include "machine.hpp"
#include <tuple>
#include <unordered_map>

namespace vx::relational {
// Exact structural identities for immutable terms in one index lifetime.
// These are cache keys, never proof facts or persistent/public identifiers.
// Weak observations verify owner lifetime before using an address fast path.
// Constructor arguments are represented by identities rather than expanded
// text, preserving shared DAGs. No hash digest stands in for equality.
class TermIndex {
    using Key=std::tuple<Term::Kind,Id,std::string,std::vector<Id>>;
    std::map<Key,Id> canonical;
    struct Observed {std::weak_ptr<const Term> owner;Id identity;};
    std::unordered_map<const Term*,Observed> observed;
    std::size_t nextSweep=16384;
    std::optional<Id> lookup(const Ref& value);
public:
    Id intern(const Ref& value);
    std::size_t size() const {return canonical.size();}
};
}
