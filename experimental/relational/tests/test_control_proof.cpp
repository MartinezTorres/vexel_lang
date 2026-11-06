#include "source_control_proof.hpp"
#include <cstdlib>
#include <iostream>

using namespace vx::relational;
namespace {
unsigned assertions=0;
void check(bool value,const std::string& detail) {
    ++assertions;if(!value) {std::cerr<<"control proof: "<<detail<<'\n';std::exit(1);}
}
struct Fixture {
    Terms terms;
    Ref number(Integer n) {return terms.number(std::move(n));}
    Ref runtime(Ref value,Integer lo=0,Integer hi=255) {return terms.make("Runtime",{value,number(lo),number(hi)});}
    Ref input(std::string name,Integer lo=0,Integer hi=255) {return runtime(terms.make("Input",{terms.make(name)}),lo,hi);}
    Ref op(std::string name,unsigned width,Ref a,Ref b,bool sign=false) {
        return runtime(terms.make(sign ? "SignedArithmetic" : "Arithmetic",{terms.make(name),number(width),a,b}),
            sign ? -(Integer(1)<<(width-1)) : Integer(0),sign ? (Integer(1)<<(width-1))-1 : (Integer(1)<<width)-1);
    }
    Ref compare(std::string name,Ref a,Ref b,unsigned width=0) {
        return runtime(width ? terms.make("SignedCompare",{terms.make(name),number(width),a,b}) :
            terms.make("Compare",{terms.make(name),a,b}),0,1);
    }
};
void byteArithmetic() {
    for(bool sign:{false,true}) for(const auto& operation:{"+","-","*","&","|","^","<<",">>"}) {
        Fixture f;auto x=f.input("x");auto y=f.number(3);ControlProof proof;
        auto result=f.op(operation,8,x,y,sign);
        for(unsigned n=0;n<256;++n) {
            std::string op=operation;unsigned value=0;
            if(op=="+") value=n+3;
            if(op=="-") value=n-3;
            if(op=="*") value=n*3;
            if(op=="&") value=n&3;
            if(op=="|") value=n|3;
            if(op=="^") value=n^3;
            if(op=="<<") value=n<<3;
            if(op==">>") value=sign && n>=128 ? (n>>3)|224U : n>>3;
            value&=255;
            int expected=sign && value>=128 ? static_cast<int>(value)-256 : static_cast<int>(value);
            auto premise=proof.read(f.compare("==",x,f.number(n)));
            auto matches=proof.read(f.compare("==",result,f.number(expected),sign ? 8 : 0));
            check(proof.conjunction(premise,proof.negate(matches))==0,op+" byte "+std::to_string(n));
            check(premise!=0,"byte input witness");
        }
    }
}
void pairedArithmetic() {
    for(const auto& operation:{"+","-","*","&","|","^"}) {
        Fixture f;auto x=f.input("x",0,15),y=f.input("y",0,15);ControlProof proof;
        auto result=f.op(operation,4,x,y);
        for(unsigned a=0;a<16;++a) for(unsigned b=0;b<16;++b) {
            std::string op=operation;unsigned expected=0;
            if(op=="+") expected=a+b;
            if(op=="-") expected=a-b;
            if(op=="*") expected=a*b;
            if(op=="&") expected=a&b;
            if(op=="|") expected=a|b;
            if(op=="^") expected=a^b;
            auto premise=proof.conjunction(proof.read(f.compare("==",x,f.number(a))),proof.read(f.compare("==",y,f.number(b))));
            auto matches=proof.read(f.compare("==",result,f.number(expected&15)));
            check(proof.conjunction(premise,proof.negate(matches))==0,op+" paired native words");
        }
    }
}
void comparisonsAndWidths() {
    for(bool sign:{false,true}) for(const auto& operation:{"==","!=","<","<=",">",">="}) {
        Fixture f;auto x=f.input("x");ControlProof proof;auto relation=f.compare(operation,x,f.number(7),sign ? 8 : 0);
        auto value=proof.read(relation);
        for(int n=0;n<256;++n) {
            auto a=sign && n>=128 ? n-256 : n;std::string op=operation;
            bool expected=op=="==" ? a==7 : op=="!=" ? a!=7 : op=="<" ? a<7 : op=="<=" ? a<=7 : op==">" ? a>7 : a>=7;
            auto premise=proof.read(f.compare("==",x,f.number(n)));
            check(proof.conjunction(premise,expected ? proof.negate(value) : value)==0,"signed/unsigned ordering");
        }
        check(proof.equivalent(value,proof.read(proof.term(value,f.terms)))==1,"rendered control preserves its relation");
    }
    for(unsigned width:{1,4,8,16,32}) {
        Fixture f;Integer mask=(Integer(1)<<width)-1;auto x=f.input("x",0,mask);ControlProof proof;
        auto zero=f.op("-",width,x,x),back=f.op("-",width,f.op("+",width,x,f.number(1)),f.number(1));
        check(proof.read(f.compare("==",zero,f.number(0)))==1,"self difference at native width");
        check(proof.read(f.compare("==",back,x))==1,"native wrapping round trip");
        auto cast=f.runtime(f.terms.make("SignedCast",{f.number(width),x}),-(Integer(1)<<(width-1)),(Integer(1)<<(width-1))-1);
        auto negative=proof.read(f.compare("<",cast,f.number(0),width));
        auto upper=proof.read(f.compare(">=",x,f.number(Integer(1)<<(width-1))));
        check(proof.equivalent(negative,upper)==1,"signed cast sign bit");
    }
}
void partialKnowledgeAndScopes() {
    Fixture f;auto x=f.input("x");ControlProof proof;
    auto first=proof.read(f.compare("<",x,f.number(128))),second=proof.read(f.compare("<",x,f.number(129)));
    check(proof.conjunction(first,proof.negate(second))==0,"ordered guards share bits");
    auto call=f.runtime(f.terms.make("CallValue",{f.number(0),f.number(0)}));
    auto maybe=proof.read(f.compare("!=",call,f.number(0)));
    check(maybe>1,"opaque call cannot be folded");
    check(proof.disjunction(maybe,first)>1,"opaque disjunction remains symbolic");
    check(proof.conjunction(maybe,first)>1,"opaque conjunction remains open");
    check(proof.read(f.compare("==",f.op("-",8,call,call),f.number(0)))==1,"correlated opaque values can prove identities");
    check(proof.constantValue(f.op("-",8,call,call))==Integer(0),"constant native return certificate");
    auto divide=f.op("/",8,x,x);auto shifted=f.op("<<",8,x,x);
    check(!proof.constantValue(divide),"division needs its definedness proof");
    check(!proof.constantValue(shifted),"dynamic shift needs its definedness proof");
    auto now=f.terms.make("LoopReady",{f.number(1),f.number(3)}),next=f.terms.make("NextLoopReady",{f.number(1),f.number(3)});
    auto a=proof.read(now),b=proof.read(next);
    check(a==proof.read(f.runtime(now,0,1)),"readiness wrapper shares its atom");
    check(proof.substitute(b,{{show(next),a}})==a,"loop readiness renaming");
    auto loop=f.runtime(f.terms.make("LoopSlotValue",{f.number(1),f.number(3),f.number(0)}));
    auto predicate=proof.read(f.compare("<",loop,f.number(7)));
    std::function<bool(const Ref&)> unstable=[&](const Ref& v){return v->name=="LoopSlotValue" || std::any_of(v->arguments.begin(),v->arguments.end(),unstable);};
    check(proof.exists(proof.conjunction(predicate,first),unstable)==first,"quantification follows numeric bit provenance");
    Fixture intervals;ControlProof rangeProof;
    auto bounded=intervals.input("bounded",3,10);
    check(rangeProof.read(intervals.compare(">=",bounded,intervals.number(3)))==1,"finite interval lower bound");
    check(rangeProof.read(intervals.compare("<=",bounded,intervals.number(10)))==1,"finite interval upper bound");
    for(int n=3;n<=10;++n) check(rangeProof.read(intervals.compare("==",bounded,intervals.number(n)))!=0,"every interval value has a witness");
}
void wordVariableRenaming() {
    for(bool sign:{false,true}) {
        Fixture f;ControlProof proof;
        auto current=f.runtime(f.terms.make("CurrentWord"),sign ? -128 : 0,sign ? 127 : 255);
        auto next=f.runtime(f.terms.make("NextWord"),sign ? -128 : 0,sign ? 127 : 255);
        auto rename=proof.variableRenaming(next,current);
        check(rename.has_value(),"native word has a renaming");
        check(rename->size()==8,"signed extension shares its sign bit");
        for(int n=sign ? -128 : 0;n<=(sign ? 127 : 255);++n)
            check(proof.substitute(proof.read(f.compare("==",next,f.number(n))),*rename)==proof.read(f.compare("==",current,f.number(n))),"renamed word preserves every native value");
    }
    Fixture f;ControlProof proof;auto byte=f.runtime(f.terms.make("Byte"));
    check(!proof.variableRenaming(f.runtime(f.terms.make("Clamped"),0,2),byte),"clamped encoding is not a fresh word variable");
    check(!proof.variableRenaming(f.runtime(f.terms.make("Tiny"),0,1),byte),"renaming cannot discard target bits");
    check(!proof.variableRenaming(f.op("+",8,byte,f.number(1)),byte),"composite expression is not a fresh word variable");
}
void definedDivisionAndShifts() {
    for(bool sign:{false,true}) for(int divisor:{-128,-7,-1,1,3,127,255}) {
        if((!sign && divisor<0) || (sign && divisor>127)) continue;
        Fixture f;ControlProof proof;auto x=f.input("dividend");
        auto quotient=f.op("/",8,x,f.number(divisor),sign),remainder=f.op("%",8,x,f.number(divisor),sign);
        for(int n=0;n<256;++n) {
            int a=sign && n>=128 ? n-256 : n;
            auto normalized=[&](int value){value&=255;return sign && value>=128 ? value-256 : value;};
            auto premise=proof.read(f.compare("==",x,f.number(n)));
            for(const auto& [result,expected]:{std::pair{quotient,normalized(a/divisor)},std::pair{remainder,normalized(a%divisor)}}) {
                auto matches=proof.read(f.compare("==",result,f.number(expected),sign ? 8 : 0));
                check(proof.conjunction(premise,proof.negate(matches))==0,"signed/unsigned division byte oracle");
            }
        }
    }
    for(bool sign:{false,true}) {
        Fixture f;ControlProof proof;auto x=f.input("numerator",sign ? -32 : 0,sign ? 31 : 63);
        auto y=f.input("denominator",sign ? -7 : 1,sign ? -1 : 7);
        auto quotient=f.op("/",8,x,y,sign),remainder=f.op("%",8,x,y,sign);
        for(int a=sign ? -32 : 0;a<=(sign ? 31 : 63);++a) for(int b=sign ? -7 : 1;b<=(sign ? -1 : 7);++b) {
            auto premise=proof.conjunction(proof.read(f.compare("==",x,f.number(a))),proof.read(f.compare("==",y,f.number(b))));
            for(const auto& [result,expected]:{std::pair{quotient,a/b},std::pair{remainder,a%b}}) {
                auto matches=proof.read(f.compare("==",result,f.number(expected),sign ? 8 : 0));
                check(proof.conjunction(premise,proof.negate(matches))==0,"variable division integer oracle");
            }
        }
    }
    for(bool sign:{false,true}) for(const auto& op:{"<<",">>"}) {
        Fixture f;ControlProof proof;auto x=f.input("shifted"),y=f.input("count",0,7);
        auto shifted=f.op(op,8,x,y,sign);
        for(unsigned n=0;n<256;++n) for(unsigned count=0;count<8;++count) {
            unsigned bits=std::string(op)=="<<" ? n<<count : n>>count;
            if(std::string(op)==">>" && sign && n>=128 && count) bits|=255U<<(8-count);
            bits&=255;int expected=sign && bits>=128 ? static_cast<int>(bits)-256 : static_cast<int>(bits);
            auto premise=proof.conjunction(proof.read(f.compare("==",x,f.number(n))),proof.read(f.compare("==",y,f.number(count))));
            auto matches=proof.read(f.compare("==",shifted,f.number(expected),sign ? 8 : 0));
            check(proof.conjunction(premise,proof.negate(matches))==0,"variable shift independent byte oracle");
        }
    }
    for(unsigned width:{1,3,8,16,32}) {
        Fixture f;ControlProof proof;auto x=f.input("word",0,(Integer(1)<<width)-1);
        check(proof.read(f.compare("==",f.op("/",width,x,f.number(1)),x))==1,"division identity at each width");
        check(proof.constantValue(f.op("%",width,x,f.number(1)))==Integer(0),"remainder identity at each width");
        auto y=f.input("smallCount",0,width-1);
        check(proof.constantValue(f.op("<<",width,f.number(0),y))==Integer(0),"zero barrel shift at each width");
        Integer minimum=-(Integer(1)<<(width-1));
        check(proof.constantValue(f.op("/",width,f.number(minimum),f.number(-1),true))==minimum,"minimum divided by minus one wraps");
        check(proof.constantValue(f.op("%",width,f.number(minimum),f.number(-1),true))==Integer(0),"minimum remainder at each width");
        const auto modulus=std::uint64_t{1}<<width,mask=modulus-1;
        for(bool sign:{false,true}) for(auto a:{std::uint64_t{0},std::uint64_t{1},mask,modulus/2,mask&std::uint64_t{0xa5a5a5a5}})
            for(auto b:{std::uint64_t{1},std::uint64_t{2},std::uint64_t{3},modulus/2,mask}) {
                b&=mask;if(!b) continue;
                auto signedValue=[&](std::uint64_t bits){return sign && bits>=modulus/2 ?
                    static_cast<std::int64_t>(bits)-static_cast<std::int64_t>(modulus) : static_cast<std::int64_t>(bits);};
                const auto left=signedValue(a),right=signedValue(b);
                for(const auto& op:{"/","%"}) {
                    auto mathematical=std::string(op)=="/" ? left/right : left%right;
                    auto expected=signedValue(static_cast<std::uint64_t>(mathematical)&mask);
                    check(proof.constantValue(f.op(op,width,f.number(left),f.number(right),sign))==Integer(expected),
                          "full-width division extrema match independent 64-bit arithmetic");
                }
            }
    }
    Fixture f;ControlProof proof;auto x=f.input("unsafe");
    for(auto value:{f.op("/",8,x,f.number(0)),f.op("%",8,x,f.number(0)),
                    f.op("<<",8,x,f.number(8)),f.op(">>",8,x,f.number(-1)),
                    f.op("/",8,x,f.input("maybeZero",0,7))})
        check(!proof.constantValue(value),
              "possibly invalid arithmetic remains opaque");
}

}
int main() {
    byteArithmetic();pairedArithmetic();comparisonsAndWidths();partialKnowledgeAndScopes();wordVariableRenaming();definedDivisionAndShifts();
    std::cout<<assertions<<" control proof assertions passed\n";
}
