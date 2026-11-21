#include "source_numeric.hpp"
#include "floating.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <boost/multiprecision/integer.hpp>

namespace vx::relational {
IntegerRange integerDomain(unsigned bits,bool sign) {
    return {sign ? -(Integer(1)<<(bits-1)) : Integer(0),(Integer(1)<<(bits-(sign ? 1 : 0)))-1};
}
IntegerBound convertedIntegerRange(IntegerBound value,unsigned bits,bool sign) {
    if(!bits || bits>32) return {};
    const auto full=integerDomain(bits,sign);if(!value) return full;
    const Integer modulus=Integer(1)<<bits;
    auto floorDivide=[&](Integer n) {Integer q=n/modulus;if(n<0 && n%modulus!=0) --q;return q;};
    const auto lo=floorDivide(value->lower-full.lower),hi=floorDivide(value->upper-full.lower);
    // Sorting wrapped endpoints would be unsound: [250,260] converted to u8
    // includes both 255 and 0. One ordered interval must retain [0,255].
    if(lo!=hi) return full;
    return IntegerRange{value->lower-lo*modulus,value->upper-hi*modulus};
}
IntegerBound arithmeticIntegerRange(const std::string& operation,IntegerBound left,IntegerBound right,
                                    unsigned bits,bool sign) {
    if(!bits || bits>32) return {};
    const bool shift=operation=="<<" || operation==">>";
    left=convertedIntegerRange(left,bits,sign);
    right=convertedIntegerRange(right,shift ? 32 : bits,shift ? false : sign);
    auto convert=[&](IntegerRange range){return convertedIntegerRange(std::move(range),bits,sign);};
    if(operation=="+") return convert({left->lower+right->lower,left->upper+right->upper});
    if(operation=="-") return convert({left->lower-right->upper,left->upper-right->lower});
    if(operation=="*") {
        std::array<Integer,4> values{left->lower*right->lower,left->lower*right->upper,
                                     left->upper*right->lower,left->upper*right->upper};
        const auto [lo,hi]=std::minmax_element(values.begin(),values.end());return convert({*lo,*hi});
    }
    if(operation=="/" || operation=="%") {
        // Keep the full domain if zero is possible. Even a precise range of
        // successful results would not by itself certify this evaluation.
        if(right->lower<=0 && right->upper>=0) return integerDomain(bits,sign);
        if(operation=="/") {
            std::array<Integer,4> values{left->lower/right->lower,left->lower/right->upper,
                                        left->upper/right->lower,left->upper/right->upper};
            const auto [lo,hi]=std::minmax_element(values.begin(),values.end());return convert({*lo,*hi});
        }
        if(left->lower==left->upper && right->lower==right->upper) {
            Integer value=left->lower%right->lower;return convert({value,value});
        }
        const Integer minimum=right->lower>0 ? right->lower : -right->upper;
        const Integer maximum=right->lower>0 ? right->upper : -right->lower;
        if(left->lower>-minimum && left->upper<minimum) return left;
        return IntegerRange{left->lower<0 ? std::max(left->lower,Integer(1-maximum)) : Integer(0),
                            left->upper>0 ? std::min(left->upper,Integer(maximum-1)) : Integer(0)};
    }
    if(shift) {
        if(right->lower<0 || right->upper>=bits) return integerDomain(bits,sign);
        auto apply=[&](const Integer& n,const Integer& count)->Integer {
            const Integer scale=Integer(1)<<count.convert_to<unsigned>();
            if(operation=="<<") return n*scale;
            Integer result=n/scale;
            if(n<0 && n%scale!=0) --result; // Arithmetic right shift rounds down.
            return result;
        };
        std::array<Integer,4> values{apply(left->lower,right->lower),apply(left->lower,right->upper),
                                    apply(left->upper,right->lower),apply(left->upper,right->upper)};
        const auto [lo,hi]=std::minmax_element(values.begin(),values.end());return convert({*lo,*hi});
    }
    if(operation=="&" || operation=="|" || operation=="^") {
        left=convertedIntegerRange(left,bits,false);right=convertedIntegerRange(right,bits,false);
        IntegerRange raw{0,0};
        if(left->lower==left->upper && right->lower==right->upper) {
            if(operation=="&") raw.lower=left->lower&right->lower;
            else if(operation=="|") raw.lower=left->lower|right->lower;
            else raw.lower=left->lower^right->lower;
            raw.upper=raw.lower;
        } else if(operation=="&") raw.upper=std::min(left->upper,right->upper);
        else {
            const Integer largest=std::max(left->upper,right->upper);Integer mask=1;
            while(mask<=largest) mask<<=1;
            raw.upper=mask-1;if(operation=="|") raw.lower=std::max(left->lower,right->lower);
        }
        return convert(raw);
    }
    // Unfamiliar operations retain the complete native domain.
    return integerDomain(bits,sign);
}
std::optional<ScalarFormat> scalarFormat(const Ref& type) {
    ScalarFormat f;
    if(type->name=="Exact") return f;
    if(type->name=="Bool") {f.kind=ScalarFormat::Bool;f.bits=1;return f;}
    if(type->name!="UInt" && type->name!="SInt" && type->name!="Fixed" && type->name!="Float") return {};
    f.bits=type->arguments.at(0)->number.convert_to<unsigned>();
    f.kind=type->name=="Fixed" ? ScalarFormat::Fixed : type->name=="Float" ? ScalarFormat::Float : ScalarFormat::IntegerKind;
    f.sign=type->name=="SInt" || (f.kind==ScalarFormat::Fixed && type->arguments.at(1)->number!=0);
    if(f.kind==ScalarFormat::Fixed) f.fraction=type->arguments.at(2)->number.convert_to<unsigned>();
    return f;
}
unsigned scalarBits(const Ref& type) {return scalarFormat(type).value().bits;}
bool scalarSigned(const Ref& type) {return scalarFormat(type).value().sign;}
bool nativeScalar(const Ref& type) {auto f=scalarFormat(type);return f && f->bits>0 && f->bits<=32;}
Ref extraNumericType(Terms& t,const std::string& spelling) {
    if(spelling=="f16" || spelling=="f32" || spelling=="f64") return t.make("Float",{t.number(std::stoul(spelling.substr(1)))});
    auto dot=spelling.find('.');
    if(dot==std::string::npos || spelling.size()<4 || (spelling[0]!='i' && spelling[0]!='u')) return {};
    auto integer=spelling.substr(1,dot-1),fraction=spelling.substr(dot+1);
    if(integer.empty() || fraction.empty() || integer.find_first_not_of("0123456789")!=std::string::npos || fraction.find_first_not_of("0123456789")!=std::string::npos) return {};
    auto width=[](const std::string& digits) {
        unsigned value=0;
        for(char digit:digits) {
            value=value*10+static_cast<unsigned>(digit-'0');
            if(value>65536) throw ConstructionLimit("fixed-point width exceeds construction budget");
        }
        return value;
    };
    auto i=width(integer),f=width(fraction);
    if(i+f>65536) throw ConstructionLimit("fixed-point width exceeds construction budget");
    if(i+f==0 || (spelling[0]=='i' && i==0)) return {};
    return t.make("Fixed",{t.number(i+f),t.number(spelling[0]=='i' ? 1 : 0),t.number(f)});
}
namespace {
Integer lower(const ScalarFormat& f) {return f.sign ? -(Integer(1)<<(f.bits-1)) : Integer(0);}
Integer upper(const ScalarFormat& f) {return (Integer(1)<<(f.bits-(f.sign ? 1 : 0)))-1;}
Integer wrapped(Integer value,const ScalarFormat& f) {
    Integer modulus=Integer(1)<<f.bits;value%=modulus;if(value<0) value+=modulus;
    if(f.sign && value>upper(f)) value-=modulus;
    return value;
}
bool extended(const Ref& t) {return t->name=="Float" || t->name=="Fixed";}
std::vector<Goal> impossible(Terms& t) {return {equal(t.number(0),t.number(1))};}
Ref runtime(Terms& t,Ref expression,const ScalarFormat& f) {return t.make("Runtime",{expression,t.number(lower(f)),t.number(upper(f))});}
std::optional<Integer> convertConstant(const ScalarFormat& from,const ScalarFormat& to,const Integer& input,bool cast) {
    if(!cast && from.kind!=ScalarFormat::Exact && !(from.kind==ScalarFormat::Float && to.kind==ScalarFormat::Float && from.bits<=to.bits)) return {};
    FloatParts value;
    if(from.kind==ScalarFormat::Float) value=splitFloat(input,from.bits);
    else {value.negative=input<0;value.numerator=value.negative ? -input : input;value.denominator=Integer(1)<<from.fraction;}
    if(to.kind==ScalarFormat::Float) {
        Integer result=joinFloat(value,to.bits);
        if(!cast && from.kind==ScalarFormat::Exact) {
            auto actual=splitFloat(result,to.bits);
            if(actual.infinity || actual.nan || actual.numerator*value.denominator!=value.numerator*actual.denominator) return {};
        }
        return result;
    }
    if(to.kind==ScalarFormat::Bool) return Integer(value.nan || value.infinity || value.numerator!=0 ? 1 : 0);
    if(value.nan || value.infinity || to.kind==ScalarFormat::Exact) return {};
    Integer raw=(value.numerator<<to.fraction)/value.denominator;
    if(value.negative) raw=-raw;
    if(!cast && (raw<lower(to) || raw>upper(to))) return {};
    return cast ? wrapped(raw,to) : raw;
}
}
void numericRules(Program& p,const std::vector<Ref>& formats) {
    auto& t=p.terms;auto x=t.variable("numeric input"),y=t.variable("numeric right"),z=t.variable("numeric result");
    auto from=t.variable("numeric source type"),to=t.variable("numeric target type"),mode=t.variable("numeric conversion mode");
    auto append=[&](const std::string& name,Clause clause){p.relations.at(name).clauses.push_back(std::move(clause));};
    p.builders.emplace("numeric_math",Program::Builder{4,5,[&t](const auto& args)->std::vector<Goal>{
        const auto f=scalarFormat(args[0]).value();unsigned op=args[1]->number.template convert_to<unsigned>();
        // Other goals may still be rejecting this assignment. A builder must
        // be total on its grounded inputs regardless of worklist order.
        if(op==16 && args[3]->kind==Term::Number && (args[3]->number<0 || args[3]->number>0xffffffffu)) return impossible(t);
        if(op==15) return {call("arithmetic%",{args[0],args[2],args[3],args[4]})};
        if(op==14 && (f.kind==ScalarFormat::Float || f.kind==ScalarFormat::Fixed))
            return {build("numeric_unary",{args[0],t.make("|"),args[2],args[4]})};
        if(op==14 && f.sign) return {call("signed_absolute",{args[0],args[2],args[4]})};
        if(args[2]->kind==Term::Number && args[3]->kind==Term::Number) {
            const auto& a=args[2]->number;const auto& b=args[3]->number;Integer result;
            if(op==16) {
                auto multiply=[&](const Integer& left,const Integer& right)->Integer {
                    if(f.kind==ScalarFormat::Float) return floatBinary("*",left,right,f.bits);
                    if(f.kind==ScalarFormat::Exact && left!=0 && right!=0) {
                        Integer l=left<0 ? -left : left,r=right<0 ? -right : right;
                        if(boost::multiprecision::msb(l)+boost::multiprecision::msb(r)>65536)
                            throw ConstructionLimit("exact power exceeds integer construction budget");
                    }
                    Integer product=(left*right)/(Integer(1)<<f.fraction);
                    return f.kind==ScalarFormat::Exact ? product : wrapped(product,f);
                };
                auto exponent=b.template convert_to<uint32_t>();Integer base=a;
                result=f.kind==ScalarFormat::Exact ? Integer(1) : convertConstant(ScalarFormat{},f,Integer(1),true).value();
                while(exponent) {if(exponent&1) result=multiply(result,base);exponent>>=1;if(exponent) base=multiply(base,base);}
            } else if(op==12) {
                if(a<0) return impossible(t);
                result=boost::multiprecision::sqrt(a);
            } else if(op==14) result=a<0 ? -a : a;
            else if(f.kind==ScalarFormat::Float) result=floatMath(op,a,b,f.bits);
            else if(op==5 || op==6) result=(op==5 ? a<b : a>=b) ? a : b;
            else if(f.kind!=ScalarFormat::Fixed) result=a;
            else {
                Integer scale=Integer(1)<<f.fraction,absolute=a<0 ? -a : a;
                Integer integral=absolute/scale,remainder=absolute%scale;
                bool increment=remainder!=0 && ((op==1 && a<0) || (op==2 && a>0) || (op==4 && remainder*2>=scale) ||
                    (op==13 && (remainder*2>scale || (remainder*2==scale && (integral&1)!=0))));
                if(increment) ++integral;
                result=integral*scale;if(a<0) result=-result;result=wrapped(result,f);
            }
            return {equal(args[4],t.number(result))};
        }
        if(!f.bits || f.bits>32) return {unknown("runtime math requires a target representation")};
        if(op==14 || (f.kind==ScalarFormat::IntegerKind && op!=5 && op!=6 && op!=12 && op!=16)) return {equal(args[4],args[2])};
        auto output=f;if(op>=8 && op<=11) {output.kind=ScalarFormat::Bool;output.bits=1;output.sign=false;output.fraction=0;}
        return {equal(args[4],runtime(t,t.make("Math",{args[0],args[1],args[2],args[3]}),output))};
    }});
    p.builders.emplace("numeric_convert",Program::Builder{4,5,[&t](const auto& args)->std::vector<Goal>{
        auto a=scalarFormat(args[0]),b=scalarFormat(args[1]);
        if(!a || !b) return impossible(t);
        bool cast=args[3]->number==1,score=args[3]->number==2;
        if(args[2]->kind==Term::Number) {
            auto result=convertConstant(*a,*b,args[2]->number,cast);
            if(score) return {equal(args[4],t.number(result ? 1 : -1))};
            return result ? std::vector<Goal>{equal(args[4],t.number(*result))} : impossible(t);
        }
        if(args[2]->name!="Runtime" || score) return impossible(t);
        if(!cast && !(a->kind==ScalarFormat::Float && b->kind==ScalarFormat::Float && a->bits<=b->bits)) return impossible(t);
        if(a->bits>32 || b->bits>32 || !a->bits || !b->bits) return {unknown("numeric runtime conversion requires a target representation")};
        return {equal(args[4],runtime(t,t.make("NumericCast",{args[0],args[1],args[2]}),*b))};
    }});
    // Explicit casts carry the source type: equal raw bits have different
    // meanings for integer, fixed-point and IEEE values.
    p.builders.emplace("scalar_cast",Program::Builder{2,4,[&t](const auto& args)->std::vector<Goal>{
        if(!scalarFormat(args[0]) || !scalarFormat(args[1])) return impossible(t);
        if(identical(args[0],args[1])) return {equal(args[2],args[3])};
        if(extended(args[0]) || extended(args[1])) return {build("numeric_convert",{args[0],args[1],args[2],t.number(1),args[3]})};
        return {call("cast_value",{args[1],args[2],args[3]})};
    }});
    p.define("scalar_cast",4,{{{from,to,x,y},{build("scalar_cast",{from,to,x,y})},"type-aware scalar cast"}});
    std::vector<Clause> integral,bitwise;
    for(const auto& type:formats) {
        auto f=scalarFormat(type);if(!f) continue;
        if(f->kind==ScalarFormat::Exact || f->kind==ScalarFormat::IntegerKind) integral.push_back({{type},{},"integer shift count"});
        if(f->kind!=ScalarFormat::Bool && f->kind!=ScalarFormat::Float) bitwise.push_back({{type},{},"bitwise scalar"});
        if(!extended(type)) continue;
        append("arithmetic_operand",{{type},{},"numeric family"});
        append("promote",{{type,t.make("Exact"),type},{},"numeric and exact"});
        append("promote",{{t.make("Exact"),type,type},{},"exact and numeric"});
        for(const auto& other:formats) {
            auto g=scalarFormat(other);if(!g) continue;
            if(f->kind==ScalarFormat::Float && g->kind==ScalarFormat::Float && f->bits!=g->bits)
                append("promote",{{type,other,t.make("Float",{t.number(std::max(f->bits,g->bits))})},{},"floating promotion"});
        }
        for(const std::string op:{"+","-","*","/","%","&","|","^","<<",">>"}) {
            if(f->kind==ScalarFormat::Float && op!="+" && op!="-" && op!="*" && op!="/" && op!="%") continue;
            append("arithmetic"+op,{{type,x,y,z},{build("numeric_binary",{type,t.make(op),x,y,z})},"numeric "+op});
        }
    }
    p.define("integer_operand",1,std::move(integral));p.define("bitwise_operand",1,std::move(bitwise));
    for(const auto& source:formats) for(const auto& target:formats) {
        if(!extended(source) && !extended(target)) continue;
        auto a=scalarFormat(source),b=scalarFormat(target);
        if(identical(source,target)) {
            append("implicit",{{source,target,x,x},{},"numeric identity"});
            append("argument_score",{{source,target,x,t.number(2)},{},"numeric exact match"});
        } else if(a && b && (a->kind==ScalarFormat::Exact || (a->kind==ScalarFormat::Float && b->kind==ScalarFormat::Float && a->bits<=b->bits))) {
            append("implicit",{{source,target,x,y},{build("numeric_convert",{source,target,x,t.number(0),y})},"numeric conversion"});
            if(a->kind==ScalarFormat::Exact)
                append("argument_score",{{source,target,x,y},{build("numeric_convert",{source,target,x,t.number(2),y})},"representable numeric literal"});
            else append("argument_score",{{source,target,x,t.number(1)},{},"floating widening"});
        } else append("argument_score",{{source,target,x,t.number(-1)},{},"numeric family mismatch"});
    }
    p.builders.emplace("numeric_binary",Program::Builder{4,5,[&t](const auto& args)->std::vector<Goal>{
        const auto f=scalarFormat(args[0]).value();const auto& op=args[1]->name;
        bool comparison=op=="==" || op=="!=" || op=="<" || op=="<=" || op==">" || op==">=";
        if(args[2]->kind==Term::Number && args[3]->kind==Term::Number) {
            const auto& a=args[2]->number;const auto& b=args[3]->number;Integer answer;
            if(f.kind==ScalarFormat::Float) {
                if(comparison) {
                    auto order=floatCompare(a,b,f.bits);
                    answer=!order ? op=="!=" : op=="==" ? *order==0 : op=="!=" ? *order!=0 : op=="<" ? *order<0 : op=="<=" ? *order<=0 : op==">" ? *order>0 : *order>=0;
                } else answer=floatBinary(op,a,b,f.bits);
            } else {
                if((op=="/" || op=="%") && b==0) return impossible(t);
                if((op=="<<" || op==">>") && (b<0 || b>=f.bits)) return impossible(t);
                if(op=="+") answer=a+b;
                else if(op=="-") answer=a-b;
                else if(op=="*") answer=(a*b)/(Integer(1)<<f.fraction);
                else if(op=="/") answer=(a<<f.fraction)/b;
                else if(op=="%") answer=a%b;
                else if(op=="&") answer=a&b;
                else if(op=="|") answer=a|b;
                else if(op=="^") answer=a^b;
                else if(op=="<<") answer=a<<b.template convert_to<unsigned>();
                else if(op==">>") answer=a>>b.template convert_to<unsigned>();
                else throw std::logic_error("unexpected fixed operation");
                answer=wrapped(answer,f);
            }
            return {equal(args[4],t.number(answer))};
        }
        if(f.bits>32) return {unknown("numeric runtime operation requires a target representation")};
        auto resultFormat=f;if(comparison) {resultFormat.kind=ScalarFormat::Bool;resultFormat.bits=1;resultFormat.sign=false;}
        auto residual=t.make(f.kind==ScalarFormat::Float ? "FloatBinary" : "FixedBinary",{args[0],args[1],args[2],args[3]});
        return {equal(args[4],runtime(t,residual,resultFormat))};
    }});
    p.builders.emplace("numeric_unary",Program::Builder{3,4,[&t](const auto& args)->std::vector<Goal>{
        const auto f=scalarFormat(args[0]).value();const auto& op=args[1]->name;
        if(op=="~" && f.kind==ScalarFormat::Float) return impossible(t);
        if(args[2]->kind==Term::Number) {
            Integer result=args[2]->number;
            if(f.kind==ScalarFormat::Float) result=floatUnary(op,result,f.bits);
            else if(op=="-") result=wrapped(-result,f);
            else if(op=="~") result=wrapped(~result,f);
            else if(op=="|") result=wrapped(result<0 ? -result : result,f);
            return {equal(args[3],t.number(result))};
        }
        if(f.bits>32) return {unknown("numeric runtime unary operation requires a target representation")};
        return {equal(args[3],runtime(t,t.make("NumericUnary",{args[0],args[1],args[2]}),f))};
    }});
}
} // namespace vx::relational
