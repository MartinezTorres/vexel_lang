#include "term_index.hpp"
#include "source_demand.hpp"
#include <iostream>
#include <new>
#include <stdexcept>

using namespace vx::relational;
namespace {
void check(bool value,const char* detail) {if(!value) throw std::runtime_error(detail);}
}
int main() {
    Terms terms;TermIndex index;
    auto a=terms.make("Pair",{terms.number(7),terms.make("UInt",{terms.number(8)})});
    auto b=terms.make("Pair",{terms.number(7),terms.make("UInt",{terms.number(8)})});
    check(a!=b && index.intern(a)==index.intern(b),"separate structural copies differ");
    check(index.intern(terms.number(7))!=index.intern(terms.make("7")),"number/name collision");
    check(index.intern(terms.make("X(Y)"))!=index.intern(terms.make("X",{terms.make("Y")})),"text punctuation collision");
    Integer large=Integer(1)<<200;
    check(index.intern(terms.number(large))!=index.intern(terms.number(-large)),"large integer sign collision");
    auto x=terms.variable("before"),y=terms.variable("before");
    auto renamed=std::make_shared<Term>(*x);renamed->name="after";
    check(index.intern(x)==index.intern(renamed),"debug name changed variable identity");
    check(index.intern(x)!=index.intern(y),"distinct variables collapsed");
    auto open=terms.make("Box",{x});Bindings bindings;bindings.unify(x,terms.number(7));
    check(index.intern(open)!=index.intern(bindings.resolve(open)),"binding state mutated a structural key");
    // A DAG with exponentially many tree paths must need only linear keys.
    TermIndex shared;auto left=terms.number(1),right=terms.number(1),different=terms.number(2);
    for(unsigned i=0;i<80;++i) {
        left=terms.make("Fork",{left,left});right=terms.make("Fork",{right,right});
        different=terms.make("Fork",{different,different});
    }
    check(shared.intern(left)==shared.intern(right),"equal shared DAGs differ");
    check(shared.size()==81,"shared DAG expanded into tree keys");
    check(shared.intern(left)!=shared.intern(different),"deep changed leaf was lost");
    {
        auto pure=terms.make("Input",{terms.make("value")});
        auto trapping=terms.make("Arithmetic",{terms.make("/"),terms.number(32),terms.number(1),pure});
        for(unsigned i=0;i<60;++i) {
            pure=terms.make("Arithmetic",{terms.make("+"),terms.number(32),pure,pure});
            trapping=terms.make("Arithmetic",{terms.make("+"),terms.number(32),trapping,trapping});
        }
        check(InterfaceDemand::discardable(pure),"shared pure value was not discardable");
        check(!InterfaceDemand::discardable(trapping),"shared possibly trapping value lost its evaluation");
        check(InterfaceDemand::discardable(pure),"a different value query contaminated purity");
    }
    auto copy=shared;
    check(copy.intern(left)==shared.intern(left),"index copy changed existing identity");
    std::weak_ptr<const Term> lifetime;
    {TermIndex local;auto temporary=terms.make("temporary");lifetime=temporary;local.intern(temporary);
     temporary.reset();check(lifetime.expired(),"address cache retained a transient term");}
    check(lifetime.expired(),"index leaked its term lifetime");
    {
        // Reuse one exact address with a new shared ownership control block.
        // The old weak observation must never supply the new term's key.
        TermIndex local;alignas(Term) unsigned char memory[sizeof(Term)];
        auto create=[&](const char* name) {
            auto value=new(memory) Term;value->kind=Term::Constructor;value->closed=true;value->name=name;
            return Ref(value,[](const Term* term){term->~Term();});
        };
        auto first=create("first");const auto firstKey=local.intern(first);
        lifetime=first;first.reset();check(lifetime.expired(),"arena term is still owned before reuse");
        auto second=create("second");check(local.intern(second)!=firstKey,"recycled address reused an expired identity");
        check(local.intern(terms.make("first"))==firstKey,"released allocation lost its canonical identity");
    }
    std::cout<<"Structural term keys: exact kinds, integers, variables, binding revisions and shared DAGs passed\n";
}
