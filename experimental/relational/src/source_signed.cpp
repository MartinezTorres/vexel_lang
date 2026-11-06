#include "machine.hpp"
#include <algorithm>
#include <set>

namespace vx::relational {
// Signed values are mathematical integers in the source model. Wrapping is
// an explicit relationship; the target's bit pattern is a later realization.
void signedRules(Program& p,const std::set<unsigned>& widths) {
    auto& t=p.terms;
    auto n=[&](Integer value){return t.number(std::move(value));};
    auto x=t.variable("signed left"),y=t.variable("signed right"),z=t.variable("signed result");
    auto expr=t.variable("signed expression"),lo=t.variable("signed lower"),hi=t.variable("signed upper");
    auto runtime=t.make("Runtime",{expr,lo,hi}),exact=t.make("Exact");
    auto append=[&](const std::string& name,Clause clause){p.relations.at(name).clauses.push_back(std::move(clause));};
    std::vector<Clause> absolute;
    for(unsigned bits:widths) {
        auto type=t.make("SInt",{n(bits)}),half=n(Integer(1)<<(bits-1)),modulus=n(Integer(1)<<bits);
        auto minimum=n(-half->number),maximum=n(half->number-1);
        append("arithmetic_operand",{{type},{},"signed integer"});
        append("promote",{{type,exact,type},{},"signed and exact"});
        append("promote",{{exact,type,type},{},"exact and signed"});
        append("implicit",{{exact,type,x,x},{call("value_range",{x,lo,hi}),lessEqual(minimum,lo),lessEqual(hi,maximum)},"representable signed literal"});
        for(unsigned other:widths) {
            auto rhs=t.make("SInt",{n(other)});
            if(bits!=other) append("promote",{{type,rhs,t.make("SInt",{n(std::max(bits,other))})},{},"signed promotion"});
            if(bits<=other) append("implicit",{{type,rhs,x,x},{},"signed widening"});
        }
        auto wrap=[&](Ref input,Ref output) {
            auto biased=t.variable("biased signed integer"),remainder=t.variable("signed residue");
            return std::vector<Goal>{add(input,half,biased),modulo(biased,modulus,remainder),add(output,half,remainder)};
        };
        append("cast_value",{{type,x,y},wrap(x,y),"signed wrapping cast"});
        append("cast_value",{{type,runtime,t.make("Runtime",{t.make("SignedCast",{n(bits),runtime}),minimum,maximum})},bits>32 ? std::vector<Goal>{unknown("wide runtime signed cast requires a target representation")} : std::vector<Goal>{},"runtime signed cast"});
        auto above=n(maximum->number+1),below=n(minimum->number-1),evidence=t.variable("signed match evidence");
        append("argument_score",{{exact,type,x,n(1)},{lessEqual(minimum,x),lessEqual(x,maximum)},"signed literal applicability"});
        append("argument_score",{{exact,type,x,n(-1)},{lessEqual(x,below)},"signed literal below range"});
        append("argument_score",{{exact,type,x,n(-1)},{lessEqual(above,x)},"signed literal above range"});
        std::vector<Ref> otherTypes{t.make("Bool"),t.make("String")};
        for(unsigned other:widths) {
            otherTypes.push_back(t.make("UInt",{n(other)}));
            append("argument_score",{{t.make("SInt",{n(other)}),type,evidence,n(other==bits ? 2 : other<bits ? 1 : -1)},{},"signed scalar applicability"});
        }
        for(const auto& other:otherTypes) {
            append("argument_score",{{other,type,evidence,n(-1)},{},"signed family mismatch"});
            append("argument_score",{{type,other,evidence,n(-1)},{},"signed family mismatch"});
        }
        for(const auto& other:{t.make("Array",{x,y}),t.make("Record",{x,y,z})})
            append("argument_score",{{other,type,evidence,n(-1)},{},"signed aggregate mismatch"});
        for(const std::string op:{"+","-","*","/","%","&","|","^","<<",">>"}) {
            auto raw=t.variable("signed unwrapped result");
            auto body=wrap(raw,z);
            body.push_back(op=="+" ? add(x,y,raw) : op=="-" ? add(raw,y,x) : op=="*" ? multiply(x,y,raw) : compute(op,x,y,raw));
            if(op=="<<" || op==">>") {body.push_back(lessEqual(n(0),y));body.push_back(lessEqual(y,n(bits-1)));}
            append("arithmetic"+op,{{type,x,y,z},std::move(body),"signed "+op});
            for(unsigned kind=0;kind<3;++kind) {
                auto second=t.make("Runtime",{t.variable("rhs expression"),t.variable("rhs lower"),t.variable("rhs upper")});
                auto a=kind==2 ? x : runtime,b=kind==0 ? y : second;
                std::vector<Goal> numeric;
                if(bits>32) numeric.push_back(unknown("wide runtime signed arithmetic requires a target representation"));
                if(kind==0) numeric.push_back(add(y,n(0),y));
                if(kind==2) numeric.push_back(add(x,n(0),x));
                append("arithmetic"+op,{{type,a,b,t.make("Runtime",{t.make("SignedArithmetic",{t.make(op),n(bits),a,b}),minimum,maximum})},std::move(numeric),"runtime signed "+op});
            }
        }
        absolute.push_back({{type,x,x},{lessEqual(n(0),x),lessEqual(x,maximum)},"nonnegative signed absolute"});
        absolute.push_back({{type,x,y},{lessEqual(minimum,x),lessEqual(x,n(-1)),call("arithmetic-",{type,n(0),x,y})},"negative signed absolute"});
        absolute.push_back({{type,runtime,t.make("Runtime",{t.make("SignedAbsolute",{n(bits),runtime}),minimum,maximum})},bits>32 ? std::vector<Goal>{unknown("wide runtime signed absolute requires a target representation")} : std::vector<Goal>{},"runtime signed absolute"});
    }
    p.define("signed_absolute",3,std::move(absolute));
    for(const std::string op:{"==","!=","<","<=",">",">="}) {
        auto bits=t.variable("comparison width");
        std::vector<Clause> clauses{{{bits,x,y,z},{add(x,n(0),x),add(y,n(0),y),call("compare"+op,{x,y,z})},"signed constant comparison"}};
        for(unsigned kind=0;kind<3;++kind) {
            auto second=t.make("Runtime",{t.variable("rhs expression"),t.variable("rhs lower"),t.variable("rhs upper")});
            auto a=kind==2 ? x : runtime,b=kind==0 ? y : second;
            std::vector<Goal> numeric;
            if(kind==0) numeric.push_back(add(y,n(0),y));
            if(kind==2) numeric.push_back(add(x,n(0),x));
            clauses.push_back({{bits,a,b,t.make("Runtime",{t.make("SignedCompare",{t.make(op),bits,a,b}),n(0),n(1)})},std::move(numeric),"signed runtime comparison"});
        }
        p.define("signed_compare"+op,4,std::move(clauses));
    }
}
} // namespace vx::relational
