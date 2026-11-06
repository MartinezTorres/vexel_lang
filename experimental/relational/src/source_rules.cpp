#include "machine.hpp"
#include <set>

namespace vx::relational {
void signedRules(Program&,const std::set<unsigned>&);
// These are language/representation adapters. The kernel knows neither UInt
// nor Runtime nor Vexel's promotion and wrapping rules.
void sourceRules(Program& p, const std::set<unsigned>& widths) {
    auto& t = p.terms;
    auto exact = t.make("Exact");
    auto x=t.variable("x"), y=t.variable("y"), z=t.variable("z");
    auto e=t.variable("expression"), lo=t.variable("lo"), hi=t.variable("hi");
    auto runtime=t.make("Runtime",{e,lo,hi});
    auto boolean=t.make("Bool");
    p.define("numeric_value_kind",2,{
        {{x,t.make("Integer")},{add(x,t.number(0),x)},"integer relationship"},
        {{runtime,t.make("Residual")},{},"runtime expression"}
    });
    p.define("value_range",3,{
        {{x,x,x},{add(x,t.number(0),x)},"integer"},
        {{runtime,lo,hi},{},"runtime boundary"}
    });
    auto sameType=t.variable("sameType");
    std::vector<Clause> promote{{{sameType,sameType,sameType},{},"same semantic type"}};
    std::vector<Clause> convert{
        {{exact,exact,x,x},{},"exact identity"},
        {{boolean,boolean,x,x},{},"boolean identity"},
        {{t.make("String"),t.make("String"),x,x},{},"string identity"},
        {{exact,boolean,x,x},{lessEqual(t.number(0),x),lessEqual(x,t.number(1))},"boolean literal"}
    };
    std::vector<Clause> casts{
        {{exact,x,x},{},"exact identity"},
        {{boolean,x,t.number(0)},{equal(x,t.number(0))},"boolean zero"},
        {{boolean,x,t.number(1)},{add(x,t.number(0),x),different(x,t.number(0))},"boolean nonzero"},
        {{boolean,runtime,t.make("Runtime",{t.make("Compare",{t.make("!="),runtime,t.number(0)}),t.number(0),t.number(1)})},{},"runtime boolean cast"}
    };
    std::vector<Clause> ranges;
    auto outLo=t.variable("outLo"), outHi=t.variable("outHi");
    for (unsigned bits:widths) {
        auto type=t.make("UInt",{t.number(bits)});
        auto modulus=t.number(Integer(1)<<bits), maximum=t.number((Integer(1)<<bits)-1);
        promote.push_back({{type,exact,type},{},"unsigned and exact"});
        promote.push_back({{exact,type,type},{},"exact and unsigned"});
        convert.push_back({{exact,type,x,x},
            {call("value_range",{x,lo,hi}),lessEqual(t.number(0),lo),lessEqual(hi,maximum)},"representable exact"});
        for (unsigned other:widths) {
            auto rhs=t.make("UInt",{t.number(other)});
            if(bits!=other) promote.push_back({{type,rhs,t.make("UInt",{t.number(std::max(bits,other))})},{},"unsigned promotion"});
            if (bits<=other) convert.push_back({{type,rhs,x,x},{},"unsigned widening"});
        }
        casts.push_back({{type,x,y},{modulo(x,modulus,y)},"integer cast"});
        casts.push_back({{type,runtime,t.make("Runtime",{t.make("Cast",{t.number(bits),runtime}),outLo,outHi})},
            {call("wrapped_range",{t.number(bits),lo,hi,outLo,outHi})},"runtime cast"});
        if(bits>32) casts.back().body.push_back(unknown("wide runtime cast requires a target representation"));
        ranges.push_back({{t.number(bits),lo,hi,lo,hi},
            {lessEqual(t.number(0),lo),lessEqual(hi,maximum)},"no wrapping"});
        ranges.push_back({{t.number(bits),lo,hi,t.number(0),maximum},
            {lessEqual(lo,t.number(-1))},"possible negative wrap"});
        ranges.push_back({{t.number(bits),lo,hi,t.number(0),maximum},
            {lessEqual(t.number(0),lo),lessEqual(modulus,hi)},"possible positive wrap"});
    }
    p.define("promote",3,std::move(promote));
    std::vector<Clause> operands{{{exact},{},"exact integer"}};
    for(unsigned bits:widths) operands.push_back({{t.make("UInt",{t.number(bits)})},{},"unsigned integer"});
    p.define("arithmetic_operand",1,std::move(operands));
    p.define("logical_operand",1,{{{exact},{},"Boolean literal"},{{boolean},{},"Boolean"}});
    p.define("arithmetic_type",3,{{{x,y,z},{call("arithmetic_operand",{x}),call("arithmetic_operand",{y}),call("promote",{x,y,z})},"builtin integer signature"}});
    p.define("implicit",4,std::move(convert));
    p.define("cast_value",3,std::move(casts));
    p.define("wrapped_range",5,std::move(ranges));

    // Applicability is a total relationship on the supported argument family.
    // -1 means inapplicable, 0 generic, 1 conversion, 2 exact type match.
    // Failed bodies never lower a candidate's score or enable a fallback.
    auto evidence=t.variable("evidence");
    std::vector<Clause> scores{{{sameType,t.make("Generic"),evidence,t.number(0)},{},"generic"}};
    std::vector<Ref> scalarTypes{boolean,t.make("String")};
    for(unsigned bits:widths) scalarTypes.push_back(t.make("UInt",{t.number(bits)}));
    for(const auto& target:scalarTypes) {
        if(target->name=="String") scores.push_back({{exact,target,evidence,t.number(-1)},{},"integer is not a string"});
        else {
        auto maximum=target->name=="Bool" ? t.number(1) : t.number((Integer(1)<<target->arguments[0]->number.convert_to<unsigned>())-1);
        scores.push_back({{exact,target,x,t.number(1)},{lessEqual(t.number(0),x),lessEqual(x,maximum)},"representable literal"});
        scores.push_back({{exact,target,x,t.number(-1)},{lessEqual(x,t.number(-1))},"negative literal"});
        auto above=t.number(maximum->number+1);
        scores.push_back({{exact,target,x,t.number(-1)},{lessEqual(above,x)},"large literal"});
        }
        for(const auto& supplied:scalarTypes) {
            int score=identical(supplied,target) ? 2 : supplied->name=="UInt" && target->name=="UInt" && supplied->arguments[0]->number<target->arguments[0]->number ? 1 : -1;
            scores.push_back({{supplied,target,evidence,t.number(score)},{},"scalar applicability"});
        }
        for(const auto& aggregate:{t.make("Array",{x,y}),t.make("Record",{x,y,z})})
            scores.push_back({{aggregate,target,evidence,t.number(-1)},{},"aggregate mismatch"});
    }
    p.define("argument_score",4,std::move(scores));
    p.builders.emplace("string_extent",Program::Builder{1,2,[&t](const auto& args)->std::vector<Goal>{
        if(args[0]->name!="Bytes") return {equal(t.number(0),t.number(1))};
        return {equal(args[1],t.number(args[0]->arguments.size()))};
    }});
    p.define("element_type",2,{
        {{t.make("Array",{x,y}),x},{},"array element"},
        {{t.make("String"),t.make("UInt",{t.number(8)})},{},"string byte"}
    });
    p.define("extent_value",3,{
        {{t.make("Array",{x,y}),e,y},{},"array extent independent of value"},
        {{t.make("String"),x,y},{build("string_extent",{x,y})},"string byte count"}
    });
    p.define("score_sum",3,{
        {{t.number(-1),x,t.number(-1)},{},"already inapplicable"},
        {{x,t.number(-1),t.number(-1)},{lessEqual(t.number(0),x)},"inapplicable argument"},
        {{x,y,z},{lessEqual(t.number(0),x),lessEqual(t.number(0),y),add(x,y,z)},"specificity sum"}
    });

    for(std::string op:{"==","!=","<","<=",">",">="}) {
        auto next=t.variable("successor");
        auto truth=[&](bool positive) -> std::vector<Goal> {
            if(op=="==" || op=="!=") return {(positive==(op=="==")) ? equal(x,y) : different(x,y),add(x,t.number(0),x),add(y,t.number(0),y)};
            bool swap=op==">" || op==">=";
            auto a=swap ? y : x,b=swap ? x : y;
            bool inclusive=op=="<=" || op==">=";
            if(!positive) {std::swap(a,b);inclusive=!inclusive;}
            return inclusive ? std::vector<Goal>{lessEqual(a,b)} : std::vector<Goal>{add(a,t.number(1),next),lessEqual(next,b)};
        };
        std::vector<Clause> comparison{
            {{x,y,t.number(1)},truth(true),"comparison true"},
            {{x,y,t.number(0)},truth(false),"comparison false"}
        };
        auto be=t.variable("rightExpression"),bl=t.variable("rightLo"),bh=t.variable("rightHi");
        for(unsigned family=0;family<3;++family) {
            auto a=family==2 ? x : runtime;
            auto b=family==0 ? y : t.make("Runtime",{be,bl,bh});
            std::vector<Goal> body;
            if(family==0) body.push_back(add(y,t.number(0),y));
            if(family==2) body.push_back(add(x,t.number(0),x));
            comparison.push_back({{a,b,t.make("Runtime",{t.make("Compare",{t.make(op),a,b}),t.number(0),t.number(1)})},body,"runtime comparison"});
        }
        p.define("compare"+op,3,std::move(comparison));
    }

    for (std::string op:{"+","-","*","/","%","&","|","^","<<",">>"}) {
        auto arithmetic=[&](Ref a,Ref b,Ref result) {
            return op=="+" ? add(a,b,result) : op=="-" ? add(result,b,a) : op=="*" ? multiply(a,b,result) : compute(op,a,b,result);
        };
        std::vector<Clause> clauses{{{exact,x,y,z},{arithmetic(x,y,z)},"exact "+op}};
        std::vector<unsigned> families(widths.begin(),widths.end());families.push_back(0);
        for (unsigned bits:families) {
            auto type=bits ? t.make("UInt",{t.number(bits)}) : exact,raw=t.variable("raw");
            if(bits) {
                std::vector<Goal> constant{arithmetic(x,y,raw),modulo(raw,t.number(Integer(1)<<bits),z)};
                if(op=="<<" || op==">>") {constant.push_back(lessEqual(t.number(0),y));constant.push_back(lessEqual(y,t.number(bits-1)));}
                clauses.push_back({{type,x,y,z},constant,"unsigned "+op});
            }
            for (unsigned kind=0;kind<3;++kind) {
                auto a=kind==2 ? x : runtime;
                auto be=t.variable("rightExpression"), bl=t.variable("rightLo"), bh=t.variable("rightHi");
                auto b=kind==0 ? y : t.make("Runtime",{be,bl,bh});
                auto al=t.variable("aLo"), ah=t.variable("aHi"), low=t.variable("low"), high=t.variable("high");
                auto lower=t.variable("lower"), upper=t.variable("upper");
                std::vector<Goal> body{call("value_range",{a,al,ah}),call("value_range",{b,bl,bh})};
                if(bits>32) body.push_back(unknown("wide runtime arithmetic requires a target representation"));
                if(kind==0) body.push_back(add(y,t.number(0),y));
                if(kind==2) body.push_back(add(x,t.number(0),x));
                if(op=="+" || op=="-" || op=="*") {
                    if(op=="-") {body.push_back(arithmetic(al,bh,low));body.push_back(arithmetic(ah,bl,high));}
                    else {body.push_back(arithmetic(al,bl,low));body.push_back(arithmetic(ah,bh,high));}
                    if(op=="*") {body.push_back(lessEqual(t.number(0),al));body.push_back(lessEqual(t.number(0),bl));}
                    if(bits) body.push_back(call("wrapped_range",{t.number(bits),low,high,lower,upper}));
                    else {body.push_back(equal(lower,low));body.push_back(equal(upper,high));}
                } else if(bits) {
                    body.push_back(equal(lower,t.number(0)));body.push_back(equal(upper,t.number((Integer(1)<<bits)-1)));
                } else continue; // Runtime exact division/bitwise need a derived representation range.
                auto residual=bits ? t.make("Arithmetic",{t.make(op),t.number(bits),a,b}) : t.make("ExactArithmetic",{t.make(op),lower,upper,a,b});
                clauses.push_back({{type,a,b,t.make("Runtime",{residual,lower,upper})},body,"runtime "+op});
            }
        }
        p.define("arithmetic"+op,4,std::move(clauses));
    }
    auto a=t.variable("a"), b=t.variable("b");
    p.define("maximum",3,{
        {{a,b,b},{lessEqual(a,b)},"right"},
        {{a,b,a},{lessEqual(b,a),different(a,b)},"left"}
    });
    p.define("minimum",3,{
        {{a,b,a},{lessEqual(a,b)},"left"},
        {{a,b,b},{lessEqual(b,a),different(a,b)},"right"}
    });
    signedRules(p,widths);
}
} // namespace vx::relational
