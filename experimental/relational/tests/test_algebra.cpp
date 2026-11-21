#include "source_algebra.hpp"
#include "source_control_proof.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace vx::relational;
namespace {
void check(bool value,const char* why) {if(!value) throw std::runtime_error(why);}
std::int64_t wrapped(std::uint64_t value,unsigned width,bool sign) {
    const auto modulus=std::uint64_t(1)<<width;value&=modulus-1;
    return sign && value>=(modulus>>1) ? static_cast<std::int64_t>(value)-static_cast<std::int64_t>(modulus) : static_cast<std::int64_t>(value);
}
std::int64_t evaluate(const Ref& value,unsigned x,unsigned y) {
    if(value->kind==Term::Number) return value->number.convert_to<std::int64_t>();
    const auto& a=value->arguments;
    if(value->name=="Runtime") return evaluate(a[0],x,y);
    if(value->name=="X") return x;
    if(value->name=="Y") return y;
    if(value->name=="Select") return evaluate(a[0],x,y)!=0 ? evaluate(a[1],x,y) : evaluate(a[2],x,y);
    if(value->name=="Not") return evaluate(a[0],x,y)==0;
    if(value->name=="And") return evaluate(a[0],x,y)!=0 && evaluate(a[1],x,y)!=0;
    if(value->name=="Cast" || value->name=="SignedCast")
        return wrapped(static_cast<std::uint64_t>(evaluate(a[1],x,y)),a[0]->number.convert_to<unsigned>(),value->name=="SignedCast");
    if(value->name=="Arithmetic" || value->name=="SignedArithmetic") {
        auto left=static_cast<std::uint64_t>(evaluate(a[2],x,y)),right=static_cast<std::uint64_t>(evaluate(a[3],x,y));
        auto result=a[0]->name=="+" ? left+right : a[0]->name=="-" ? left-right : left*right;
        return wrapped(result,a[1]->number.convert_to<unsigned>(),value->name=="SignedArithmetic");
    }
    throw std::runtime_error("oracle does not know expression");
}
}
int main() {
    Terms terms;TermIndex identities;
    auto number=[&](unsigned n){return terms.number(n);};
    auto runtime=[&](Ref expression,unsigned width,bool sign=false) {
        return terms.make("Runtime",{expression,terms.number(sign ? -(Integer(1)<<(width-1)) : Integer(0)),terms.number((Integer(1)<<(width-(sign ? 1 : 0)))-1)});
    };
    auto input=[&](const char* name,unsigned width){return runtime(terms.make(name),width);};
    auto arithmetic=[&](const char* op,unsigned width,Ref left,Ref right,bool sign=false) {
        return runtime(terms.make(sign ? "SignedArithmetic" : "Arithmetic",{terms.make(op),number(width),left,right}),width,sign);
    };
    auto cast=[&](unsigned width,Ref value,bool sign=false){return runtime(terms.make(sign ? "SignedCast" : "Cast",{number(width),value}),width,sign);};
    std::vector<std::pair<Ref,Ref>> identitiesToCheck;
    for(unsigned width:{8u,16u,32u}) {
        auto x=input("X",width),y=input("Y",width);
        auto xy=arithmetic("*",width,x,y),yx=arithmetic("*",width,y,x);
        check(modularEquivalent(xy,yx,identities),"commutative native product not proved");
        auto zero=arithmetic("-",width,xy,yx);
        auto constant=modularConstant(zero,identities);
        check(constant && *constant==0,"commutative products did not cancel");
        ControlProof proof;
        check(proof.constantValue(zero)==Integer(0),"control proof did not consume modular constant");
        auto distributed=arithmetic("*",width,x,arithmetic("+",width,y,number(7)));
        auto expanded=arithmetic("+",width,xy,arithmetic("*",width,x,number(7)));
        check(modularEquivalent(distributed,expanded,identities),"distributive identity not proved");
        identitiesToCheck.emplace_back(xy,yx);identitiesToCheck.emplace_back(distributed,expanded);
        auto signedZero=arithmetic("-",width,cast(width,xy,true),cast(width,yx,true),true);
        check(modularConstant(signedZero,identities)==Integer(0),"signed wrapping identity not proved");
        identitiesToCheck.emplace_back(signedZero,number(0));
    }
    auto x=input("X",8),y=input("Y",8);
    auto select=[&](Ref condition,Ref yes,Ref no){return runtime(terms.make("Select",{condition,yes,no}),8);};
    auto first=select(x,arithmetic("+",8,arithmetic("+",8,x,number(2)),number(1)),y);
    auto second=select(x,arithmetic("+",8,number(1),arithmetic("+",8,x,number(2))),y);
    check(modularEquivalent(first,second,identities),"conditional arithmetic classes did not agree");
    identitiesToCheck.emplace_back(first,second);
    auto reversed=select(terms.make("Not",{x}),y,arithmetic("+",8,x,number(3)));
    check(modularEquivalent(first,reversed,identities),"complemented conditional classes did not agree");
    identitiesToCheck.emplace_back(first,reversed);
    auto nested=select(x,select(x,arithmetic("+",8,x,number(3)),y),arithmetic("+",8,x,number(3)));
    check(modularEquivalent(nested,arithmetic("+",8,x,number(3)),identities),"repeated truth factors did not preserve conditional identity");
    identitiesToCheck.emplace_back(nested,arithmetic("+",8,x,number(3)));
    auto boolean=select(x,number(1),number(0));
    check(!modularEquivalent(boolean,x,identities),"truth atom was confused with its raw integer");
    check(evaluate(boolean,255,0)!=evaluate(x,255,0),"truth/integer counterexample was not exercised");
    {
        auto wideX=input("X",32),wideY=input("Y",32);
        auto left=arithmetic("*",32,wideX,wideY),right=arithmetic("*",32,wideY,wideX);
        auto zero=arithmetic("-",32,left,right,true);
        auto negative=arithmetic("-",32,zero,number(1),true);
        ControlProof rawProof,signedProof;
        check(rawProof.constantValue(negative->arguments[0])==Integer(0xffffffffU),"fallback changed raw word representation");
        check(signedProof.constantValue(negative)==Integer(-1),"fallback lost the signed Runtime interpretation");
    }
    auto narrowThenWiden=cast(16,arithmetic("+",8,x,number(1)));
    auto wideSum=arithmetic("+",16,cast(16,x),number(1));
    check(!modularEquivalent(narrowThenWiden,wideSum,identities),"narrow wrapping was flattened into wider arithmetic");
    check(evaluate(narrowThenWiden,255,0)!=evaluate(wideSum,255,0),"wrapping counterexample was not exercised");
    auto signedWiden=cast(16,cast(8,x,true),true);
    auto unsignedWiden=cast(16,x);
    check(!modularEquivalent(signedWiden,unsignedWiden,identities),"sign extension was treated as zero extension");
    check(evaluate(signedWiden,128,0)!=evaluate(unsignedWiden,128,0),"signed counterexample was not exercised");
    auto mixed=arithmetic("-",16,narrowThenWiden,wideSum);
    check(!modularConstant(mixed,identities),"mixed-width difference was made constant");
    auto alias=arithmetic("-",8,arithmetic("+",8,x,number(37)),number(37));
    auto reduction=modularReduction(terms,alias,identities);
    check(reduction.has_value(),"wrapping identity did not reduce to an atom");
    identitiesToCheck.emplace_back(alias,*reduction);
    check(!modularReduction(terms,mixed,identities),"narrow wrapping produced a false immutable alias");
    // A wide computation's low bits do survive an outer narrowing cast.
    auto low=cast(8,arithmetic("+",32,cast(32,x),cast(32,y)));
    auto byte=arithmetic("+",8,x,y);
    check(modularEquivalent(low,byte,identities),"safe low-bit identity was lost");
    identitiesToCheck.emplace_back(low,byte);
    for(const auto& [left,right]:identitiesToCheck)
        for(unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b)
            check(evaluate(left,a,b)==evaluate(right,a,b),"modular certificate disagrees with independent wrapping evaluation");
    const std::array<unsigned,10> edges{0,1,127,128,255,256,65535,0x7fffffffU,0x80000000U,0xffffffffU};
    for(const auto& [left,right]:identitiesToCheck) for(auto a:edges) for(auto b:edges)
        check(evaluate(left,a,b)==evaluate(right,a,b),"modular certificate disagrees at a native width boundary");
    auto large=number(1);
    for(unsigned i=0;i<12;++i) {
        auto variable=runtime(terms.make("Other",{number(i)}),32);
        large=arithmetic("*",32,large,arithmetic("+",32,variable,number(1)));
    }
    check(!modularConstant(large,identities),"bounded expansion invented a constant");
    check(modularConstant(arithmetic("-",32,input("X",32),input("X",32)),identities)==Integer(0),"bounded failure poisoned later proof");
    std::cout<<"Modular value certificates: arithmetic identities, width boundaries and independent oracle passed\n";
}
