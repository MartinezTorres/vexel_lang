#pragma once

#include "machine.hpp"
#include "term_index.hpp"
#include <array>
#include <cstdint>
#include <tuple>

namespace vx::relational {
// Source-specific finite control reasoning. Machine still assigns no meaning
// to these IR constructors. Numeric words are target-independent 32-bit values
// with each native operation applying its declared wrapping width.
class ControlProof {
    using Bit=std::size_t;
    using Word=std::array<Bit,32>;
    static constexpr std::size_t nodeLimit=100000;
    using Code=std::uint32_t;
    using Key=std::array<Code,3>;
    static_assert(2*nodeLimit<~Code(0));
    // Append-only exact memo tables. Entries are integers owned by this proof;
    // contiguous storage avoids a heap allocation for every BDD/cache node.
    // Hash collisions always compare the complete triple.
    class Memo {
        // The proof's node limit bounds node and atom identities alike. Keep
        // public handles unchanged while halving each stored exact quadruple.
        static constexpr Code empty=~Code(0);
        struct Entry {Key key{};Code value=empty;};
        std::vector<Entry> entries;
        std::size_t count=0;
        static std::size_t hash(const Key& key);
        Bit insert(const Key& key,Bit value);
    public:
        std::optional<Bit> find(const Key& key) const;
        Bit emplace(const Key& key,Bit value);
        std::size_t size() const {return count;}
    };
    struct Atom {Ref source,value;std::string prefix,display;unsigned bit;bool complete;};
    struct Node {std::size_t atom;Bit low,high;};
    Terms generated;
    // A handle is (node index << 1) | complement. Terminals 0/1 share
    // index zero; every stored nonterminal has an uncomplemented low edge.
    std::vector<Node> nodes{{0,0,0}};
    Node branch(Bit value) const {
        auto result=nodes.at(value>>1);
        result.low^=value&1;result.high^=value&1;return result;
    }
    Memo unique,cache;
    std::map<std::pair<Id,unsigned>,std::size_t> atomIds;
    std::vector<Atom> atoms;
    TermIndex identities;
    std::map<Id,std::optional<Word>> words;
    std::map<Id,Bit> booleans;
    Bit node(std::size_t atom,Bit low,Bit high);
    Bit atom(Ref source,Ref value,unsigned bit=32);
    bool atomLess(std::size_t first,std::size_t second) const;
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
    std::optional<Bit> choiceComparison(const Ref& value);
    std::optional<Integer> alternativeConstant(const Ref& value);
    std::optional<Word> primitive(const Ref& runtime);
public:
    // Keys are primitive positive bit handles from this proof, not printed
    // names. A renaming has the same lifetime and scope as its source graph.
    using Renaming=std::map<Bit,Bit>;
    Bit select(Bit condition,Bit yes,Bit no);
    Bit read(const Ref& value);
    std::optional<Integer> constantValue(const Ref& value);
    Bit conjunction(Bit a,Bit b) {return select(a,b,0);}
    Bit disjunction(Bit a,Bit b) {return select(a,1,b);}
    Bit negate(Bit a) {return a^1;}
    Bit equivalent(Bit a,Bit b) {
        if(a==b) return 1;
        if(a<2) return a ? b : negate(b);
        if(b<2) return b ? a : negate(a);
        return select(a,b,negate(b));
    }
    Bit exists(Bit value,const std::function<bool(const Ref&)>& remove);
    Bit substitute(Bit value,const Renaming& replacements);
    // Rename a fresh full-width word's atomic bits, preserving repeated sign
    // bits. Composite or narrowed/clamped encodings are not fresh variables.
    std::optional<Renaming> variableRenaming(const Ref& from,const Ref& to);
    Ref term(Bit value,Terms& terms) const;
    std::size_t size() const {return nodes.size();}
    std::size_t termCount() const {return identities.size();}
    std::size_t operationCount() const {return cache.size();}
    bool full() const {return nodes.size()>=nodeLimit;}
};
}
