#pragma once

#include "machine.hpp"
#include <array>
#include <tuple>

namespace vx::relational {
// Source-specific finite control reasoning. Machine still assigns no meaning
// to these IR constructors. Numeric words are target-independent 32-bit values
// with each native operation applying its declared wrapping width.
class ControlProof {
    using Bit=std::size_t;
    using Word=std::array<Bit,32>;
    struct Node {std::string atom;Bit low,high;};
    Terms generated;
    std::vector<Node> nodes{{"",0,0},{"",1,1}};
    std::map<std::tuple<std::string,Bit,Bit>,Bit> unique;
    std::map<std::tuple<Bit,Bit,Bit>,Bit> cache;
    std::map<std::string,Ref> atoms;
    std::map<std::string,std::optional<Word>> words;
    std::map<std::string,Bit> booleans;
    Bit node(const std::string& atom,Bit low,Bit high);
    Bit atom(const std::string& name,Ref value);
    Bit parity(Bit a,Bit b);
    Bit any(const Word& value);
    Bit equal(const Word& a,const Word& b);
    Bit less(const Word& a,const Word& b);
    Word constant(Integer value);
    Word choose(Bit condition,const Word& yes,const Word& no);
    Word add(const Word& a,const Word& b,unsigned width,bool subtract=false);
    Word wrap(Word value,unsigned width,bool sign);
    std::optional<Word> word(const Ref& value);
    std::optional<Word> expression(const Ref& value);
    std::optional<Word> primitive(const Ref& runtime);
public:
    Bit select(Bit condition,Bit yes,Bit no);
    Bit read(const Ref& value);
    std::optional<Integer> constantValue(const Ref& value);
    Bit conjunction(Bit a,Bit b) {return select(a,b,0);}
    Bit disjunction(Bit a,Bit b) {return select(a,1,b);}
    Bit negate(Bit a) {return select(a,0,1);}
    Bit equivalent(Bit a,Bit b) {return select(a,b,negate(b));}
    Bit exists(Bit value,const std::function<bool(const Ref&)>& remove);
    Bit substitute(Bit value,const std::map<std::string,Bit>& replacements);
    // Rename a fresh full-width word's atomic bits, preserving repeated sign
    // bits. Composite or narrowed/clamped encodings are not fresh variables.
    std::optional<std::map<std::string,Bit>> variableRenaming(const Ref& from,const Ref& to);
    Ref term(Bit value,Terms& terms) const;
    std::size_t size() const {return nodes.size();}
};
}
