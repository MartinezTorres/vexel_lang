#pragma once

#include "term_index.hpp"
#include <compare>
#include <cstdint>

namespace vx::relational {
// Bounded identities in a native word's modular ring. These prove relationships
// between values; they do not discharge execution, effect or definedness goals.
// A missing certificate leaves the ordinary numeric proof unchanged.
std::optional<Integer> modularConstant(const Ref&,TermIndex&);
bool modularEquivalent(const Ref&,const Ref&,TermIndex&);

// A value class after conversion to the supplied native representation. Atom
// identities belong to the supplied index: compare forms only within that
// index lifetime and one observation scope. This is a proof key, not executable
// code and not evidence that evaluating its operands is safe to discard.
struct ModularForm {
    // A truth test is a distinct atom from the tested integer. In particular,
    // truth(255) is one, not 255, when a conditional becomes a polynomial.
    using Atom=std::pair<Id,bool>;
    unsigned width;
    bool sign;
    std::map<std::vector<Atom>,std::uint32_t> coefficients;
    auto operator<=>(const ModularForm&) const = default;
};
std::optional<ModularForm> modularForm(const Ref&,unsigned width,bool sign,TermIndex&);
// A constant or a single unchanged atom, retaining the root word conversion.
// The caller must separately establish that removed value work is discardable.
std::optional<Ref> modularReduction(Terms&,const Ref&,TermIndex&);
}
