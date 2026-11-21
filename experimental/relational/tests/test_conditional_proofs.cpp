#include "source_conditional.hpp"
#include <iostream>
#include <stdexcept>

using namespace vx::relational;
namespace {
void check(bool value,const char* why) {if(!value) throw std::runtime_error(why);}
bool is(const Ref& value,unsigned n) {return value->kind==Term::Number && value->number==n;}
}
int main() {
    Terms terms;ConditionalProofs cached,uncached(0);
    auto number=[&](unsigned value){return terms.number(value);};
    auto runtime=[&](Ref expression,unsigned maximum=255){return terms.make("Runtime",{expression,number(0),number(maximum)});};
    auto x=runtime(terms.make("Input",{terms.make("x")}));
    auto guard=runtime(terms.make("Compare",{terms.make("<"),x,number(128)}),1);
    auto none=terms.make("Premises");
    auto yes=terms.make("Premises",{guard});
    auto no=terms.make("Premises",{runtime(terms.make("Not",{guard}),1)});
    for(unsigned i=0;i<4;++i) {
        check(is(cached.refine(terms,guard,yes),1),"true branch fact lost");
        check(is(cached.refine(terms,guard,no),0),"false branch fact lost");
        check(identical(cached.refine(terms,guard,none),guard),"branch fact leaked into unguarded query");
        check(identical(cached.refine(terms,guard,yes),uncached.refine(terms,guard,yes)),"cached proof differs from fresh proof");
    }
    check(cached.stats().hits>0,"repeated proof was not reused");
    check(cached.stats().graphReuses>0 && cached.stats().nodes<uncached.stats().nodes,"different queries did not share Boolean subgraphs");

    auto call=runtime(terms.make("CallValue",{number(5),number(0)}));
    unsigned current=7,calls=0;bool available=true,exhaust=false;
    ConditionalProofs::Meaning meaning=[&](const Ref& input)->Ref {
        ++calls;if(exhaust) throw ConstructionLimit("interpretation budget exhausted");
        return available && identical(input,call) ? runtime(number(current)) : input;
    };
    check(is(cached.refine(terms,call,none,meaning),7),"native interpretation not used");
    auto before=calls;check(is(cached.refine(terms,call,none,meaning),7),"same native proof changed");
    check(calls>before,"cached proof skipped dependency validation");
    current=9;check(is(cached.refine(terms,call,none,meaning),9),"changed interpretation reused a stale answer");
    available=false;check(identical(cached.refine(terms,call,none,meaning),call),"removed interpretation retained a constant");
    available=true;current=11;check(is(cached.refine(terms,call,none,meaning),11),"new native fact was ignored");
    check(identical(cached.refine(terms,call,none),call),"native facts leaked into shadow mode");
    exhaust=true;check(identical(cached.refine(terms,call,none,meaning),call),"exhausted revalidation did not keep residual value");
    exhaust=false;check(is(cached.refine(terms,call,none,meaning),11),"exhaustion poisoned later proof");
    check(cached.stats().invalidated>=3,"changed dependencies were not invalidated");

    // Dependency interpretation can ask for nested shadow proofs and cross a
    // cache limit. The outer key must not acquire a recycled identity.
    ConditionalProofs tiny(2,64);
    ConditionalProofs::Meaning nested=[&](const Ref& input)->Ref {
        for(unsigned n=0;n<5;++n) {
            auto value=runtime(terms.make("Arithmetic",{terms.make("+"),number(8),number(n),number(1)}));
            check(is(tiny.refine(terms,value,none),n+1),"nested query changed value");
        }
        return identical(input,call) ? runtime(number(current)) : input;
    };
    for(unsigned n=1;n<8;++n) {
        current=n;check(is(tiny.refine(terms,call,none,nested),n),"nested eviction recycled an outer key");
        check(tiny.retained()<=2 && tiny.keys()<=64,"retained cache exceeded its limits");
    }
    check(tiny.stats().resets>0,"nested limit did not reset cache");
    ConditionalProofs smallKeys(8,1);
    check(is(smallKeys.refine(terms,guard,yes),1),"identity limit changed proof");
    check(smallKeys.keys()==0 && smallKeys.retained()==0,"oversized identity graph retained");
    check(smallKeys.proofRetention().graphs==0,"oversized Boolean expression cache retained");

    // Each ten-bit product fits a fresh graph. Retaining distinct products
    // eventually consumes its capacity: retrying must preserve the next
    // query's proof under its own premise. Compact representations may retain
    // more than two products before reaching the same physical node limit.
    ConditionalProofs warmBudget;
    for(unsigned i=0;i<8 && warmBudget.stats().graphRetries==0;++i) {
        auto left=runtime(terms.make("Input",{terms.make("left"),number(i)}),1023);
        auto right=runtime(terms.make("Input",{terms.make("right"),number(i)}),1023);
        auto product=runtime(terms.make("Arithmetic",{terms.make("*"),number(10),left,right}),1023);
        auto condition=runtime(terms.make("Compare",{terms.make("<"),product,number(512)}),1);
        check(is(warmBudget.refine(terms,condition,terms.make("Premises",{condition})),1),"retained work stole a fresh query's proof budget");
    }
    check(warmBudget.stats().graphRetries>0 && warmBudget.stats().exhausted==0,"fresh-graph budget retry was not exercised");

    auto a=runtime(terms.make("Input",{terms.make("a")}),0xffffffffU);
    auto b=runtime(terms.make("Input",{terms.make("b")}),0xffffffffU);
    auto product=runtime(terms.make("Arithmetic",{terms.make("*"),number(32),a,b}),0xffffffffU);
    ConditionalProofs limited;
    for(unsigned n=0;n<2;++n) check(identical(limited.refine(terms,product,none),product),"exhausted proof fabricated an answer");
    check(limited.stats().exhausted==2 && limited.stats().hits==0 && limited.retained()==0,"unfinished proof was cached");
    std::cout<<"Conditional proof reuse: branch isolation, exact interpretations, nested eviction and exhaustion passed\n";
}
